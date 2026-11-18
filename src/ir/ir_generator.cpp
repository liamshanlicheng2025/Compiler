#include "ir/ir_generator.h"

#include <string>

#include "ll.h"
#include "type_map.h"
#include "semantic/symbol_table.h"

IRGenerator::IRGenerator(SymbolTable& symtab,
                         const std::unordered_map<const Expr*, Type>& exprTypes)
    : symtab_(symtab), exprTypes_(exprTypes) {}

std::string IRGenerator::run(const CratePtr& crate) {
    visit(*crate);
    return ir_.str();
}

const ll::Module& IRGenerator::module() const {
    return ir_.module();
}

Type IRGenerator::typeOf(const Expr& e) const {
    auto it = exprTypes_.find(&e);
    return it == exprTypes_.end() ? Type{TypeKind::Unknown} : it->second;
}

ll::Value IRGenerator::evalExpr(const ExprPtr& e) {
    e->accept(*this);
    return lastValue_;
}

// 地址通道：左值表达式（赋值左边、& 取引用）要的是地址，不能 load
ll::Value IRGenerator::evalAddr(const ExprPtr& e) {
    if (auto p = std::dynamic_pointer_cast<PathExpr>(e)) {
        auto it = locals_.find(p->name);
        if (it != locals_.end())
            return it->second;   // 局部变量的地址 = let 时登记的栈槽
        // static 全局变量：@name 本身就是地址（赋值的左边只会是变量或 static）
        return ll::Value::global("@" + p->name);
    }
    if (auto p = std::dynamic_pointer_cast<FieldExpr>(e)) {
        ll::Value base = evalExpr(p->object);   // 结构体的"值"=地址
        // 对象类型名（自动解引用：Ref 先剥一层），查字段在声明序里的下标
        Type ot = typeOf(*p->object);
        std::string tyName = ot.kind == TypeKind::Ref ? ot.elem->name : ot.name;
        Symbol* ts = symtab_.lookupType(tyName);
        size_t idx = 0;
        for (; idx < ts->structDef->fields.size(); ++idx)
            if (ts->structDef->fields[idx].name == p->field) break;
        ll::Value ptr = ir_.freshReg();
        ir_.emitGep(ptr, ll::Type::struct_(tyName), base,
                    {ll::Value::imm("0"),
                     ll::Value::imm(std::to_string(idx))});
        return ptr;
    }
    if (auto p = std::dynamic_pointer_cast<IndexExpr>(e)) {
        ll::Value base = evalExpr(p->array);
        ll::Value idx = evalExpr(p->index);
        ll::Value ptr = ir_.freshReg();
        ir_.emitGep(ptr, irtypes::llvmType(typeOf(*p->array)), base,
                    {ll::Value::imm("0"), idx});
        return ptr;
    }
    // UnaryExpr "*"：指针值本身就是地址，直接返回
    if (auto p = std::dynamic_pointer_cast<UnaryExpr>(e))
        return evalExpr(p->operand);
    return ll::Value::reg("");   // 其余表达式不可作左值（语义阶段已拦）
}

