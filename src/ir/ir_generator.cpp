#include "ir/ir_generator.h"

#include <string>

#include "type_map.h"
#include "semantic/symbol_table.h"

IRGenerator::IRGenerator(SymbolTable& symtab,
                         const std::unordered_map<const Expr*, Type>& exprTypes)
    : symtab_(symtab), exprTypes_(exprTypes) {}

std::string IRGenerator::run(const CratePtr& crate) {
    visit(*crate);
    return ir_.str();
}

Type IRGenerator::typeOf(const Expr& e) const {
    auto it = exprTypes_.find(&e);
    return it == exprTypes_.end() ? Type{TypeKind::Unknown} : it->second;
}

std::string IRGenerator::evalExpr(const ExprPtr& e) {
    e->accept(*this);
    return lastValue_;
}

// 地址通道：左值表达式（赋值左边、& 取引用）要的是地址，不能 load
std::string IRGenerator::evalAddr(const ExprPtr& e) {
    if (auto p = std::dynamic_pointer_cast<PathExpr>(e))
        return locals_.at(p->name);   // 变量的地址 = let 时登记的栈槽
    // TODO(M5)：FieldExpr → gep 字段；IndexExpr → gep 下标；"*" → evalExpr（指针值即地址）
    return "";
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
        line += " " + irtypes::llvmType(node.fields[i].type);
    }
    line += " }";
    ir_.raw(line);
}

void IRGenerator::visit(Function& node) {
    locals_.clear();
    loopLabels_.clear();
    currentFnRetType_ =
        node.returnType ? *node.returnType : Type{TypeKind::Unit};

    // TODO(M4)：方法名要 mangling（如 Point::new → @_Point_new 之类），
    //   否则两个 impl 里的同名方法会撞车
    // 调用约定：main 对外是 C 入口，必须 ret i32（模拟器把返回值当退出码），
    // 源语言的 main 返回 ()，统一 ret i32 0
    bool isMain = (node.name == "main");
    std::vector<std::pair<std::string, std::string>> params;
    for (auto& p : node.params)
        params.emplace_back(irtypes::llvmType(p.type), p.name);
    // TODO(M4)：isMethod 时第一个参数是 self（&self → ptr，按值 self → %struct.X）
    ir_.beginFunction(isMain ? "i32" : irtypes::llvmType(currentFnRetType_),
                      node.name, params);

    // 参数落槽：传入的 SSA 值 %p 不可写，和 let 一样给栈槽，统一走 load/store
    for (auto& p : node.params) {
        std::string ty = irtypes::llvmType(p.type);
        std::string slot = "%" + p.name + ".addr";
        ir_.emitAlloca(slot, ty);
        ir_.emitStore(ty, "%" + p.name, slot);
        locals_[p.name] = slot;
    }
    // 函数体：块类型是 Unit 则 ret void，否则尾表达式的值即返回值
    // TODO(M3)：显式 ReturnExpr 之后的死代码处理
    visit(*node.body);
    if (isMain) {
        ir_.emitRet("i32", "0");
    } else if (typeOf(*node.body).kind == TypeKind::Unit) {
        ir_.emitRetVoid();
    } else {
        ir_.emitRet(irtypes::llvmType(currentFnRetType_), lastValue_);
    }
    ir_.endFunction();
    // 旧实现的 blankLine 写在模块区流里：每个函数结尾在模块区多一个空行
    ir_.raw("");
}

// ============================================================
// M2 起：以下是待填的桩，注释是该函数的 lowering 步骤
// ============================================================

// 块：先逐句 lowering，再算尾表达式（单独存在 tailExpr 字段，不在 statements
// 里）；尾表达式的值留在 lastValue_ 作为整个块的值
// TODO(M3)：return/break 之后的语句是死代码，加"块已终结"标志跳过
void IRGenerator::visit(Block& node) {
    for (auto& stmt : node.statements) {
        stmt -> accept(*this);
    }
    if (node.tailExpr) evalExpr(node.tailExpr);
}

