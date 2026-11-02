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

// ============================================================
// M1 已实现：模块外壳 + struct 类型定义 + 函数外壳
// ============================================================

void IRGenerator::visit(Crate& node) {
    ir_.raw("target triple = \"riscv32-unknown-unknown-elf\"");
    ir_.blankLine();
    // 内建函数声明（builtin.c 提供实现；declare 了不用无害）
    ir_.raw("declare void @printInt(i32)");
    ir_.raw("declare void @printlnInt(i32)");
    ir_.raw("declare i32 @getInt()");
    ir_.raw("declare void @exit(i32)");
    ir_.blankLine();
    // struct 类型定义先于函数体输出（字段类型可能引用别的 struct）
    for (auto& item : node.items)
        if (auto s = std::dynamic_pointer_cast<StructDef>(item)) visit(*s);
    ir_.blankLine();
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
    ir_.beginFunction();
    locals_.clear();
    loopLabels_.clear();
    currentFnRetType_ =
        node.returnType ? *node.returnType : Type{TypeKind::Unit};

    // TODO(M4)：方法名要 mangling（如 Point::new → @_Point_new 之类），
    //   否则两个 impl 里的同名方法会撞车
    std::string sig = "define " + irtypes::llvmType(currentFnRetType_)
                    + " @" + node.name + "(";
    for (size_t i = 0; i < node.params.size(); ++i) {
        if (i) sig += ", ";
        sig += irtypes::llvmType(node.params[i].type) + " %" + node.params[i].name;
    }
    // TODO(M4)：isMethod 时第一个参数是 self（&self → ptr，按值 self → %struct.X）
    sig += ") {";
    ir_.rawBody(sig);
    ir_.label("entry");

    // TODO(M2) 起，函数体 lowering 替换掉下面的"默认返回"：
    //   1. 每个参数 p：%p.addr = alloca <类型>; store <类型> %p, ptr %p.addr；
    //      登记 locals_[p.name] = "%p.addr"
    //   2. visit(*node.body)：Block 逐句 lowering，
    //      尾表达式的值在 lastValue_ 里（Block 类型非 void 时）
    //   3. 收尾：ret <返回类型> <尾表达式的值>；void 则 ret void
    //      （显式 ReturnExpr 自己发 ret，其后是死代码，见 visit(ReturnExpr)）

    std::string dv = irtypes::defaultValue(currentFnRetType_);
    if (dv.empty()) ir_.emit("ret void");
    else            ir_.emit("ret " + irtypes::llvmType(currentFnRetType_) + " " + dv);
    ir_.rawBody("}");
    ir_.blankLine();
    ir_.endFunction();
}

// ============================================================
// M2 起：以下是待填的桩，注释是该函数的 lowering 步骤
// ============================================================

void IRGenerator::visit(Block& node) {
    // TODO(M2)：
    //   逐句 visit(node.statements[i])；
    //   块是表达式：最后一个无分号的尾表达式（ExprStmt::hasSemi == false）
    //   的值留在 lastValue_ 里作为整个块的值；没有则是 void
}

void IRGenerator::visit(LetStmt& node) {
    // TODO(M2)：
    //   1. %name.addr = alloca <类型>（类型：node.type 标注或查 exprTypes
    //      里 init 的类型）
    //   2. 有初值：evalExpr(node.init) 得值，store <类型> <值>, ptr %name.addr
    //   3. locals_[node.name] = "%name.addr"（遮蔽旧同名槽，直接覆盖）
    Type ty = node.type ? *node.type : typeOf(*node.init);
    std::string slot = "%" + node.name + ".addr";
    ir_.emitAssign(slot, "alloca " + irtypes::llvmType(ty));
    if (node.init) {
        std::string val = evalExpr(node.init);
        ir_.emit("store " + irtypes::llvmType(ty) + " " + val + ", ptr " + slot);
    }
    locals_[node.name] = slot;
}

void IRGenerator::visit(ExprStmt& node) {
    // TODO(M2)：evalExpr(node.expr) 即可；hasSemi 为 false 时这是块的
    //   尾表达式，值留在 lastValue_ 给块用
}