// 聚合体逐成员拷贝：dst/src 都是地址。标量 load+store；数组逐元素递归；
// struct 按声明序逐字段递归（字段类型来自 structDef）
void IRGenerator::emitCopyAggregate(ll::Value dst, ll::Value src, const Type& ty) {
    if (ty.kind == TypeKind::Array) {
        ll::Type cont = irtypes::llvmType(ty);
        for (size_t i = 0; i < ty.arrayLen; i++) {
            ll::Value d = ir_.freshReg();
            ll::Value s = ir_.freshReg();
            ir_.emitGep(d, cont, dst,
                        {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
            ir_.emitGep(s, cont, src,
                        {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
            emitCopyAggregate(d, s, *ty.elem);
        }
        return;
    }
    if (ty.kind == TypeKind::Named) {
        Symbol* ts = symtab_.lookupType(ty.name);
        ll::Type cont = ll::Type::struct_(ty.name);
        for (size_t i = 0; i < ts->structDef->fields.size(); i++) {
            ll::Value d = ir_.freshReg();
            ll::Value s = ir_.freshReg();
            ir_.emitGep(d, cont, dst,
                        {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
            ir_.emitGep(s, cont, src,
                        {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
            emitCopyAggregate(d, s, ts->structDef->fields[i].type);
        }
        return;
    }
    ll::Type lt = irtypes::llvmType(ty);
    ll::Value v = ir_.freshReg();
    ir_.emitLoad(v, lt, src);
    ir_.emitStore(lt, v, dst);
}

// ============================================================
// M1 已实现：模块外壳 + struct 类型定义 + 函数外壳
// ============================================================

void IRGenerator::visit(Crate& node) {
    ir_.raw("target triple = \"riscv32-unknown-none-elf\"");
    ir_.raw("");
    // 内建函数声明（builtin.c 提供实现；declare 了不用无害）
    ir_.raw("declare void @printInt(i32)");
    ir_.raw("declare void @printlnInt(i32)");
    ir_.raw("declare i32 @getInt()");
    ir_.raw("declare void @exit(i32)");
    ir_.raw("");
    // struct 类型定义先于函数体输出（字段类型可能引用别的 struct）
    for (auto& item : node.items)
        if (auto s = std::dynamic_pointer_cast<StructDef>(item)) visit(*s);
    ir_.raw("");
    // static 全局变量（有才发，避免多空行）
    bool anyStatic = false;
    for (auto& item : node.items)
        if (auto s = std::dynamic_pointer_cast<StaticDef>(item)) {
            visit(*s);
            anyStatic = true;
        }
    if (anyStatic) ir_.raw("");
    // 函数定义；impl 里的方法也是普通函数（M4 接入）
    for (auto& item : node.items) {
        if (auto f = std::dynamic_pointer_cast<Function>(item)) {
            visit(*f);
        } else if (auto impl = std::dynamic_pointer_cast<ImplBlock>(item)) {
            currentImplType_ = impl->typeName;   // 方法名 mangling 用的类型名
            for (auto& m : impl->methods) visit(*m);
            currentImplType_.clear();
        }
    }
}

void IRGenerator::visit(StructDef& node) {
    // %struct.Name = type { 字段类型按声明顺序 }，字段顺序即内存布局
    std::string line = "%struct." + node.name + " = type {";
    for (size_t i = 0; i < node.fields.size(); ++i) {
        if (i) line += ",";
        line += " " + irtypes::llvmType(node.fields[i].type).str();
    }
    line += " }";
    ir_.raw(line);
}

void IRGenerator::visit(Function& node) {
    locals_.clear();
    loopLabels_.clear();
    loopValueSlots_.clear();
    blockSealed_ = false;
    inMain_ = (node.name == "main");
    currentFnRetType_ = node.returnType ? *node.returnType : Type{TypeKind::Unit};

    // 方法名 mangling（类型名_方法名，与 MethodCallExpr 同一约定）：impl 块
    // 里的函数（currentImplType_ 非空）都 mangle，包括没有 self 的关联函数；
    // isMethod 只决定要不要加 self 参数
    bool isMain = (node.name == "main");
    std::string llvmName = !currentImplType_.empty()
        ? currentImplType_ + "_" + node.name : node.name;
    // 返回聚合体的函数走 sret：LLVM 层 ret void，隐藏第一参数 ptr %sret
    bool retAggregate = currentFnRetType_.kind == TypeKind::Named
                     || currentFnRetType_.kind == TypeKind::Array;
    std::vector<std::pair<ll::Type, std::string>> params;
    if (retAggregate)
        params.emplace_back(ll::Type::ptr(), "sret");
    if (node.isMethod) {
        ll::Type selfTy = node.selfByValue ? ll::Type::struct_(currentImplType_) : ll::Type::ptr();
        params.emplace_back(selfTy, "self");
    }
    for (auto& p : node.params)
        params.emplace_back(irtypes::llvmType(p.type), p.name);
    ll::Type llvmRet = isMain ? ll::Type::i32()
        : retAggregate ? ll::Type::voidT() : irtypes::llvmType(currentFnRetType_);
    // 调用约定：main 对外是 C 入口，必须 ret i32（模拟器把返回值当退出码），
    // 源语言的 main 返回 ()，统一 ret i32 0
    ir_.beginFunction(llvmRet, llvmName, params);
    sretSlot_ = retAggregate ? ll::Value::reg("%sret") : ll::Value::reg("");
    if (node.isMethod) {
        ll::Type selfTy = node.selfByValue ? ll::Type::struct_(currentImplType_) : ll::Type::ptr();
        ll::Value slot = ll::Value::reg("%self.addr");
        ir_.emitAlloca(slot, selfTy);
        ir_.emitStore(selfTy, ll::Value::reg("%self"), slot);
        locals_["self"] = slot;
    }
    // 参数落槽：传入的 SSA 值 %p 不可写，和 let 一样给栈槽，统一走 load/store
    for (auto& p : node.params) {
        ll::Type ty = irtypes::llvmType(p.type);
        ll::Value slot = ll::Value::reg("%" + p.name + ".addr");
        ir_.emitAlloca(slot, ty);
        ir_.emitStore(ty, ll::Value::reg("%" + p.name), slot);
        locals_[p.name] = slot;
    }
    // 函数体：块类型是 Unit 则 ret void，否则尾表达式的值即返回值；
    // body 以 return/break 收尾时块已被终结（blockSealed_），不再补 ret
    visit(*node.body);
    if (!blockSealed_) {
        if (inMain_) {
            ir_.emitRet(ll::Type::i32(), ll::Value::imm("0"));
        } else if (retAggregate) {
            // sret：把尾表达式的聚合体（值=地址）拷贝进隐藏参数槽
            emitCopyAggregate(sretSlot_, lastValue_, currentFnRetType_);
            ir_.emitRetVoid();
        } else if (typeOf(*node.body).kind == TypeKind::Unit) {
            ir_.emitRetVoid();
        } else if (typeOf(*node.body).kind == TypeKind::Never) {
            // 发散函数（如 fn f() -> i32 { loop {}; }）：ret 不可达但要有，
            // 用该类型的零值兜底
            ir_.emitRet(irtypes::llvmType(currentFnRetType_),
                        irtypes::defaultValue(currentFnRetType_));
        } else {
            ir_.emitRet(irtypes::llvmType(currentFnRetType_), lastValue_);
        }
    }
    ir_.endFunction();
    // 旧实现的 blankLine 写在模块区流里：每个函数结尾在模块区多一个空行
    ir_.raw("");
}

// ============================================================
// M2 起：以下是待填的桩，注释是该函数的 lowering 步骤
// ============================================================

// 块：逐句 lowering，再算尾表达式（单独存在 tailExpr 字段，不在 statements
// 里）；尾表达式的值留在 lastValue_ 作为整个块的值。
// return/break/continue 终结当前块后（blockSealed_），剩余语句不可达，直接跳过
void IRGenerator::visit(Block& node) {
    for (auto& stmt : node.statements) {
        if (blockSealed_) break;   // 终结指令后的语句不可达，跳过
        stmt -> accept(*this);
    }
    if (!blockSealed_ && node.tailExpr) evalExpr(node.tailExpr);
}

// let：alloca 栈槽 + store 初值 + 登记 locals_（遮蔽 = 同名新槽覆盖）。
// 聚合体（struct/数组）的"值"= 地址：init 求值得到的是地址，整个拷贝进新槽
// （拷贝而非别名：数组是 Copy 语义，let b = a 后改 b 不能影响 a）
void IRGenerator::visit(LetStmt& node) {
    Type ty = node.type ? *node.type : typeOf(*node.init);
    if (ty.kind == TypeKind::Named || ty.kind == TypeKind::Array) {
        ll::Value slot = ll::Value::reg("%" + node.name + ".addr");
        ir_.emitAlloca(slot, irtypes::llvmType(ty));
        if (node.init) emitCopyAggregate(slot, evalExpr(node.init), ty);
        locals_[node.name] = slot;
        return;
    }
    ll::Value slot = ll::Value::reg("%" + node.name + ".addr");
    ir_.emitAlloca(slot, irtypes::llvmType(ty));
    if (node.init) {
        ll::Value val = evalExpr(node.init);
        ir_.emitStore(irtypes::llvmType(ty), val, slot);
    }
    locals_[node.name] = slot;
}

// 表达式语句：求值后丢弃（块的尾表达式存在 Block::tailExpr，不经这里）
void IRGenerator::visit(ExprStmt& node) {
    evalExpr(node.expr);
}

// 字面量：整数/浮点直接当常量文本；bool → "1"/"0"；char → ASCII 码；
// 字符串登记为全局常量后 gep 取首元素指针
void IRGenerator::visit(Literal& node) {
    if (node.kind == LiteralKind::Int || node.kind == LiteralKind::Float) {
        lastValue_ = ll::Value::imm(node.text);
    }
    if (node.kind == LiteralKind::Bool) {
        if (node.text == "true") lastValue_ = ll::Value::imm("1");
        else if (node.text == "false") lastValue_ = ll::Value::imm("0");
    }
    if (node.kind == LiteralKind::Char) {
        lastValue_ = ll::Value::imm(std::to_string((unsigned char)node.text[1]));
    }
    if (node.kind == LiteralKind::Str) {
        std::string content = node.text.substr(1, node.text.size() - 2);
        ll::Value name = ir_.addString(content);
        ll::Value p = ir_.freshReg();
        int len = content.size() + 1;
        ir_.emitGep(p, ll::Type::array(ll::Type::i8(), len), name,
                    {ll::Value::imm("0"), ll::Value::imm("0")});
        lastValue_ = p;
    }
}

// 名字求值（值通道）：局部变量 → load；const → 编译期常量文本；
// 关联常量 Type::N → ConstDef 上的 constValue。左值场景走 evalAddr，不经这里
void IRGenerator::visit(PathExpr& node) {
    if (locals_.find(node.name) != locals_.end()) {
        ll::Value slot = locals_.find(node.name)->second;
        TypeKind k = typeOf(node).kind;
        // 聚合体（struct/数组）的"值"= 地址：直接给栈槽，不 load
        if (k == TypeKind::Named || k == TypeKind::Array) {
            lastValue_ = slot;
            return;
        }
        ll::Value p = ir_.freshReg();
        ir_.emitLoad(p, irtypes::llvmType(typeOf(node)), slot);
        lastValue_ = p;
        return;
    }
    Symbol* sym = symtab_.lookup(node.name);
    if (sym && sym -> kind == SymbolKind::Const && sym -> hasConstValue) {
        lastValue_ = ll::Value::imm(std::to_string(sym -> constValue));
        return;
    }
    // static 全局变量：@name 本身就是地址，值通道 load 出内容
    if (sym && sym->kind == SymbolKind::Static) {
        ll::Value p = ir_.freshReg();
        ir_.emitLoad(p, irtypes::llvmType(typeOf(node)),
                     ll::Value::global("@" + node.name));
        lastValue_ = p;
        return;
    }
    auto pos = node.name.find("::");
    if (pos != std::string::npos) {
        std::string tyName = node.name.substr(0, pos);
        std::string cName = node.name.substr(pos + 2);
        auto it = symtab_.implConsts.find(tyName);
        if (it != symtab_.implConsts.end()) {
            for (auto& cd : it->second) {
                if (cd->name == cName && cd->hasConstValue) {
                    lastValue_ = ll::Value::imm(std::to_string(cd->constValue));
                    return;
                }
            }
        }
    }
}

void IRGenerator::visit(BinaryExpr& node) {
    // 赋值类先分流：lhs 走 evalAddr（不 load），rhs 走 evalExpr
    if (node.op == "=") {
        ll::Value addr = evalAddr(node.lhs);
        Type lt = typeOf(*node.lhs);
        // 聚合体整体赋值：逐成员拷贝（rhs 求值结果是地址）
        if (lt.kind == TypeKind::Named || lt.kind == TypeKind::Array) {
            emitCopyAggregate(addr, evalExpr(node.rhs), lt);
            return;
        }
        ll::Value val = evalExpr(node.rhs);
        ir_.emitStore(irtypes::llvmType(lt), val, addr);
        // 赋值表达式的值是 ()，不留 lastValue_
        return;
    }
    // 复合赋值：load 旧值 → 运算 → store 回去
    if (node.op == "+=" || node.op == "-=" || node.op == "*="
        || node.op == "/=" || node.op == "%=") {
        ll::Value addr = evalAddr(node.lhs);
        ll::Type ty = irtypes::llvmType(typeOf(*node.lhs));
        bool lSigned = irtypes::isSignedInt(typeOf(*node.lhs));
        ll::Value old = ir_.freshReg();
        ir_.emitLoad(old, ty, addr);
        ll::Value r = evalExpr(node.rhs);
        std::string opc;
        if      (node.op == "+=") opc = "add";
        else if (node.op == "-=") opc = "sub";
        else if (node.op == "*=") opc = "mul";
        else if (node.op == "/=") opc = lSigned ? "sdiv" : "udiv";
        else                      opc = lSigned ? "srem" : "urem";
        ll::Value res = ir_.freshReg();
        ir_.emitBinary(opc, res, ty, old, r);
        ir_.emitStore(ty, res, addr);
        return;
    }

    // 短路逻辑 && / ||：rhs 不能在分流前求值（否则丢失短路语义），
    // 所以必须在上面 l/r 的 evalExpr 之前分流。三标签结构：rhs 块 +
    // 常量出口块 + 汇合块；两条流入边各自 store，汇合块只 load
    if (node.op == "&&" || node.op == "||") {
        bool isAnd = (node.op == "&&");
        ll::Value slot = ir_.freshReg();
        ir_.emitAlloca(slot, ll::Type::i8());
        ll::Value lhs = evalExpr(node.lhs);   // i8（bool 载体）
        ll::Value c = ir_.freshReg();
        ir_.emitIcmp("ne", c, ll::Type::i8(), lhs, ll::Value::imm("0"));
        std::string rhsL = ir_.freshLabel(isAnd ? "and.rhs" : "or.rhs");
        std::string constL = ir_.freshLabel(isAnd ? "and.false" : "or.true");
        std::string endL = ir_.freshLabel(isAnd ? "and.end" : "or.end");
        // &&：左边为假直接进常量出口（store 0）；||：左边为真进常量出口（store 1）
        if (isAnd) ir_.emitBrCond(c, rhsL, constL);
        else ir_.emitBrCond(c, constL, rhsL);
        ir_.label(rhsL);
        blockSealed_ = false;
        ll::Value rhs = evalExpr(node.rhs);
        if (!blockSealed_) {   // rhs 里 return/break 收尾时不补
            ir_.emitStore(ll::Type::i8(), rhs, slot);
            ir_.emitBr(endL);
        }
        ir_.label(constL);
        blockSealed_ = false;
        ir_.emitStore(ll::Type::i8(), ll::Value::imm(isAnd ? "0" : "1"), slot);
        ir_.emitBr(endL);
        ir_.label(endL);
        blockSealed_ = false;
        ll::Value v = ir_.freshReg();
        ir_.emitLoad(v, ll::Type::i8(), slot);
        lastValue_ = v;
        return;
    }

    ll::Value l = evalExpr(node.lhs);
    ll::Value r = evalExpr(node.rhs);
    bool lSigned = irtypes::isSignedInt(typeOf(*node.lhs));
    ll::Type opTy = irtypes::llvmType(typeOf(*node.lhs));  // 操作数类型

    // 算术：+ - * / %
    if (node.op == "+" || node.op == "-" || node.op == "*"
        || node.op == "/" || node.op == "%") {
        std::string opc;
        if (node.op == "+") opc = "add";
        else if (node.op == "-") opc = "sub";
        else if (node.op == "*") opc = "mul";
        else if (node.op == "/") opc = lSigned ? "sdiv" : "udiv";
        else                     opc = lSigned ? "srem" : "urem";
        ll::Value res = ir_.freshReg();
        ir_.emitBinary(opc, res, opTy, l, r);
        lastValue_ = res;
        return;
    }

    // 比较：== != < <= > >=（icmp 结果是 i1，zext 成我们的 bool 载体 i8）
    if (node.op == "==" || node.op == "!=" || node.op == "<"
        || node.op == "<=" || node.op == ">" || node.op == ">=") {
        std::string pred;
        if      (node.op == "==") pred = "eq";
        else if (node.op == "!=") pred = "ne";
        else if (node.op == "<")  pred = lSigned ? "slt" : "ult";
        else if (node.op == "<=") pred = lSigned ? "sle" : "ule";
        else if (node.op == ">")  pred = lSigned ? "sgt" : "ugt";
        else                      pred = lSigned ? "sge" : "uge";
        ll::Value c = ir_.freshReg();
        ir_.emitIcmp(pred, c, opTy, l, r);
        ll::Value z = ir_.freshReg();
        ir_.emitZext(z, ll::Type::i1(), ll::Type::i8(), c);
        lastValue_ = z;
        return;
    }

    // 位运算：& | ^ << >>（bool 上的 & | ^ 同样走这三条，操作数类型 i8）；
    // >> 按有符号性选 ashr/lshr
    if (node.op == "&" || node.op == "|" || node.op == "^"
        || node.op == "<<" || node.op == ">>") {
        std::string opc;
        if      (node.op == "&")  opc = "and";
        else if (node.op == "|")  opc = "or";
        else if (node.op == "^")  opc = "xor";
        else if (node.op == "<<") opc = "shl";
        else                      opc = lSigned ? "ashr" : "lshr";
        ll::Value res = ir_.freshReg();
        ir_.emitBinary(opc, res, opTy, l, r);
        lastValue_ = res;
        return;
    }
}

// 一元运算：
//   "-" → sub ty 0, v（用 0 减操作数）
//   "!" → xor i8 v, 1（bool 载体是 i8，与 1 异或即逻辑非）
//   "&"/"&mut" → 取地址（不产生指令）；  "*" → 对指针值 load
void IRGenerator::visit(UnaryExpr& node) {
    if (node.op == "-") {
        ll::Value v = evalExpr(node.operand);
        ll::Value res = ir_.freshReg();
        ir_.emitBinary("sub", res, irtypes::llvmType(typeOf(node)), ll::Value::imm("0"), v);
        lastValue_ = res;
        return;
    }
    // "!"：bool 载体是 i8，逻辑非 = 与 1 异或
    if (node.op == "!") {
        ll::Value v = evalExpr(node.operand);
        ll::Value res = ir_.freshReg();
        ir_.emitBinary("xor", res, ll::Type::i8(), v, ll::Value::imm("1"));
        lastValue_ = res;
        return;
    }
    // "&" / "&mut"：取地址，不产生任何指令——地址本身就是引用的值
    if (node.op == "&" || node.op == "&mut") {
        lastValue_ = evalAddr(node.operand);
    }
    // "*"：解引用 = 对指针值 load（作左值时不走这里，走 evalAddr 的 "*" 分支）
    if (node.op == "*") {
        ll::Value ptr = evalExpr(node.operand);
        ll::Value res = ir_.freshReg();
        ir_.emitLoad(res, irtypes::llvmType(typeOf(node)), ptr);
        lastValue_ = res;
    }
}

// as 类型转换：四种整型在 LLVM 都是 i32（有符号性只在指令上），互转是空
// 操作直接透传；只有跨宽度的 bool/char（i8）↔ 整型（i32）要 zext/trunc
void IRGenerator::visit(CastExpr& node) {
    ll::Value v = evalExpr(node.expr);
    auto src = irtypes::llvmType(typeOf(*node.expr)).kind;
    auto dst = irtypes::llvmType(node.target).kind;
    if (src == ll::Type::Kind::I8 && dst == ll::Type::Kind::I32) {
        ll::Value res = ir_.freshReg();
        ir_.emitZext(res, ll::Type::i8(), ll::Type::i32(), v);
        lastValue_ = res;
        return;
    }
    if (src == ll::Type::Kind::I32 && dst == ll::Type::Kind::I8) {
        ll::Value res = ir_.freshReg();
        ir_.emitTrunc(res, ll::Type::i32(), ll::Type::i8(), v);
        lastValue_ = res;
        return;
    }
    lastValue_ = v;   // 同宽（含整型互转）和子集用不到的 as：透传
}

// 函数调用：callee 是 PathExpr；内建名字映射到 builtin.c 的函数；
// void 返回的 call 不接 %r =
void IRGenerator::visit(CallExpr& node) {
    // ① callee 取函数名（callee 必是 PathExpr）
    auto calleePath = std::dynamic_pointer_cast<PathExpr>(node.callee);
    std::string origName = calleePath->name;

    // ② 内建名字映射：源语言的别名统一映射到 builtin.c 的函数名
    static const std::unordered_map<std::string, std::string> builtinMap = {
        {"printlnInt", "printlnInt"}, {"println_i32", "printlnInt"},
        {"printInt",   "printInt"},   {"print_i32",   "printInt"},
        {"getInt",     "getInt"},     {"get_i32",     "getInt"},
        {"exit",       "exit"},
    };
    std::string llvmName = origName;
    auto it = builtinMap.find(origName);
    if (it != builtinMap.end()) llvmName = it->second;

    // 关联函数调用 Point::new(...)：路径带 "::" → mangling 成 类型名_函数名，
    // 返回类型从 implMethods 里查（符号表里没有 "Point::new" 这个名字）
    std::shared_ptr<Function> assocFn;
    auto pos = origName.find("::");
    if (pos != std::string::npos) {
        std::string tyName = origName.substr(0, pos);
        std::string fnName = origName.substr(pos + 2);
        llvmName = tyName + "_" + fnName;
        for (auto& m : symtab_.implMethods[tyName])
            if (m->name == fnName) { assocFn = m; break; }
    }

    // ③ 逐参数求值，收集 (类型, 值) 对
    std::vector<std::pair<ll::Type, ll::Value>> args;
    for (auto& arg : node.args) {
        ll::Value v = evalExpr(arg);
        args.emplace_back(irtypes::llvmType(typeOf(*arg)), v);
    }

    // ④ 发 call：void 返回不接 %r =；否则结果进新寄存器
    // TODO(加固)：参数类型是 i8 而目标函数要 i32 时（printlnInt(bool)），先 zext
    // 返回类型：关联函数从 implMethods 查到的 Function 取，普通函数查符号表
    Type retTy{TypeKind::Unit};
    if (assocFn) {
        if (assocFn->returnType) retTy = *assocFn->returnType;
    } else {
        Symbol* sym = symtab_.lookup(origName);
        retTy = sym->type;
    }
    // sret：返回聚合体时调用方 alloca 槽位，地址作第一个实参，call 变 void；
    // 聚合体的"值"= 这个槽的地址
    if (retTy.kind == TypeKind::Named || retTy.kind == TypeKind::Array) {
        ll::Value slot = ir_.freshReg();
        ir_.emitAlloca(slot, irtypes::llvmType(retTy));
        args.insert(args.begin(), {ll::Type::ptr(), slot});
        ir_.emitCallVoid(llvmName, args);
        lastValue_ = slot;
        return;
    }
    if (retTy.kind == TypeKind::Unit) {
        ir_.emitCallVoid(llvmName, args);
    } else {
        ll::Value r = ir_.freshReg();
        ir_.emitCall(r, irtypes::llvmType(retTy), llvmName, args);
        lastValue_ = r;
    }
}

// ---- M3：控制流 ----

// if：条件 i8 → icmp ne 0 转 i1 → 条件跳转；then/else 各自成块、末尾跳进汇合点
// 带值的 if（let x = if ...）：入口 alloca 槽，两分支 store，汇合点 load
void IRGenerator::visit(IfExpr& node) {
    ll::Value c = evalExpr(node.cond);
    ll::Value c1 = ir_.freshReg();
    ir_.emitIcmp("ne", c1, ll::Type::i8(), c, ll::Value::imm("0"));
    std::string thenL = ir_.freshLabel("if.then");
    std::string elseL = ir_.freshLabel("if.else");
    std::string endL = ir_.freshLabel("if.end");
    bool hasElse = node.elseBlock != nullptr;
    // 块的类型是 Unit/Never 时这个 if 不产值（Never = 分支以 return 等收尾）
    bool hasValue = typeOf(node).kind != TypeKind::Unit && typeOf(node).kind != TypeKind::Never;
    ll::Value slot = ll::Value::reg("");
    ll::Type valTy = irtypes::llvmType(typeOf(node));
    if (hasValue) {
        slot = ir_.freshReg();
        ir_.emitAlloca(slot, valTy);
    }
    ir_.emitBrCond(c1, thenL, hasElse ? elseL : endL);
    ir_.label(thenL);
    blockSealed_ = false;          // 每个新标签块都解封
    visit(*node.thenBlock);
    // 分支以 return/break 收尾（块被封死）时不 store 也不补 br——
    // 该块已经有终结指令
    if (!blockSealed_) {
        if (hasValue)
            ir_.emitStore(valTy, lastValue_, slot);
        ir_.emitBr(endL);
    }
    if (hasElse) {
        ir_.label(elseL);
        blockSealed_ = false;
        visit(*node.elseBlock);
        if (!blockSealed_) {
            if (hasValue)
                ir_.emitStore(valTy, lastValue_, slot);
            ir_.emitBr(endL);
        }
    }
    ir_.label(endL);
    blockSealed_ = false;
    if (hasValue) {
        ll::Value v = ir_.freshReg();
        ir_.emitLoad(v, valTy, slot);
        lastValue_ = v;
    }
}

// while：三标签结构 cond → body → end。cond 块算条件（i8 转 i1）条件跳
// body/end；body 块压 loopLabels_（continue→cond、break→end）后翻译，
// 正常走完才补回 cond 的回边；while 恒为 ()
void IRGenerator::visit(WhileExpr& node) {
    std::string condL = ir_.freshLabel("while.cond");
    std::string bodyL = ir_.freshLabel("while.body");
    std::string endL = ir_.freshLabel("while.end");
    ir_.emitBr(condL);
    ir_.label(condL);
    ll::Value c = evalExpr(node.cond);
    ll::Value c1 = ir_.freshReg();
    ir_.emitIcmp("ne", c1, ll::Type::i8(), c, ll::Value::imm("0"));
    ir_.emitBrCond(c1, bodyL, endL);
    ir_.label(bodyL);
    blockSealed_ = false;
    loopLabels_.push_back({condL, endL});
    loopValueSlots_.push_back(ll::Value::reg(""));   // while 不产值，压占位
    visit(*node.body);
    loopLabels_.pop_back();
    loopValueSlots_.pop_back();
    if (!blockSealed_) {
        ir_.emitBr(condL);
    }
    ir_.label(endL);
    blockSealed_ = false;
}

// loop：无条件块的无限循环，body 尾回边跳 body 头（continue 目标也是 body
// 头）。产值的 loop（类型 = 各 break 值的 LUB，非 ()/Never）在入口 alloca
// 结果槽并压 loopValueSlots_，带值 break 往槽里 store，汇合块 load 出值
void IRGenerator::visit(LoopExpr& node) {
    std::string bodyL = ir_.freshLabel("loop.body");
    std::string endL = ir_.freshLabel("loop.end");
    bool hasValue = typeOf(node).kind != TypeKind::Unit && typeOf(node).kind != TypeKind::Never;
    ll::Value slot = ll::Value::reg("");   // 不产值时占位
    if (hasValue) {
        slot = ir_.freshReg();
        ir_.emitAlloca(slot, irtypes::llvmType(typeOf(node)));
    }
    loopValueSlots_.push_back(slot);
    ir_.emitBr(bodyL);
    ir_.label(bodyL);
    blockSealed_ = false;
    loopLabels_.push_back({bodyL, endL});
    visit(*node.body);
    loopLabels_.pop_back();
    loopValueSlots_.pop_back();
    if (!blockSealed_) {
        ir_.emitBr(bodyL);   // 回边跳 body 头（loop 没有 cond 块）
    }
    ir_.label(endL);
    blockSealed_ = false;
    if (hasValue) {
        ll::Value v = ir_.freshReg();
        ir_.emitLoad(v, irtypes::llvmType(typeOf(node)), slot);
        lastValue_ = v;
    }
}

// return：有值 ret <返回类型> <值>；无值时 main 按 C 入口约定 ret i32 0，
// 其他函数 ret void。ret 终结当前块（置 blockSealed_），return 类型是
// Never，不留 lastValue_
void IRGenerator::visit(ReturnExpr& node) {
    if (node.value) {
        ll::Value v = evalExpr(node.value);
        if (currentFnRetType_.kind == TypeKind::Named
            || currentFnRetType_.kind == TypeKind::Array) {
            // sret：聚合体（值=地址）拷贝进隐藏参数槽，ret void
            emitCopyAggregate(sretSlot_, v, currentFnRetType_);
            ir_.emitRetVoid();
        } else {
            ir_.emitRet(irtypes::llvmType(currentFnRetType_), v);
        }
    } else if (inMain_) {
        // main 的 C 入口约定：裸 return; 也 ret i32 0
        ir_.emitRet(ll::Type::i32(), ll::Value::imm("0"));
    } else {
        ir_.emitRetVoid();
    }
    blockSealed_ = true;
}

// break：带值时先把值 store 进本层 loop 的结果槽（loopValueSlots_ 栈顶），
// 再无条件跳到本层循环出口（loopLabels_ 栈顶 second）；终结当前块
void IRGenerator::visit(BreakExpr& node) {
    if (node.value) {
        ll::Value v = evalExpr(node.value);
        ir_.emitStore(irtypes::llvmType(typeOf(*node.value)), v, loopValueSlots_.back());
    }
    ir_.emitBr(loopLabels_.back().second);
    blockSealed_ = true;
}

// continue：无条件跳到本层循环的循环头（loopLabels_ 栈顶 first——while
// 是 cond 块，loop 是 body 块）；终结当前块
void IRGenerator::visit(ContinueExpr& node) {
    ir_.emitBr(loopLabels_.back().first);
    blockSealed_ = true;
}

// ---- M4：方法 ----

void IRGenerator::visit(ImplBlock& node) {
    // visit(Crate) 已直接遍历 methods，这里不会走到；留空
    (void)node;
}

// 方法调用 = 普通函数调用 + self 作第一个参数；函数名 mangling 成
// 类型名_方法名（与 visit(Function) 定义处同一约定）。
// 接收者类型名从语义类型取（Ref 剥一层），方法在 implMethods 里按名字查；
// self 实参：&self/&mut self 传地址（左值走 evalAddr，临时值本来就是地址），
// 按值 self 传整个结构体
// TODO(如遇到)：内建方法（Vec 的 push/len 等）不在 implMethods 里，
//   要按 (类型名, 方法名) 查内建表映射到 builtin.c 的函数
void IRGenerator::visit(MethodCallExpr& node) {
    // ① 接收者的语义类型（ast 的 Type，不是 ll 类型！）：Ref 剥一层取类型名
    Type rt = typeOf(*node.receiver);
    std::string typeName = (rt.kind == TypeKind::Ref) ? rt.elem->name : rt.name;
    // ② 在 implMethods[类型名] 里按名字找方法
    std::shared_ptr<Function> method;
    for (auto& m : symtab_.implMethods[typeName])
        if (m->name == node.method) {
            method = m;
            break;
        }
    std::vector<std::pair<ll::Type, ll::Value>> args;
    // ③ self 实参（第一个参数）
    if (!method->selfByValue) {
        // &self / &mut self：传地址。左值接收者走 evalAddr；临时值
        // （Point::new().norm()）不是左值，但聚合体的"值"本来就是地址
        bool isLvalue = std::dynamic_pointer_cast<PathExpr>(node.receiver)
                     || std::dynamic_pointer_cast<FieldExpr>(node.receiver)
                     || std::dynamic_pointer_cast<IndexExpr>(node.receiver)
                     || std::dynamic_pointer_cast<UnaryExpr>(node.receiver);
        args.push_back({ll::Type::ptr(),
                        isLvalue ? evalAddr(node.receiver)
                                 : evalExpr(node.receiver)});
    } else {
        // 按值 self：整个结构体传值
        args.push_back({irtypes::llvmType(rt), evalExpr(node.receiver)});
    }
    for (auto& arg : node.args) {
        ll::Value v = evalExpr(arg);
        args.emplace_back(irtypes::llvmType(typeOf(*arg)), v);
    }
    std::string llvmName = typeName + "_" + node.method;
    // ⑤ 发 call：返回聚合体走 sret（alloca 槽位作第一实参，值=槽地址）
    Type retTy = method->returnType ? *method->returnType : Type{TypeKind::Unit};
    if (retTy.kind == TypeKind::Named || retTy.kind == TypeKind::Array) {
        ll::Value slot = ir_.freshReg();
        ir_.emitAlloca(slot, irtypes::llvmType(retTy));
        args.insert(args.begin(), {ll::Type::ptr(), slot});
        ir_.emitCallVoid(llvmName, args);
        lastValue_ = slot;
        return;
    }
    bool isVoid = retTy.kind == TypeKind::Unit;
    if (isVoid) {
        ir_.emitCallVoid(llvmName, args);
    } else {
        ll::Value r = ir_.freshReg();
        ir_.emitCall(r, irtypes::llvmType(retTy), llvmName, args);
        lastValue_ = r;
    }
}

// ---- M5：复合类型 ----

// 结构体字面量 Point { x: 1, y: 2 }：alloca 整个结构体的槽位，
// 逐字段 GEP（下标 = 声明序）+ store 填初值；地址即值
void IRGenerator::visit(StructLiteralExpr& node) {
    ll::Value slot = ir_.freshReg();
    ir_.emitAlloca(slot, ll::Type::struct_(node.name));
    // 字段下标表：structDef->fields 的声明顺序就是内存布局顺序
    Symbol* ts = symtab_.lookupType(node.name);
    for (auto& field : node.fields) {
        for (size_t i = 0; i < ts->structDef->fields.size(); i++) {
            if (field.first == ts->structDef->fields[i].name) {
                ll::Value v = evalExpr(field.second);
                ll::Value p = ir_.freshReg();
                ir_.emitGep(p, ll::Type::struct_(node.name), slot,
                            {ll::Value::imm("0"),
                             ll::Value::imm(std::to_string(i))});
                ir_.emitStore(irtypes::llvmType(ts->structDef->fields[i].type),
                              v, p);
                break;
            }
        }
    }
    lastValue_ = slot;
}

// 字段访问的值通道（p.x 当右值用）：evalAddr 的逻辑 + 最后一步 load；
// 字段本身是聚合体时按约定返回地址（不 load）。
// 左值通道（p.x = v、&p.x）走 evalAddr 的 FieldExpr 分支
void IRGenerator::visit(FieldExpr& node) {
    ll::Value base = evalExpr(node.object);   // 结构体的"值"=地址
    Type rt = typeOf(*node.object);
    std::string typeName = (rt.kind == TypeKind::Ref) ? rt.elem->name : rt.name;
    Symbol* ts = symtab_.lookupType(typeName);
    size_t index = 0;
    for (; index < ts->structDef->fields.size(); index++)
        if (ts->structDef->fields[index].name == node.field) break;
    ll::Value p = ir_.freshReg();
    ir_.emitGep(p, ll::Type::struct_(typeName), base,
                {ll::Value::imm("0"), ll::Value::imm(std::to_string(index))});
    TypeKind k = typeOf(node).kind;
    if (k == TypeKind::Named || k == TypeKind::Array) {
        lastValue_ = p;   // 聚合体字段的"值"= 地址
        return;
    }
    ll::Value res = ir_.freshReg();
    ir_.emitLoad(res, irtypes::llvmType(typeOf(node)), p);
    lastValue_ = res;
}

// 数组字面量 [1, 2, 3] / [0; 5]：alloca 整个数组的槽位，逐元素 GEP + store；
// 重复形式 [v; N] 的 v 只求值一次，N 取语义阶段定好的 arrayLen；
// 元素是聚合体时逐成员拷贝（值 = 地址，不能直接 store）
void IRGenerator::visit(ArrayExpr& node) {
    Type arrTy = typeOf(node);
    ll::Type contTy = irtypes::llvmType(arrTy);   // Array(n, 元素类型)
    ll::Value slot = ir_.freshReg();
    ir_.emitAlloca(slot, contTy);
    bool elemAggregate = arrTy.elem->kind == TypeKind::Named
                      || arrTy.elem->kind == TypeKind::Array;
    if (!node.repeatValue) {
        // 列举形式
        for (size_t i = 0; i < node.elements.size(); i++) {
            ll::Value p = ir_.freshReg();
            ir_.emitGep(p, contTy, slot,
                        {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
            if (elemAggregate) {
                emitCopyAggregate(p, evalExpr(node.elements[i]), *arrTy.elem);
            } else {
                ll::Value v = evalExpr(node.elements[i]);
                ir_.emitStore(irtypes::llvmType(typeOf(*node.elements[i])), v, p);
            }
        }
    } else {
        // 重复形式 [v; N]：v 只算一次
        if (elemAggregate) {
            ll::Value src = evalExpr(node.repeatValue);
            for (size_t i = 0; i < arrTy.arrayLen; i++) {
                ll::Value p = ir_.freshReg();
                ir_.emitGep(p, contTy, slot,
                            {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
                emitCopyAggregate(p, src, *arrTy.elem);
            }
        } else {
            ll::Value v = evalExpr(node.repeatValue);
            ll::Type elemTy = irtypes::llvmType(*arrTy.elem);
            for (size_t i = 0; i < arrTy.arrayLen; i++) {
                ll::Value p = ir_.freshReg();
                ir_.emitGep(p, contTy, slot,
                            {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
                ir_.emitStore(elemTy, v, p);
            }
        }
    }
    lastValue_ = slot;
}

// 下标访问的值通道（a[i] 当右值用）：gep 算元素地址 + load；
// 元素本身是聚合体时按约定返回地址（不 load），二维数组 a[i][j] 赖此嵌套。
// 左值通道（a[i] = v）走 evalAddr 的 IndexExpr 分支
void IRGenerator::visit(IndexExpr& node) {
    ll::Value base = evalExpr(node.array);
    ll::Value idx = evalExpr(node.index);
    ll::Type contTy = irtypes::llvmType(typeOf(*node.array));
    ll::Value p = ir_.freshReg();
    ir_.emitGep(p, contTy, base, {ll::Value::imm("0"), idx});
    TypeKind k = typeOf(node).kind;
    if (k == TypeKind::Named || k == TypeKind::Array) {
        lastValue_ = p;   // 聚合体元素的"值"= 地址
        return;
    }
    ll::Value res = ir_.freshReg();
    ir_.emitLoad(res, irtypes::llvmType(typeOf(node)), p);
    lastValue_ = res;
}

// ---- 不 codegen 的条目 ----

void IRGenerator::visit(ConstDef& node) {
    // 常量没有运行时代码：值在编译期（ConstEvaluator）已算好，
    // 使用处（PathExpr）直接内联常量文本
    (void)node;
}

// static 全局静态量：模块区发全局变量，初值是编译期常量
// （ConstEvaluator 已算好，存在符号表的 Symbol 上）；
// 全局名 @name 本身就是地址——值通道（PathExpr）load，左值通道直接用
void IRGenerator::visit(StaticDef& node) {
    Symbol* sym = symtab_.lookup(node.name);
    long long init = (sym && sym->hasConstValue) ? sym->constValue : 0;
    ir_.raw("@" + node.name + " = global "
            + irtypes::llvmType(node.type).str() + " "
            + std::to_string(init));
}

void IRGenerator::visit(MacroStmt& node) {
    // TODO(M2 如遇到)：println!(x) 若出现在语句位置，等价于 printlnInt 调用
    (void)node;
}

void IRGenerator::visit(ItemStmt& node) {
    // 块内嵌套的 fn/const：函数提升到处在两遍扫描阶段处理，const 无代码
    (void)node;
}

// enum/match 课程不要求（助教明确），死桩
void IRGenerator::visit(EnumDef& node) { (void)node; }
void IRGenerator::visit(MatchExpr& node) { (void)node; }