// let：alloca 栈槽 + store 初值 + 登记 locals_（遮蔽 = 同名新槽覆盖）
void IRGenerator::visit(LetStmt& node) {
    Type ty = node.type ? *node.type : typeOf(*node.init);
    std::string slot = "%" + node.name + ".addr";
    ir_.emitAlloca(slot, irtypes::llvmType(ty));
    if (node.init) {
        std::string val = evalExpr(node.init);
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
        lastValue_ = node.text;
    }
    if (node.kind == LiteralKind::Bool) {
        if (node.text == "true") lastValue_ = "1";
        else if (node.text == "false") lastValue_ = "0";
    }
    if (node.kind == LiteralKind::Char) {
        lastValue_ = std::to_string((unsigned char)node.text[1]);
    }
    if (node.kind == LiteralKind::Str) {
        std::string content = node.text.substr(1, node.text.size() - 2);
        std::string name = ir_.addString(content);
        std::string p = ir_.freshReg();
        int len = content.size() + 1;
        ir_.emitGep(p, "[" + std::to_string(len) + " x i8]", name, {"0", "0"});
        lastValue_ = p;
    }
}

// 名字求值（值通道）：局部变量 → load；const → 编译期常量文本；
// 关联常量 Type::N → ConstDef 上的 constValue。左值场景走 evalAddr，不经这里
void IRGenerator::visit(PathExpr& node) {
    if (locals_.find(node.name) != locals_.end()) {
        std::string p = ir_.freshReg();
        ir_.emitLoad(p, irtypes::llvmType(typeOf(node)),
                     locals_.find(node.name)->second);
        lastValue_ = p;
        return;
    }
    Symbol* sym = symtab_.lookup(node.name);
    if (sym && sym -> kind == SymbolKind::Const && sym -> hasConstValue) {
        lastValue_ = std::to_string(sym -> constValue);
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
                    lastValue_ = std::to_string(cd->constValue);
                    return;
                }
            }
        }
    }
}

void IRGenerator::visit(BinaryExpr& node) {
    // 赋值类先分流：lhs 走 evalAddr（不 load），rhs 走 evalExpr
    if (node.op == "=") {
        std::string addr = evalAddr(node.lhs);
        std::string val = evalExpr(node.rhs);
        std::string ty = irtypes::llvmType(typeOf(*node.lhs));
        ir_.emitStore(ty, val, addr);
        // 赋值表达式的值是 ()，不留 lastValue_
        return;
    }
    // 复合赋值：load 旧值 → 运算 → store 回去
    if (node.op == "+=" || node.op == "-=" || node.op == "*="
        || node.op == "/=" || node.op == "%=") {
        std::string addr = evalAddr(node.lhs);
        std::string ty = irtypes::llvmType(typeOf(*node.lhs));
        bool lSigned = irtypes::isSignedInt(typeOf(*node.lhs));
        std::string old = ir_.freshReg();
        ir_.emitLoad(old, ty, addr);
        std::string r = evalExpr(node.rhs);
        std::string opc;
        if      (node.op == "+=") opc = "add";
        else if (node.op == "-=") opc = "sub";
        else if (node.op == "*=") opc = "mul";
        else if (node.op == "/=") opc = lSigned ? "sdiv" : "udiv";
        else                      opc = lSigned ? "srem" : "urem";
        std::string res = ir_.freshReg();
        ir_.emitBinary(opc, res, ty, old, r);
        ir_.emitStore(ty, res, addr);
        return;
    }

    std::string l = evalExpr(node.lhs);
    std::string r = evalExpr(node.rhs);
    bool lSigned = irtypes::isSignedInt(typeOf(*node.lhs));
    std::string opTy = irtypes::llvmType(typeOf(*node.lhs));  // 操作数类型

    // 算术：+ - * / %
    if (node.op == "+" || node.op == "-" || node.op == "*"
        || node.op == "/" || node.op == "%") {
        std::string opc;
        if      (node.op == "+") opc = "add";
        else if (node.op == "-") opc = "sub";
        else if (node.op == "*") opc = "mul";
        else if (node.op == "/") opc = lSigned ? "sdiv" : "udiv";
        else                     opc = lSigned ? "srem" : "urem";
        std::string res = ir_.freshReg();
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
        std::string c = ir_.freshReg();
        ir_.emitIcmp(pred, c, opTy, l, r);
        std::string z = ir_.freshReg();
        ir_.emitZext(z, "i1", "i8", c);
        lastValue_ = z;
        return;
    }

    // TODO(M3)：短路逻辑 &&, ||：条件跳转 + 两个基本块，不是 and/or 指令
    // TODO(低频)：位运算 & | ^ << >>：and/or/xor/shl；>> 按 lSigned 选 ashr/lshr
}