void IRGenerator::visit(Literal& node) {
    // TODO(M2)：按 node.kind 分：
    //   Int   → lastValue_ = node.text（直接当常量文本用）
    //   Bool  → lastValue_ = "1" / "0"（bool 在 IR 是 i8）
    //   Char  → lastValue_ = 字符的 ASCII 码数值
    //   Str   → ir_.addString(node.text) 得 "@.str.N"，
    //           lastValue_ = "getelementptr inbounds ([K x i8], ptr @.str.N, i32 0, i32 0)"
    //           之类的首元素指针（opaque ptr 下直接当 ptr 用也行）
    //   Float → 直接写十进制文本
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
        ir_.emitAssign(p, "getelementptr inbounds [" + std::to_string(len) + " x i8], ptr " + name + ", i32 0, i32 0");
        lastValue_ = p;
    }
}

void IRGenerator::visit(PathExpr& node) {
    // TODO(M2)：按名字查：
    //   locals_ 有 → %v = load <类型>, ptr %name.addr；lastValue_ = %v
    //   const 常量 → 符号表 Symbol::hasConstValue，直接用常量文本
    //   关联常量 Type::N → ConstDef 节点上的 constValue
    // 注意：作为赋值左值时不能 load，要的是地址本身——见 visit(BinaryExpr)
    //   赋值分支的处理方式（左值地址需要另一条通道：可以把"取地址"写成
    //   一个独立的 evalAddr(const ExprPtr&) 辅助函数）

    if (locals_.find(node.name) != locals_.end()) {
        std::string p = ir_.freshReg();
        ir_.emitAssign(p, "load " + irtypes::llvmType(typeOf(node)) + ", ptr " + locals_.find(node.name)->second);
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
    // TODO(M2/M3)：
    //   赋值类（=, +=, -= ...）：lhs 走 evalAddr 拿地址，rhs 算值，
    //     store 进去；复合赋值先 load 旧值运算再 store
    //   短路逻辑（&&, ||，M3）：翻译成条件跳转 + phi/alloca，不是 and/or
    //   比较（==, !=, <, <=, >, >=）：icmp，有符号性按 typeOf(*node.lhs)
    //     选 sgt/ugt 等（irtypes::isSignedInt）；结果是 i1，zext 到 i8
    //   算术（+ - * / %）：add/sub/mul；除/模按有符号性选 sdiv/udiv、
    //     srem/urem
    //   位运算（& | ^ << >>）：and/or/xor；移位按有符号性选 ashr/lshr
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
        ir_.emitAssign(res, opc + " " + opTy + " " + l + ", " + r);
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
        ir_.emitAssign(c, "icmp " + pred + " " + opTy + " " + l + ", " + r);
        std::string z = ir_.freshReg();
        ir_.emitAssign(z, "zext i1 " + c + " to i8");
        lastValue_ = z;
        return;
    }

    // TODO(M2 尾巴)：赋值类 =, +=, -= ...：lhs 走 evalAddr 拿地址（不 load），
    //   rhs 算值，store 进去；复合赋值 = load 旧值 + 运算 + store。
    //   赋值表达式的值是 ()，不用留 lastValue_
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

void IRGenerator::visit(CallExpr& node) {
    // TODO(M2 内建 / M4 一般)：
    //   1. callee 是 PathExpr：查函数名
    //   2. 内建名字映射：printlnInt/println_i32 → @printlnInt，
    //      printInt/print_i32 → @printInt，getInt/get_i32 → @getInt，
    //      exit → @exit
    //   3. 逐参数 evalExpr 收集值，拼 call：
    //      %r = call i32 @getInt()  /  call void @printInt(i32 %v)
    //   4. 一般函数（M4）：返回类型查符号表 Symbol::type；void 返回的
    //      call 不接 %r =
}

// ---- M3：控制流 ----

void IRGenerator::visit(IfExpr& node) {
    // TODO(M3)：
    //   %c = evalExpr(node.cond)（i8）；转 i1：%cond = icmp ne i8 %c, 0
    //   三个标签 if.then / if.else / if.end（freshLabel）
    //   br i1 %cond, label %if.then, label %if.else（无 else 则else 直接指 end）
    //   各分支 visit Block；带值 if（表达式用值时）两个分支把值 store 进
    //   同一个 alloca 槽，end 里 load 出来
}

void IRGenerator::visit(WhileExpr& node) {
    // TODO(M3)：
    //   标签 while.cond / while.body / while.end
    //   entry 先 br 到 cond；cond 里算条件再条件跳转 body/end；body 尾 br 回 cond
    //   进循环前 loopLabels_.push_back({cond, end})，出循环 pop
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
