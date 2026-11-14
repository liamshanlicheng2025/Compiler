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
    if (auto p = std::dynamic_pointer_cast<PathExpr>(e))
        return locals_.at(p->name);   // 变量的地址 = let 时登记的栈槽
    // TODO(M5) 另外三种左值：
    //   FieldExpr（p.x）：base = evalExpr(object)（结构体"值"=地址），
    //     查字段下标（lookupType(类型名)->structDef->fields），
    //     p = freshReg(); emitGep(p, struct 类型, base, {imm("0"), imm(下标)});
    //     返回 p（不 load）
    //   IndexExpr（a[i]）：base = evalExpr(array)，idx = evalExpr(index)，
    //     p = freshReg(); emitGep(p, 数组类型, base, {imm("0"), idx});
    //     返回 p（不 load）
    //   UnaryExpr "*"：evalExpr(node.operand)——指针值本身就是地址，直接返回
    return ll::Value::reg("");
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
    // 函数定义；impl 里的方法也是普通函数（M4 接入）
    for (auto& item : node.items) {
        if (auto f = std::dynamic_pointer_cast<Function>(item)) {
            visit(*f);
        } else if (auto impl = std::dynamic_pointer_cast<ImplBlock>(item)) {
            for (auto& m : impl->methods) visit(*m);
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
    currentFnRetType_ =
        node.returnType ? *node.returnType : Type{TypeKind::Unit};

    // TODO(M4)：方法名要 mangling，否则两个 impl 里的同名方法会撞车。
    //   约定：isMethod 时 LLVM 名 = 类型名_方法名（Point::new → @Point_new）。
    //   类型名来源：加成员 std::string currentImplType_；visit(Crate) 遍历
    //   ImplBlock 的方法前先 currentImplType_ = impl->typeName，遍历完清空；
    //   这里 isMethod 时拼 currentImplType_ + "_" + node.name。
    //   注意 MethodCallExpr 里要按同一约定拼，两处必须一起改
    // 调用约定：main 对外是 C 入口，必须 ret i32（模拟器把返回值当退出码），
    // 源语言的 main 返回 ()，统一 ret i32 0
    bool isMain = (node.name == "main");
    std::vector<std::pair<ll::Type, std::string>> params;
    for (auto& p : node.params)
        params.emplace_back(irtypes::llvmType(p.type), p.name);
    // TODO(M4)：isMethod 时第一个参数是 self（&self → ptr，按值 self → %struct.X）
    ir_.beginFunction(isMain ? ll::Type::i32() : irtypes::llvmType(currentFnRetType_),
                      node.name, params);

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

// let：alloca 栈槽 + store 初值 + 登记 locals_（遮蔽 = 同名新槽覆盖）
void IRGenerator::visit(LetStmt& node) {
    Type ty = node.type ? *node.type : typeOf(*node.init);
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
        ll::Value p = ir_.freshReg();
        ir_.emitLoad(p, irtypes::llvmType(typeOf(node)),
                     locals_.find(node.name)->second);
        lastValue_ = p;
        return;
    }
    Symbol* sym = symtab_.lookup(node.name);
    if (sym && sym -> kind == SymbolKind::Const && sym -> hasConstValue) {
        lastValue_ = ll::Value::imm(std::to_string(sym -> constValue));
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
        ll::Value val = evalExpr(node.rhs);
        ll::Type ty = irtypes::llvmType(typeOf(*node.lhs));
        ir_.emitStore(ty, val, addr);
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
void IRGenerator::visit(UnaryExpr& node) {
    // TODO(M5)："&" / "&mut" → lastValue_ = evalAddr(node.operand)
    //   （地址本身就是引用的值，不产生指令）；
    //   "*" → ptr = evalExpr(node.operand)（指针值，不是地址），
    //   res = freshReg(); emitLoad(res, llvmType(typeOf(node)), ptr)。
    //   作左值时（*p = x 左边）不走这里，走 evalAddr 的 "*" 分支
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
    // "&" / "&mut"（M5）：取地址，不产生任何指令——
    //   lastValue_ = evalAddr(node.operand)（地址本身就是引用的值）
    //
    // "*"（M5）：解引用 = 对指针值 load：
    //   ptr = evalExpr(node.operand)（指针值，不是地址！）
    //   res = ir_.freshReg();
    //   ir_.emitLoad(res, irtypes::llvmType(typeOf(node)), ptr);
    //   lastValue_ = res
    //   注意作左值时（*p = x 的左边）不走这里，走 evalAddr 的 "*" 分支
}

void IRGenerator::visit(CastExpr& node) {
    // TODO：as 类型转换。我们的类型在 LLVM 层的宽度只有两档：i32（四种整型）
    //   和 i8（bool/char），所以只有三种情况：
    //   1. v = evalExpr(node.expr)；
    //      src = irtypes::llvmType(typeOf(*node.expr)).kind；
    //      dst = irtypes::llvmType(node.target).kind
    //   2. src == dst（四种整型互转全是 i32→i32）：空操作，lastValue_ = v
    //      直接透传——有符号性只在指令上，不在类型上，所以 u32/i32 互转
    //      什么都不用做
    //   3. i8→i32（bool/char as 整型）：res = ir_.freshReg();
    //      ir_.emitZext(res, ll::Type::i8(), ll::Type::i32(), v);
    //      lastValue_ = res
    //   4. i32→i8（整型 as char/bool）：trunc 指令已经加好：
    //      ir_.emitTrunc(res, ll::Type::i32(), ll::Type::i8(), v);
    (void)node;
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

    // ③ 逐参数求值，收集 (类型, 值) 对
    std::vector<std::pair<ll::Type, ll::Value>> args;
    for (auto& arg : node.args) {
        ll::Value v = evalExpr(arg);
        args.emplace_back(irtypes::llvmType(typeOf(*arg)), v);
    }

    // ④ 发 call：void 返回不接 %r =；否则结果进新寄存器
    // TODO(加固)：参数类型是 i8 而目标函数要 i32 时（printlnInt(bool)），先 zext
    Symbol* sym = symtab_.lookup(origName);
    if (sym->type.kind == TypeKind::Unit) {
        ir_.emitCallVoid(llvmName, args);
    } else {
        ll::Value r = ir_.freshReg();
        ir_.emitCall(r, irtypes::llvmType(sym->type), llvmName, args);
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
        ir_.emitRet(irtypes::llvmType(currentFnRetType_), v);
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

void IRGenerator::visit(MethodCallExpr& node) {
    // TODO(M4/M5)：obj.method(args...)
    //   1. 接收者类型名：rt = typeOf(*node.receiver)；
    //      rt.kind == Named 则类型名 = rt.name；是 Ref 则剥一层（*rt.elem）
    //      再取 name（方法按值类型注册，引用只是 receiver 的形态）
    //   2. 查方法：遍历 symtab_.implMethods[类型名]，找 m->name == node.method
    //      的 Function，拿到返回类型和 self 形态（m->selfByValue / selfMut）
    //   3. self 实参（LLVM 参数表的第一个）：
    //      - &self / &mut self：evalAddr(node.receiver)——地址即引用值
    //      - 按值 self：结构体按值传参会 load 整个结构体（%struct.X byval），
    //        测试点里少见，先按 evalExpr 透传、遇到再处理
    //      - 接收者是临时值（如 Point::new().norm()）时 evalAddr 不支持：
    //        先 evalExpr 把结构体地址拿到（本来就是地址），直接用
    //   4. 其余参数：照 visit(CallExpr) 第 ③ 步，逐参数 evalExpr 收集
    //      (irtypes::llvmType(typeOf(*arg)), v) 对
    //   5. 发 call：函数名按 mangling 约定 类型名_方法名（与 visit(Function)
    //      的 M4 注释对应，两处一起改）；m->returnType 是 Unit 用 emitCallVoid，
    //      否则 emitCall + lastValue_ = 结果
    //   内建方法特例：Vec 的 push/remove/len、Box::new、字符串方法等不在
    //   implMethods 里，要在第 2 步之前先按 (类型名, 方法名) 查内建表，
    //   映射到 builtin.c 的函数（如 vec_push），遇到 rx 测试点再补
    (void)node;
}

// ---- M5：复合类型 ----

void IRGenerator::visit(StructLiteralExpr& node) {
    // TODO(M5)：Point { x: 1, y: 2 }
    //   约定：聚合体（struct/数组）的"值"统一用它的地址（ptr）表示
    //   1. slot = ir_.freshReg();
    //      ir_.emitAlloca(slot, ll::Type::struct_(node.name));
    //   2. 字段下标表：Symbol* ts = symtab_.lookupType(node.name);
    //      ts->structDef->fields 的声明顺序就是内存布局顺序
    //   3. 逐字段（node.fields 是 (字段名, 初值) 对）：
    //        在 fields 里查字段名得下标 i；
    //        v = evalExpr(初值)；
    //        p = ir_.freshReg();
    //        ir_.emitGep(p, ll::Type::struct_(node.name), slot,
    //                    {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
    //        ir_.emitStore(irtypes::llvmType(字段类型), v, p);
    //   4. lastValue_ = slot;    // 地址即值
    (void)node;
}

void IRGenerator::visit(FieldExpr& node) {
    // TODO(M5)：值通道（p.x 当右值用）
    //   1. base = evalExpr(node.object);          // 结构体的"值"=地址
    //   2. 字段下标：typeOf(*node.object) 取类型名（Ref 先剥一层），
    //      symtab_.lookupType(类型名)->structDef->fields 查 node.field 的下标 i
    //   3. p = ir_.freshReg();
    //      ir_.emitGep(p, ll::Type::struct_(类型名), base,
    //                  {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
    //   4. res = ir_.freshReg();
    //      ir_.emitLoad(res, irtypes::llvmType(typeOf(node)), p);
    //      lastValue_ = res;
    //   左值通道（p.x = v、&p.x）不走这里——在 evalAddr 的 FieldExpr 分支，
    //   只做 1-3 步返回 p（不 load）
    (void)node;
}

void IRGenerator::visit(ArrayExpr& node) {
    // TODO(M5)：[1, 2, 3] 或 [0; 5]；数组的"值"= 地址（同 struct 约定）
    //   1. contTy = irtypes::llvmType(typeOf(node));   // Array(n, 元素类型)
    //      slot = ir_.freshReg(); ir_.emitAlloca(slot, contTy);
    //   2. 列举形式（node.repeatValue 为空指针）：
    //      对 node.elements 里第 i 个 e：
    //        v = evalExpr(e); p = ir_.freshReg();
    //        ir_.emitGep(p, contTy, slot,
    //                    {ll::Value::imm("0"), ll::Value::imm(std::to_string(i))});
    //        ir_.emitStore(元素类型, v, p);
    //   3. 重复形式 [v; N]：v = evalExpr(node.repeatValue)（只算一次！）；
    //      N 是编译期常量（语义阶段已查过）；循环 N 次发 gep + store。
    //      N 很大时展开太长再考虑改运行时循环，测试点规模内直接展开
    //   4. lastValue_ = slot;
    (void)node;
}

void IRGenerator::visit(IndexExpr& node) {
    // TODO(M5)：值通道（a[i] 当右值用）
    //   1. base = evalExpr(node.array);              // 数组的"值"=地址
    //   2. idx = evalExpr(node.index);               // i32
    //   3. contTy = irtypes::llvmType(typeOf(*node.array));
    //      p = ir_.freshReg();
    //      ir_.emitGep(p, contTy, base, {ll::Value::imm("0"), idx});
    //   4. res = ir_.freshReg();
    //      ir_.emitLoad(res, irtypes::llvmType(typeOf(node)), p);
    //      lastValue_ = res;
    //   左值通道（a[i] = v）在 evalAddr 的 IndexExpr 分支：只做 1-3 步
    (void)node;
}

// ---- 不 codegen 的条目 ----

void IRGenerator::visit(ConstDef& node) {
    // 常量没有运行时代码：值在编译期（ConstEvaluator）已算好，
    // 使用处（PathExpr）直接内联常量文本
    (void)node;
}

void IRGenerator::visit(StaticDef& node) {
    // TODO(M5 后期，测试点少)：static COUNT: i32 = 0;
    //   1. 模块区发全局变量（raw 顶格行）：
    //        ir_.raw("@" + node.name + " = global "
    //                + irtypes::llvmType(node.type).str() + " " + 初值文本);
    //      初值文本：符号表里该 static 的 Symbol 上有 hasConstValue/constValue
    //      （ConstEvaluator 已算好），to_string 即可
    //   2. 使用处配合：PathExpr 查到 SymbolKind::Static 的符号时，
    //      全局名 @name 本身就是地址——值通道 emitLoad(res, ty,
    //      ll::Value::global("@" + name))；左值通道直接用 global 当地址
    (void)node;
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