void IRGenerator::visit(UnaryExpr& node) {
    // TODO(M2/M5)：
    //   "-" → 0 减去操作数（sub i32 0, %v）
    //   "!" → xor i8 %v, 1（bool 是 i8）
    //   "&"/"&mut"（M5）→ 操作数走 evalAddr，lastValue_ = 地址本身
    //   "*"（M5）→ 操作数是 ptr，load 目标类型
}

void IRGenerator::visit(CastExpr& node) {
    // TODO(M2)：整型之间 as 全是 i32→i32，空操作，lastValue_ 直接透传；
    //   只有 bool/char（i8）↔ 整型（i32）要 zext/trunc
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

    // ③ 逐参数求值，收集值和类型字符串（拼参数列表用）
    std::vector<std::string> argVals, argTys;
    for (auto& arg : node.args) {
        argVals.push_back(evalExpr(arg));
        argTys.push_back(irtypes::llvmType(typeOf(*arg)));
    }

    // ④ 发 call：void 返回不接 %r =；否则结果进新寄存器
    // TODO(加固)：参数类型是 i8 而目标函数要 i32 时（printlnInt(bool)），先 zext
    Symbol* sym = symtab_.lookup(origName);
    std::vector<std::pair<std::string, std::string>> args;
    for (size_t i = 0; i < argVals.size(); i++)
        args.emplace_back(argTys[i], argVals[i]);
    if (sym->type.kind == TypeKind::Unit) {
        ir_.emitCall("", "void", llvmName, args);
    } else {
        std::string r = ir_.freshReg();
        ir_.emitCall(r, irtypes::llvmType(sym->type), llvmName, args);
        lastValue_ = r;
    }
}

// ---- M3：控制流 ----

// if：条件 i8 → icmp ne 0 转 i1 → 条件跳转；then/else 各自成块、末尾跳进汇合点
// 带值的 if（let x = if ...）：入口 alloca 槽，两分支 store，汇合点 load
void IRGenerator::visit(IfExpr& node) {
    std::string c = evalExpr(node.cond);
    std::string c1 = ir_.freshReg();
    ir_.emitIcmp("ne", c1, "i8", c, "0");
    std::string thenL = ir_.freshLabel("if.then");
    std::string elseL = ir_.freshLabel("if.else");
    std::string endL = ir_.freshLabel("if.end");
    bool hasElse = node.elseBlock != nullptr;
    // 块的类型是 Unit/Never 时这个 if 不产值（Never = 分支以 return 等收尾）
    bool hasValue = typeOf(node).kind != TypeKind::Unit && typeOf(node).kind != TypeKind::Never;
    std::string slot;
    std::string valTy = irtypes::llvmType(typeOf(node));
    if (hasValue) {
        slot = ir_.freshReg();
        ir_.emitAlloca(slot, valTy);
    }
    ir_.emitBrCond(c1, thenL, hasElse ? elseL : endL);
    ir_.label(thenL);
    visit(*node.thenBlock);
    // 分支以 return/break 收尾（类型 Never）时不 store 也不补 br——
    // 该块已经有终结指令（依赖 ReturnExpr 等发出 ret/br）
    if (typeOf(*node.thenBlock).kind != TypeKind::Never) {
        if (hasValue)
            ir_.emitStore(valTy, lastValue_, slot);
        ir_.emitBr(endL);
    }
    if (hasElse) {
        ir_.label(elseL);
        visit(*node.elseBlock);
        if (typeOf(*node.elseBlock).kind != TypeKind::Never) {
            if (hasValue)
                ir_.emitStore(valTy, lastValue_, slot);
            ir_.emitBr(endL);
        }
    }
    ir_.label(endL);
    if (hasValue) {
        std::string v = ir_.freshReg();
        ir_.emitLoad(v, valTy, slot);
        lastValue_ = v;
    }
}

void IRGenerator::visit(WhileExpr& node) {
    // TODO(M3)：
    //   标签 while.cond / while.body / while.end
    //   entry 先 br 到 cond；cond 里算条件再条件跳转 body/end；body 尾 br 回 cond
    //   进循环前 loopLabels_.push_back({cond, end})，出循环 pop
    std::string condL = ir_.freshLabel("while.cond");
    std::string bodyL = ir_.freshLabel("while.body");
    std::string endL = ir_.freshLabel("while.end");
    
}

void IRGenerator::visit(LoopExpr& node) {
    // TODO(M3)：同 while 但没有 cond 块：loop.body / loop.end，
    //   body 尾无条件 br 回 body；带值 break（M3 后期）：break 的值 store
    //   进 alloca 槽，end 里 load
}

void IRGenerator::visit(ReturnExpr& node) {
    // TODO(M3)：有值则 evalExpr(node.value)，ret <类型> <值>；无值 ret void
    //   注意 ret 之后当前基本块已终结：后续指令要落到新标签块里
    //   （可以让 Block 在发完 ret 后补一个 freshLabel 的死代码块承接）
}

void IRGenerator::visit(BreakExpr& node) {
    // TODO(M3)：br label %<loopLabels_.back().second>；带值 break 先 store
    //   进当前 loop 的结果槽；块终结问题同 ReturnExpr
}

void IRGenerator::visit(ContinueExpr& node) {
    // TODO(M3)：br label %<loopLabels_.back().first>；块终结问题同上
}

// ---- M4：方法 ----

void IRGenerator::visit(ImplBlock& node) {
    // visit(Crate) 已直接遍历 methods，这里不会走到；留空
    (void)node;
}

void IRGenerator::visit(MethodCallExpr& node) {
    // TODO(M4/M5)：
    //   1. receiver 的类型查 exprTypes_ → 类型名 T
    //   2. 查 symtab_.implMethods[T] 找名字叫 node.method 的 Function
    //   3. self 实参：receiver 走 evalAddr（&self/&mut self）或 evalExpr
    //      （按值 self），作为第一个实参
    //   4. call @T_method(self, args...)——与 Function 外壳的 mangling 对应
}

// ---- M5：复合类型 ----

void IRGenerator::visit(StructLiteralExpr& node) {
    // TODO(M5)：
    //   1. %s = alloca %struct.Name
    //   2. 逐字段：字段下标查符号表 structDef 字段表（第几个字段），
    //      %p = getelementptr %struct.Name, ptr %s, i32 0, i32 <下标>
    //      store <字段类型> <初值>, ptr %p
    //   3. lastValue_ = %s（结构体"值"就用它的地址表示，约定成俗）
}

void IRGenerator::visit(FieldExpr& node) {
    // TODO(M5)：object 的地址（evalAddr）+ 字段下标 → getelementptr → load
    //   作左值时只要 gep 不 load
}

void IRGenerator::visit(ArrayExpr& node) {
    // TODO(M5)：
    //   列举 [a, b, c]：alloca [3 x T]，逐下标 gep + store
    //   重复 [0; N]：值只算一次存起来，循环 store N 次（或先展开成 N 条
    //     store，N 小的测试点够用）
}

void IRGenerator::visit(IndexExpr& node) {
    // TODO(M5)：数组地址 + 下标值 →
    //   %p = getelementptr [N x T], ptr %arr, i32 0, i32 <下标>
    //   load / 作左值直接用这个 %p
}

// ---- 不 codegen 的条目 ----

void IRGenerator::visit(ConstDef& node) {
    // 常量没有运行时代码：值在编译期（ConstEvaluator）已算好，
    // 使用处（PathExpr）直接内联常量文本
    (void)node;
}

void IRGenerator::visit(StaticDef& node) {
    // TODO(M5 后期，测试点少)：@name = global <类型> <初值常量>
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
