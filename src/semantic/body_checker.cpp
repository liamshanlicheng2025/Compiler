#include "semantic/body_checker.h"

#include "semantic_error.h"
#include "semantic/semantic_error.h"

static bool isInt(const Type& t){
    return t.kind == TypeKind::I32 || t.kind == TypeKind::U32 || t.kind == TypeKind::Isize
        || t.kind == TypeKind::Usize || t.kind == TypeKind::IntLit;
}

// 整数字面量归一化：落到符号表/数组元素等"存起来"的地方时定版为 I32
static void normalizeIntLit(Type& t){
    if (t.kind == TypeKind::IntLit) t.kind = TypeKind::I32;
}

void BodyChecker::run(const CratePtr& crate) {
    crate->accept(*this);
}

// 统一登记：子表达式求值后立刻把类型记进旁表（IR 阶段按节点指针查类型）
void BodyChecker::evalExpr(const ExprPtr& e) {
    e->accept(*this);
    exprTypes_[e.get()] = lastType_;
}

void BodyChecker::visit(Crate& node) {
    rxDialect_ = node.rxDialect;   // Rx 方言下 if/while 条件不要求括号
    // 顶层条目里只有函数有"体"要检查；const/static 的初值也要查
    for (auto& item : node.items) item->accept(*this);
}

// 把类型里的 Self 归一化为当前 impl 的目标类型名；impl 外出现 Self 报错
void BodyChecker::resolveSelfType(Type& t) {
    if (t.kind == TypeKind::Named && t.name == "Self") {
        if (currentImplType_.empty())
            semaError("Self 只能在 impl 块内使用");
        t.name = currentImplType_;
    }
    if (t.elem) resolveSelfType(*t.elem);
}

// 类型合法性检查：具名类型必须已注册（值命名空间的同名符号不算数）；
// 带泛型实参使用时，该 struct 必须确实声明了泛型参数（S<i32> 但 S 无泛型要拦）
void BodyChecker::checkTypeResolvable(const Type& t) {
    if (t.kind == TypeKind::Named) {
        Symbol* s = symtab_.lookupType(t.name);
        if (!s) semaError("未解析的类型：" + t.name);
        if (t.hasGenericArgs && !(s->structDef && s->structDef->hasGenericParams))
            semaError("类型 " + t.name + " 没有类型参数");
    }
    if (t.elem) checkTypeResolvable(*t.elem);
}

void BodyChecker::visit(Function& node) {
    // 记录返回类型（嵌套函数会覆盖它，进出时保存/恢复）、开作用域、
    // 注册 self（方法）和参数、查函数体、收尾比对
    Type savedRetType = currentFnRetType_;
    currentFnRetType_ = node.returnType ? *node.returnType : Type{TypeKind::Unit};
    resolveSelfType(currentFnRetType_);
    checkTypeResolvable(currentFnRetType_);
    constEval_.resolveArrayLens(currentFnRetType_);   // fn make_table(n) -> [[i32; n]; n] 之类要拦
    symtab_.enterScope();
    if (node.isMethod){
        Type inner;
        inner.kind = TypeKind::Named;
        inner.name = currentImplType_;
        Type selfT;
        if (node.selfByValue) {
            selfT = inner;                     // 按值 self：类型就是 struct 本体
        } else {
            selfT.kind = TypeKind::Ref;
            selfT.isMut = node.selfMut;
            selfT.elem = std::make_shared<Type>(inner);
        }
        Symbol sym{SymbolKind::Variable, "self", selfT, node.selfMut};
        symtab_.insert(sym);
    }
    for (auto& p : node.params) {
        // 可变参数（fn f(mut n: i32)）的 isMut 由 builder 从 pattern 取出
        Type pt = p.type;
        resolveSelfType(pt);
        checkTypeResolvable(pt);
        constEval_.resolveArrayLens(pt);              // 参数里的数组长度同样要解析
        Symbol sym{SymbolKind::Variable, p.name, pt, p.isMut};
        if (!symtab_.insert(sym)) semaError("重复的参数名：" + p.name);
    }
    evalExpr(node.body);
    if (!lastType_.equal(currentFnRetType_)) semaError("函数 " + node.name + " 返回类型不匹配：声明 " + currentFnRetType_.toString() + "，实际 " + lastType_.toString());
    symtab_.exitScope();
    currentFnRetType_ = savedRetType;
}

void BodyChecker::visit(Block& node) {
    // 作用域进出 + 遍历语句 + 尾表达式求类型
    symtab_.enterScope();
    // 块内 fn 提升（item hoisting，c46）：先注册嵌套函数签名，再逐条检查
    for (auto& stmt : node.statements) {
        if (auto* is = dynamic_cast<ItemStmt*>(stmt.get())) {
            if (auto fn = std::dynamic_pointer_cast<Function>(is->item)) {
                Symbol sym;
                sym.kind = SymbolKind::Function;
                sym.name = fn->name;
                for (auto& p : fn->params) sym.paramTypes.push_back(p.type);
                sym.type = fn->returnType ? *fn->returnType : Type{TypeKind::Unit};
                symtab_.insert(sym);
            }
        }
    }
    Type lastStmtType{TypeKind::Unit};
    bool diverged = false;   // 是否有语句发散（return/break/不停 loop）：其后代码不可达
    for (size_t i = 0; i < node.statements.size(); ++i) {
        auto& stmt = node.statements[i];
        stmt->accept(*this);
        lastStmtType = lastType_;   // 语句的类型（ExprStmt 会留下其中表达式的类型）
        if (lastDiverges_) diverged = true;
        // 非末尾位置的无分号表达式语句必须没有值（() 或 Never）：
        // { 1 } println_i32(2); 这种形态要拦
        bool isLast = (i + 1 == node.statements.size());
        if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (!es->hasSemi && (!isLast || node.tailExpr)
                && lastStmtType.kind != TypeKind::Unit
                && lastStmtType.kind != TypeKind::Never)
                semaError("块中间的无分号表达式语句必须返回 ()，实际 "
                          + lastStmtType.toString());
        }
    }
    if (node.tailExpr) {
        evalExpr(node.tailExpr);   // 块的类型 = 尾表达式类型
        // 发散传播：任何语句发散（其后代码不可达）或尾表达式发散，块都发散
        lastDiverges_ = diverged || lastDiverges_;
    } else {
        // 末尾是"无分号的带块表达式语句"（if/match/loop/裸块）时，它就是块的值
        //（函数体末尾的 if (k == p) { a[p] } else ... 这类形态）
        bool lastIsValue = false;
        if (!node.statements.empty()) {
            if (auto* es = dynamic_cast<ExprStmt*>(node.statements.back().get())) {
                if (!es->hasSemi && es->expr &&
                    (dynamic_cast<IfExpr*>(es->expr.get())
                  || dynamic_cast<LoopExpr*>(es->expr.get())
                  || dynamic_cast<MatchExpr*>(es->expr.get())
                  || dynamic_cast<Block*>(es->expr.get())))
                    lastIsValue = true;
            }
        }
        if (lastIsValue) {
            lastType_ = lastStmtType;
            lastDiverges_ = diverged;
        } else if (diverged) {
            // 中途有语句发散：块本身发散（Never），兼容任何上下文
            lastType_ = Type{TypeKind::Never};
            lastDiverges_ = true;
        } else {
            lastType_ = Type{TypeKind::Unit};
            lastDiverges_ = false;
        }
    }
    symtab_.exitScope();
}

// let 声明：确定变量类型（标注优先，无标注则从初值推导），
// 检查标注与初值匹配，然后注册进当前作用域
void BodyChecker::visit(LetStmt& node) {
    Type varType;
    if (node.init) {
        evalExpr(node.init);           // 初值求类型，结果在 lastType_
        Type initType = lastType_;          // 立刻存走——之后的 visit 会覆盖它
        if (node.type) {
            Type ann = *node.type;
            resolveSelfType(ann);
            checkTypeResolvable(ann);
            constEval_.resolveArrayLens(ann);         // 标注里的数组长度（[i32; ARR] 等）
            if (!initType.equal(ann))
                semaError("let " + node.name + " 类型不匹配：标注 "
                          + ann.toString() + "，初值 " + initType.toString());
            varType = ann;
        } else {
            varType = initType;             // 无标注：类型推导 = 初值类型
        }
    } else {
        if (!node.type)
            semaError("let " + node.name + " 既无类型标注也无初值，无法确定类型");
        varType = *node.type;
        constEval_.resolveArrayLens(varType);
    }
    Symbol sym{SymbolKind::Variable, node.name, varType, node.isMut};
    normalizeIntLit(sym.type);   // 存进符号表前定版：let x = 1; → x 是 i32
    // Rust 允许同作用域 let 遮蔽；重复定义检查
    // 只针对函数/类型/常量等顶层名字（CrateIndex 那边仍报错）
    symtab_.insertOrShadow(sym);
    lastDiverges_ = false;
}

void BodyChecker::visit(ExprStmt& node) {
    if (node.expr) evalExpr(node.expr);   // 语句位置的表达式，类型丢弃
}

void BodyChecker::visit(Literal& node) {
    // 种类在 builder 里已标好（LiteralKind），这里一对一翻译成类型
    switch (node.kind){
        case LiteralKind::Int: {
            // 溢出检查：剥掉类型后缀后按范围判断。
            // u32/usize 后缀上限是 u32 最大值；
            // 无后缀按 i32 判断，但一元 - 正下方允许 2147483648（i32 MIN）
            std::string digits = node.text;
            long long limit = 2147483647LL;
            TypeKind suffixKind = TypeKind::Unknown;
            for (const char* suf : {"usize", "isize", "i32", "u32"}) {
                auto p = digits.rfind(suf);
                if (p != std::string::npos && p + std::strlen(suf) == digits.size()) {
                    if (suf[0] == 'u') limit = 4294967295LL;   // u32/usize
                    if (std::strcmp(suf, "i32") == 0)   suffixKind = TypeKind::I32;
                    if (std::strcmp(suf, "u32") == 0)   suffixKind = TypeKind::U32;
                    if (std::strcmp(suf, "isize") == 0) suffixKind = TypeKind::Isize;
                    if (std::strcmp(suf, "usize") == 0) suffixKind = TypeKind::Usize;
                    digits = digits.substr(0, p);
                    break;
                }
            }
            long long v;
            try {
                v = std::stoll(digits, nullptr, 0);
            } catch (...) {
                semaError("整数字面量溢出：" + node.text);
            }
            if (v > limit && !(allowI32MinLit_ && v == 2147483648LL))
                semaError("整数字面量溢出（超出范围）：" + node.text);
            // 带后缀的字面量类型当场定型（let x: isize = 1i32; 要报不匹配）；
            // 无后缀保持 IntLit，由上下文归一化
            lastType_ = suffixKind == TypeKind::Unknown
                      ? Type{TypeKind::IntLit} : Type{suffixKind};
            break;
        }
        case LiteralKind::Float: lastType_ = Type{TypeKind::F64}; break;
        case LiteralKind::Bool: lastType_ = Type{TypeKind::Bool}; break;
        case LiteralKind::Char: lastType_ = Type{TypeKind::Char}; break;
        case LiteralKind::Str: {
            // 字符串字面量的真实类型是 &str（Rust 语义），标注 &str 才能匹配
            Type r;
            r.kind = TypeKind::Ref;
            r.elem = std::make_shared<Type>(Type{TypeKind::Str});
            lastType_ = r;
            break;
        }
        case LiteralKind::Unit: lastType_ = Type{TypeKind::Unit}; break;
    }
    lastDiverges_ = false;
}

// 二元运算：按 op 分五类——赋值 / 复合赋值 / 算术位运算 / 比较 / 逻辑
void BodyChecker::visit(BinaryExpr& node) {
    evalExpr(node.lhs);
    Type lhsT = lastType_;              // 立刻存走——visit rhs 会覆盖 lastType_
    evalExpr(node.rhs);
    Type rhsT = lastType_;

    auto isLValue = [](const ExprPtr& e) {
        if (dynamic_cast<PathExpr*>(e.get()))  return true;
        if (dynamic_cast<FieldExpr*>(e.get())) return true;
        if (dynamic_cast<IndexExpr*>(e.get())) return true;
        if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) return u->op == "*";
        return false;
    };

    // 赋值可变性检查：沿 FieldExpr/IndexExpr 走到根上的变量名，查符号的 isMut。
    //（result[i][j] = ...、seq[0] = 99 这类也要查到根变量的可变性）
    auto checkAssignable = [&](const ExprPtr& e) {
        if (!isLValue(e)) semaError("赋值目标不可赋值");
        ExprPtr cur = e;
        while (true) {
            if (auto* p = dynamic_cast<PathExpr*>(cur.get())) {
                Symbol* sym = symtab_.lookup(p->name);
                if (sym && (!sym->isMut || sym->kind == SymbolKind::Const)) {
                    // 透过 &mut 引用改指向的内容合法（如 arr: &mut [T; 10]）；
                    // 但直接给引用变量本身赋值仍需 isMut（cur == e 即直接赋值）
                    bool throughMutRef = (cur != e) && sym->type.kind == TypeKind::Ref
                                         && sym->type.isMut;
                    if (!throughMutRef)
                        semaError("给不可变变量赋值：" + p->name);
                }
                return;
            }
            if (auto* u = dynamic_cast<UnaryExpr*>(cur.get())) {
                if (u->op == "*") {
                    // 通过引用赋值：途经的每一层引用都必须可写（&mut）。
                    // let p = &x; *p = 2;（共享引用）和
                    // let q = &p; **q = 2;（外层共享）都要拦
                    ExprPtr op = u->operand;
                    while (true) {
                        evalExpr(op);
                        if (lastType_.kind != TypeKind::Ref || !lastType_.isMut)
                            semaError("通过不可变引用赋值");
                        auto* uu = dynamic_cast<UnaryExpr*>(op.get());
                        if (!uu || uu->op != "*") break;
                        op = uu->operand;
                    }
                    return;
                }
            }
            if (auto* f = dynamic_cast<FieldExpr*>(cur.get())) { cur = f->object; continue; }
            if (auto* ix = dynamic_cast<IndexExpr*>(cur.get())) { cur = ix->array; continue; }
            return;   // 其他形态（如函数调用返回的 &mut）：可变性由类型保证，不查
        }
    };

    if (node.op == "=") {                       // ① 赋值
        checkAssignable(node.lhs);
        if (!lhsT.equal(rhsT))
            semaError("赋值类型不匹配：左 " + lhsT.toString() + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "+=" || node.op == "-=" || node.op == "*="
            || node.op == "/=" || node.op == "%=") {   // ② 复合赋值
        checkAssignable(node.lhs);
        if (!isInt(lhsT) || !lhsT.equal(rhsT))
            semaError("复合赋值要求两边都是相同整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "&=" || node.op == "|=" || node.op == "^=") {
        // 复合位运算赋值：整型同型，或 bool 两边（b &= false 是合法 Rx）
        checkAssignable(node.lhs);
        bool intOk  = isInt(lhsT) && lhsT.equal(rhsT);
        bool boolOk = lhsT.kind == TypeKind::Bool && rhsT.kind == TypeKind::Bool;
        if (!intOk && !boolOk)
            semaError("复合位运算赋值要求两边同为整型或同为 bool：左 "
                      + lhsT.toString() + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "<<=" || node.op == ">>=") {   // 移位赋值：同移位规则
        checkAssignable(node.lhs);
        if (!isInt(lhsT) || !isInt(rhsT))
            semaError("移位赋值要求两边都是整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "+" || node.op == "-" || node.op == "*" || node.op == "/"
            || node.op == "%" || node.op == "&" || node.op == "|" || node.op == "^") {
        // bool 的 & | ^ 是合法（非短路）逻辑运算，结果仍 bool
        if ((node.op == "&" || node.op == "|" || node.op == "^")
            && lhsT.kind == TypeKind::Bool && rhsT.kind == TypeKind::Bool) {
            lastType_ = Type{TypeKind::Bool};
            lastDiverges_ = false;
            return;
        }
        if (!isInt(lhsT) || !lhsT.equal(rhsT))
            semaError("算术/位运算要求两边都是相同整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        // 结果取具体的一侧（1 + x（u32）→ u32）；两侧都是字面量则保持 IntLit
        lastType_ = (lhsT.kind == TypeKind::IntLit) ? rhsT : lhsT;
    } else if (node.op == "<<" || node.op == ">>") {   // 移位：两边都整型即可，
        if (!isInt(lhsT) || !isInt(rhsT))              // 位数类型可与左操作数不同
            semaError("移位运算要求两边都是整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = lhsT;
    } else if (node.op == "==" || node.op == "!=" || node.op == "<" || node.op == ">"
            || node.op == "<=" || node.op == ">=") {   // ④ 比较：Bool 是产出
        if (!lhsT.equal(rhsT))
            semaError("比较运算两边类型不一致：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Bool};
    } else if (node.op == "&&" || node.op == "||") {   // ⑤ 逻辑：Bool 是要求也是产出
        if (lhsT.kind != TypeKind::Bool || rhsT.kind != TypeKind::Bool)
            semaError("逻辑运算要求两边都是 bool：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Bool};
    }
    lastDiverges_ = false;
}

// 路径表达式：查符号表，把名字的类型写进 lastType_
void BodyChecker::visit(PathExpr& node) {
    // 多段路径：首段必须是已注册类型。enum 变体（Operation::Add）的类型 =
    // Named(枚举名)；struct 关联函数当值用给 Unknown 兜底
    if (node.name.find("::") != std::string::npos) {
        std::string head = node.name.substr(0, node.name.find("::"));
        Symbol* s = symtab_.lookupType(head);
        if (!s || (s->kind != SymbolKind::StructType && s->kind != SymbolKind::EnumType))
            semaError("未声明的名字：" + node.name);
        if (s->kind == SymbolKind::EnumType) {
            Type t; t.kind = TypeKind::Named; t.name = head;
            lastType_ = t;
        } else {
            // struct 的关联常量（Config::NEG）：类型 = 常量的声明类型；
            // 查不到（如关联函数 Point::new 当值用）则 Unknown 兜底
            std::string tail = node.name.substr(node.name.find("::") + 2);
            if (symtab_.implConsts.count(head)) {
                for (auto& cd : symtab_.implConsts[head]) {
                    if (cd -> name == tail) {
                        lastType_ = cd->type;
                        lastDiverges_ = false;
                        return;
                    }
                }
            }
            lastType_ = Type{TypeKind::Unknown};
        }
        lastDiverges_ = false;
        return;
    }
    Symbol* sym = symtab_.lookup(node.name);
    if (!sym) semaError("未声明的名字：" + node.name);
    if (sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Const
        || sym->kind == SymbolKind::Static) {
        lastType_ = sym->type;
    } else {
        // Function / StructType 不当值用：给 Unknown 兜底（equal 对 Unknown 放行）
        lastType_ = Type{TypeKind::Unknown};
    }
    lastDiverges_ = false;
}

// 一元运算：-（整型取负）、!（bool 取反）、&/&mut（引用）、*（解引用）
void BodyChecker::visit(UnaryExpr& node) {
    // 一元 - 正下方的整数字面量允许取到 2147483648（合起来是 i32 MIN）
    bool savedAllow = allowI32MinLit_;
    if (node.op == "-")
        if (auto* lit = dynamic_cast<Literal*>(node.operand.get()))
            if (lit->kind == LiteralKind::Int) allowI32MinLit_ = true;
    evalExpr(node.operand);
    allowI32MinLit_ = savedAllow;
    Type t = lastType_;
    if (node.op == "-") {
        // 只有有符号整型能取负（-1u32 要拦）
        if (t.kind != TypeKind::I32 && t.kind != TypeKind::Isize
            && t.kind != TypeKind::IntLit)
            semaError("一元 - 只适用于有符号整型，实际 " + t.toString());
        lastType_ = t;
    } else if (node.op == "!") {
        // bool 取反得 bool；整型的 ! 是按位取反（Rust 语义，c29），类型不变
        if (t.kind != TypeKind::Bool && !isInt(t))
            semaError("一元 ! 要求 bool 或整型，实际 " + t.toString());
        lastType_ = t;
    } else if (node.op == "&" || node.op == "&mut") {
        // &mut 要求操作数可写
        if (node.op == "&mut") {
            if (auto* p = dynamic_cast<PathExpr*>(node.operand.get())) {
                Symbol* sym = symtab_.lookup(p->name);
                if (sym && !sym->isMut)
                    semaError("对不可变变量取 &mut 引用：" + p->name);
            }
        }
        Type r;
        r.kind = TypeKind::Ref;
        r.isMut = (node.op == "&mut");
        r.elem = std::make_shared<Type>(t);
        lastType_ = r;
    } else if (node.op == "*") {
        if (t.kind != TypeKind::Ref) semaError("对非引用类型解引用：" + t.toString());
        lastType_ = *t.elem;
    }
    lastDiverges_ = false;
}

// if 表达式：条件必须 bool；有 else 时两分支类型一致（值 = 分支类型），
// 无 else 时 then 块必须没有值（() 或发散），整个 if 值为 ()
void BodyChecker::visit(IfExpr& node) {
    // 条件括号是旧课程规范的要求；Rx 方言按标准 Rust 语义，不要求
    if (!rxDialect_ && !node.condGrouped) semaError("if 条件缺少括号");
    evalExpr(node.cond);
    if (lastType_.kind != TypeKind::Bool && lastType_.kind != TypeKind::Never)
        semaError("if 条件必须是 bool，实际 " + lastType_.toString());
    evalExpr(node.thenBlock);
    Type thenT = lastType_;
    bool thenDiverges = lastDiverges_;
    if (node.elseBlock) {
        evalExpr(node.elseBlock);
        Type elseT = lastType_;
        if (!elseT.equal(thenT))
            semaError("if 两分支类型不一致：then " + thenT.toString()
                      + "，else " + elseT.toString());
        // 一侧发散（return/break）时，结果取另一侧的类型
        lastType_ = (thenT.kind == TypeKind::Never) ? elseT : thenT;
        // 只有两侧都发散（且都有 else 覆盖所有情况）整个 if 才发散
        lastDiverges_ = thenDiverges && lastDiverges_;
    } else {
        if (thenT.kind != TypeKind::Unit && thenT.kind != TypeKind::Never)
            semaError("没有 else 的 if 分支必须返回 ()，实际 " + thenT.toString());
        lastType_ = Type{TypeKind::Unit};
        lastDiverges_ = false;   // 条件为假时正常走完，不发散
    }
}

// while 循环：条件必须 bool（Never 放行）；值为 ()。
// 体内的带值 break 不上传（Rust 不允许 while 带值 break），进出时保存/恢复
void BodyChecker::visit(WhileExpr& node) {
    if (!rxDialect_ && !node.condGrouped) semaError("while 条件缺少括号");
    // 条件不属于循环体：条件里的 break/continue 不允许绑定到任何循环
    //（loop { while continue {} } 要拦）
    int savedDepth = loopDepth_;
    loopDepth_ = 0;
    evalExpr(node.cond);
    loopDepth_ = savedDepth;
    if (lastType_.kind != TypeKind::Bool && lastType_.kind != TypeKind::Never)
        semaError("while 条件必须是 bool，实际 " + lastType_.toString());
    ++loopDepth_;
    bool savedHas = hasLoopBreakValue_; Type savedT = loopBreakType_;
    bool savedAny = hasAnyBreak_;
    hasLoopBreakValue_ = false;
    hasAnyBreak_ = false;
    evalExpr(node.body);
    Type bodyT = lastType_;
    bool tookBreakValue = hasLoopBreakValue_;
    hasLoopBreakValue_ = savedHas; loopBreakType_ = savedT;
    hasAnyBreak_ = savedAny;
    --loopDepth_;
    if (tookBreakValue)
        semaError("while 循环不接受带值 break");
    if (bodyT.kind != TypeKind::Unit && bodyT.kind != TypeKind::Never)
        semaError("while 循环体必须返回 ()，实际 " + bodyT.toString());
    lastType_ = Type{TypeKind::Unit};
    lastDiverges_ = false;   // 条件不成立时正常退出，while 本身不发散
}

// loop 无限循环：值 = 体内带值 break 的类型；无 break 时只能靠 return 离开，
// 类型是 Never（发散，兼容任何上下文）；有普通 break 则 ()
void BodyChecker::visit(LoopExpr& node) {
    ++loopDepth_;
    bool savedHas = hasLoopBreakValue_; Type savedT = loopBreakType_;
    bool savedAny = hasAnyBreak_;
    hasLoopBreakValue_ = false;
    hasAnyBreak_ = false;
    evalExpr(node.body);
    Type bodyT = lastType_;
    if (bodyT.kind != TypeKind::Unit && bodyT.kind != TypeKind::Never)
        semaError("loop 循环体必须返回 ()，实际 " + bodyT.toString());
    bool anyBreak = hasAnyBreak_;
    if (hasLoopBreakValue_) lastType_ = loopBreakType_;
    else if (!anyBreak) lastType_ = Type{TypeKind::Never};
    else lastType_ = Type{TypeKind::Unit};
    // 没有任何 break 的 loop 只能靠 return 离开：发散
    lastDiverges_ = !anyBreak;
    hasLoopBreakValue_ = savedHas; loopBreakType_ = savedT;
    hasAnyBreak_ = savedAny;
    --loopDepth_;
}

// return：有值则与函数声明返回类型比对，无值则要求函数返回 ()；自身类型 Never（发散）
void BodyChecker::visit(ReturnExpr& node) {
    if (node.value) {
        evalExpr(node.value);
        if (!lastType_.equal(currentFnRetType_))
            semaError("return 类型不匹配：函数声明返回 " + currentFnRetType_.toString()
                      + "，实际 " + lastType_.toString());
    } else {
        if (currentFnRetType_.kind != TypeKind::Unit)
            semaError("函数声明返回 " + currentFnRetType_.toString()
                      + "，但 return 没有返回值");
    }
    lastType_ = Type{TypeKind::Never};   // 发散：equal 对 Never 放行，兼容一切上下文
    lastDiverges_ = true;
}

// break/continue：必须在循环内（loopDepth_ > 0）；自身类型 Never
// break 带值（break expr;）：记录值类型，由最近的 loop 取走作为循环的类型
void BodyChecker::visit(BreakExpr& node) {
    if (loopDepth_ == 0) semaError("break 不在循环内");
    hasAnyBreak_ = true;
    if (node.value) {
        evalExpr(node.value);
        loopBreakType_ = lastType_;
        hasLoopBreakValue_ = true;
    }
    lastType_ = Type{TypeKind::Never};
    lastDiverges_ = true;
}

void BodyChecker::visit(ContinueExpr& node) {
    if (loopDepth_ == 0) semaError("continue 不在循环内");
    lastType_ = Type{TypeKind::Never};
    lastDiverges_ = true;
}

// 函数调用：callee 必须是 PathExpr。两种形态：
//   add(1, 2)        → 符号表查 Function 符号，签名在 Symbol::paramTypes / type
//   Point::new(1, 2) → 名字含 "::"，去 implMethods[类型名] 里按方法名找，
//                      签名在该方法的 AST 节点（params / returnType）上
void BodyChecker::visit(CallExpr& node) {
    auto* callee = dynamic_cast<PathExpr*>(node.callee.get());
    if (!callee) semaError("不支持的调用形式");
    auto checkArgs = [&](const std::string& fnName,
                         const std::vector<Type>& params, const Type& ret) {
        if (node.args.size() != params.size())
            semaError("函数 " + fnName + " 参数个数不对：期望 "
                      + std::to_string(params.size()) + " 个，实际 "
                      + std::to_string(node.args.size()) + " 个");
        for (size_t i = 0; i < node.args.size(); ++i) {
            evalExpr(node.args[i]);
            if (!lastType_.equal(params[i]))
                semaError("函数 " + fnName + " 第 " + std::to_string(i + 1)
                          + " 个参数类型不匹配：期望 " + params[i].toString()
                          + "，实际 " + lastType_.toString());
        }
        lastType_ = ret;
    };
    auto pos = callee->name.rfind("::");
    if (pos != std::string::npos) {
        std::string typeName = callee->name.substr(0, pos);
        std::string fnName = callee->name.substr(pos + 2);
        // 内建关联函数：String::from(&str) -> String
        if (typeName == "String" && fnName == "from") {
            if (node.args.size() != 1) semaError("String::from 需要 1 个参数");
            evalExpr(node.args[0]);
            Type t; t.kind = TypeKind::Named; t.name = "String";
            lastType_ = t;
            lastDiverges_ = false;
            return;
        }
        // Self::method()：当前 impl 目标类型
        if (typeName == "Self") typeName = currentImplType_;
        auto it = symtab_.implMethods.find(typeName);
        if (it != symtab_.implMethods.end()) {
            for (auto& m : it->second) {
                if (m->name == fnName) {
                    std::vector<Type> params;
                    for (auto& p : m->params) params.push_back(p.type);
                    Type ret = m->returnType ? *m->returnType : Type{TypeKind::Unit};
                    checkArgs(callee->name, params, ret);
                    lastDiverges_ = false;
                    return;
                }
            }
        }
        semaError("类型 " + typeName + " 没有关联函数 " + fnName);
    }
    Symbol* sym = symtab_.lookup(callee->name);
    if (!sym || sym->kind != SymbolKind::Function)
        semaError("未声明的函数：" + callee->name);
    checkArgs(callee->name, sym->paramTypes, sym->type);
    lastDiverges_ = false;
}

// 字段访问 p.x：object 必须是 Named 类型；按类型名查 StructType 符号，
// 回指 structDef 找字段定义，字段类型写进 lastType_
void BodyChecker::visit(FieldExpr& node) {
    evalExpr(node.object);
    Type objT = lastType_;
    while (objT.kind == TypeKind::Ref) {
        objT = *objT.elem;
    }
    if (objT.kind != TypeKind::Named)
        semaError("对非 struct 类型取字段：" + objT.toString());
    Symbol* sym = symtab_.lookupType(objT.name);
    if (!sym || sym->kind != SymbolKind::StructType)
        semaError("类型 " + objT.name + " 不是 struct");
    if (!sym->structDef)
        semaError("内建类型 " + objT.name + " 没有字段可访问");
    for (auto& f : sym->structDef->fields) {
        if (f.name == node.field) {
            lastType_ = f.type;
            lastDiverges_ = false;
            return;
        }
    }
    semaError("struct " + objT.name + " 没有字段 " + node.field);
}

// 方法调用 p.shift(3)：由 receiver 的类型定位 implMethods，找到方法后
// 比对实参（self 不在 params 里）；&mut self 方法要求 receiver 可写
void BodyChecker::visit(MethodCallExpr& node) {
    evalExpr(node.receiver);
    Type recvT = lastType_;
    // 自动解引用：&Point 的 receiver 也能调 Point 的方法（&self 本来就是这么传的）
    while (recvT.kind == TypeKind::Ref) recvT = *recvT.elem;
    // 内建方法：数组 len()；字符串 len()/is_empty()/to_string()；整型 to_string()
    // （课程测试用到的标准库方法极简子集，返回类型直接写死）
    if (recvT.kind == TypeKind::Array && node.method == "len") {
        if (!node.args.empty()) semaError("len() 不接受参数");
        lastType_ = Type{TypeKind::Usize};
        lastDiverges_ = false;
        return;
    }
    if (recvT.kind == TypeKind::Str
        || (recvT.kind == TypeKind::Named && recvT.name == "String")) {
        Type ret;
        if (node.method == "len")           ret = Type{TypeKind::Usize};
        else if (node.method == "is_empty") ret = Type{TypeKind::Bool};
        else if (node.method == "to_string") { ret.kind = TypeKind::Named; ret.name = "String"; }
        else semaError("字符串类型不支持方法 " + node.method);
        lastType_ = ret;
        lastDiverges_ = false;
        return;
    }
    if (isInt(recvT) && node.method == "to_string") {
        Type t; t.kind = TypeKind::Named; t.name = "String";
        lastType_ = t;
        lastDiverges_ = false;
        return;
    }
    if (recvT.kind != TypeKind::Named)
        semaError("对非 struct 类型调方法：" + recvT.toString());
    auto it = symtab_.implMethods.find(recvT.name);
    if (it != symtab_.implMethods.end()) {
        for (auto& m : it->second) {
            if (m->name == node.method) {
                // 关联函数（没有 self 参数）不能通过 . 调用，只能 Type::f() 走
                if (!m->isMethod)
                    semaError(node.method + " 是关联函数（无 self 参数），"
                              "不能通过 . 调用，请用 " + recvT.name + "::" + node.method + "()");
                if (node.args.size() != m->params.size())
                    semaError("方法 " + node.method + " 参数个数不对：期望 "
                              + std::to_string(m->params.size()) + " 个，实际 "
                              + std::to_string(node.args.size()) + " 个");
                for (size_t i = 0; i < node.args.size(); ++i) {
                    evalExpr(node.args[i]);
                    if (!lastType_.equal(m->params[i].type))
                        semaError("方法 " + node.method + " 第 "
                                  + std::to_string(i + 1)
                                  + " 个参数类型不匹配：期望 "
                                  + m->params[i].type.toString() + "，实际 "
                                  + lastType_.toString());
                }
                // &mut self 方法要求 receiver 可写：沿 Field/Index/解引用走到
                // 根变量查符号（self.heap.push() 在 &mut self 方法里合法——
                // 根 self 的 isMut 来自方法的 selfMut）。
                // receiver 是临时值（make().add(2)）时直接放行：
                // 临时值本身就是可写的
                if (m->selfMut) {
                    ExprPtr cur = node.receiver;
                    Symbol* s = nullptr;
                    while (true) {
                        if (auto* p = dynamic_cast<PathExpr*>(cur.get())) {
                            s = symtab_.lookup(p->name);
                            break;
                        }
                        if (auto* f = dynamic_cast<FieldExpr*>(cur.get())) { cur = f->object; continue; }
                        if (auto* ix = dynamic_cast<IndexExpr*>(cur.get())) { cur = ix->array; continue; }
                        if (auto* u = dynamic_cast<UnaryExpr*>(cur.get())) {
                            if (u->op == "*") { cur = u->operand; continue; }
                        }
                        break;
                    }
                    bool ok = !s || (s->isMut
                        || (s->type.kind == TypeKind::Ref && s->type.isMut));
                    if (!ok)
                        semaError("&mut self 方法 " + node.method
                                  + " 的 receiver 必须是可变变量");
                }
                lastType_ = m->returnType ? *m->returnType : Type{TypeKind::Unit};
                lastDiverges_ = false;
                return;
            }
        }
    }
    semaError("类型 " + recvT.name + " 没有方法 " + node.method);
}

// 结构体字面量 Point { x: 1, y: 2 }：名字必须是已注册 struct；
// 字段不缺不多、每个初值类型匹配；类型为 Named(结构体名)
void BodyChecker::visit(StructLiteralExpr& node) {
    Symbol* sym = symtab_.lookupType(node.name);
    if (!sym || sym->kind != SymbolKind::StructType)
        semaError("未定义的 struct：" + node.name);
    if (node.fields.size() != sym->structDef->fields.size())
        semaError("struct " + node.name + " 字段个数不对：期望 "
                  + std::to_string(sym->structDef->fields.size()) + " 个，实际 "
                  + std::to_string(node.fields.size()) + " 个");
    for (auto& [fname, valueExpr] : node.fields) {
        const Type* fieldType = nullptr;
        for (auto& f : sym->structDef->fields) {
            if (f.name == fname) { fieldType = &f.type; break; }
        }
        if (!fieldType)
            semaError("struct " + node.name + " 没有字段 " + fname);
        evalExpr(valueExpr);
        if (!lastType_.equal(*fieldType))
            semaError("struct " + node.name + " 字段 " + fname
                      + " 类型不匹配：期望 " + fieldType->toString()
                      + "，实际 " + lastType_.toString());
    }
    lastType_ = Type{TypeKind::Named};
    lastType_.name = node.name;
    lastDiverges_ = false;
}

// 数组字面量两种形态：列举 [1, 2, 3]（repeatValue 为空）与重复 [0; 5]
void BodyChecker::visit(ArrayExpr& node) {
    if (!node.repeatValue) {
        // ---- 列举形式 ----
        if (node.elements.empty())
            semaError("空数组字面量无法推导类型");
        evalExpr(node.elements[0]);
        Type firstT = lastType_;
        for (size_t i = 1; i < node.elements.size(); ++i) {
            evalExpr(node.elements[i]);
            if (!lastType_.equal(firstT))
                semaError("数组元素类型不一致：第 1 个是 " + firstT.toString()
                          + "，第 " + std::to_string(i + 1) + " 个是 "
                          + lastType_.toString());
        }
        Type arr;
        arr.kind = TypeKind::Array;
        arr.elem = std::make_shared<Type>(firstT);
        arr.arrayLen = node.elements.size();
        lastType_ = arr;
        lastDiverges_ = false;
    } else {
        // ---- 重复形式 [0; 5] ----
        evalExpr(node.repeatValue);
        Type elemT = lastType_;
        evalExpr(node.repeatCount);
        if (!isInt(lastType_))
            semaError("数组重复次数必须是整型，实际 " + lastType_.toString());
        // 次数是字面量时取具体数值；
        // 不是字面量则长度记 0（equal 比较数组时长度 0 只影响数组间比较，兜底用）
        size_t len = 0;
        if (auto* lit = dynamic_cast<Literal*>(node.repeatCount.get()))
            len = static_cast<size_t>(std::stoull(lit->text));
        // 次数是 const 名（[1; N]）时的三道检查：
        //   1. 名字必须解析到 const（局部变量 → 拒）
        //   2. 该 const 必须是 usize 类型（i32 常量也拒）
        //   3. 有编译期值则用它作数组长度（evaluateAll 已预求值）
        if (auto* p = dynamic_cast<PathExpr*>(node.repeatCount.get())) {
            if (p -> name.find("::") != std::string::npos) {
                // 关联常量（[1; Config::N]）：拆开查 implConsts，检查同上，
                // 编译期值读 evaluateAll 写回 ConstDef 节点的 constValue
                std::string head = p -> name.substr(0, p -> name.find("::"));
                std::string tail = p -> name.substr(p -> name.find("::") + 2);
                const ConstDef* cd = nullptr;
                if (symtab_.implConsts.count(head)) {
                    for (auto& c : symtab_.implConsts[head]) {
                        if (c -> name == tail) { cd = c.get(); break; }
                    }
                }
                if (!cd)
                    semaError("数组重复次数必须是常量：" + p -> name);
                if (cd -> type.kind != TypeKind::Usize)
                    semaError("数组重复次数必须是 usize 常量：" + p -> name
                              + "，实际类型 " + cd -> type.toString());
                if (cd -> hasConstValue) len = static_cast<size_t>(cd -> constValue);
            } else {
                Symbol* s = symtab_.lookup(p -> name);
                if (!s || s -> kind != SymbolKind::Const)
                    semaError("数组重复次数必须是常量：" + p -> name);
                if (s -> type.kind != TypeKind::Usize)
                    semaError("数组重复次数必须是 usize 常量：" + p -> name
                              + "，实际类型 " + s -> type.toString());
                if (s -> hasConstValue) len = static_cast<size_t>(s -> constValue);
            }
        }
        Type arr;
        arr.kind = TypeKind::Array;
        arr.elem = std::make_shared<Type>(elemT);
        arr.arrayLen = len;
        lastType_ = arr;
        lastDiverges_ = false;
    }
}

void BodyChecker::visit(IndexExpr& node) {
    evalExpr(node.array);
    // 自动解引用：&[T; N] 也能直接用下标
    while (lastType_.kind == TypeKind::Ref) lastType_ = *lastType_.elem;
    if (lastType_.kind != TypeKind::Array)
        semaError("对非数组取下标：" + lastType_.toString());
    Type elemT = *lastType_.elem;        // 先存走元素类型
    size_t arrLen = lastType_.arrayLen;  // 已知长度（0 = 未知）留给越界检查
    evalExpr(node.index);
    // Rx 方言下标必须是 usize（字面量 IntLit 可推导为 usize）；
    // 旧规范只要求整型
    if (rxDialect_) {
        if (lastType_.kind != TypeKind::Usize && lastType_.kind != TypeKind::IntLit)
            semaError("数组下标必须是 usize，实际 " + lastType_.toString());
    } else {
        if (!isInt(lastType_)) semaError("数组下标必须是整型");
    }
    // 常量下标越界检查（如 grid[3][3]，grid 是 [[i32; 3]; 3]）
    long long idx;
    if (arrLen != 0 && ConstEvaluator::evalIntLiteral(node.index, idx)
        && (idx < 0 || idx >= static_cast<long long>(arrLen)))
        semaError("数组下标越界：下标 " + std::to_string(idx)
                  + "，长度 " + std::to_string(arrLen));
    lastType_ = elemT;
    lastDiverges_ = false;
}

void BodyChecker::visit(ConstDef& node) {
    // 全局常量：初值类型必须等于标注类型
    //   node.value->accept(*this);
    //   if (!lastType_.equal(node.type)) semaError("const " + node.name + " 初值类型不匹配");
    // （常量求值留到 IR 阶段；这里只查类型。node.value 为空理论不可能，g4 要求必填）
    evalExpr(node.value);
    if (!lastType_.equal(node.type)) semaError("const " + node.name + " 初值类型不匹配");
    // 初值是整数字面量时把编译期值记进符号（数组长度常量解析用）
    long long v;
    if (ConstEvaluator::evalIntLiteral(node.value, v)) {
        if (Symbol* s = symtab_.lookup(node.name)) {
            s->constValue = v;
            s->hasConstValue = true;
        }
    }
}

void BodyChecker::visit(StaticDef& node) {
    // 同 ConstDef：node.value->accept(*this)，与 node.type 比对
    evalExpr(node.value);
    if (!lastType_.equal(node.type)) semaError("static " + node.name + " 初值类型不匹配");
}

// enum/match 课程不要求（助教 CR 确认）：AST 里仍可能有这些节点，
// 遇到直接跳过，不做检查
void BodyChecker::visit(MatchExpr&) {}
void BodyChecker::visit(EnumDef&) {}

// 语句级宏：println!/print! 的参数不解析（CodeGen 阶段处理 argsText），直接放行；
// 其他宏名不在课程范围内，报错
void BodyChecker::visit(MacroStmt& node) {
    if (node.name != "println" && node.name != "print")
        semaError("不支持的宏：" + node.name);
}

// as 类型转换：检查子表达式后，结果类型就是目标类型（数值间转换直接放行）
// as 类型转换：只允许数值标量之间的转换——整型互转、整型↔浮点、
// bool/char → 整型；其余组合报错（int→bool、引用→整型、struct 参与等）
void BodyChecker::visit(CastExpr& node) {
    evalExpr(node.expr);
    Type src = lastType_;
    const Type& dst = node.target;
    auto intLike = [](TypeKind k) {
        return k == TypeKind::I32 || k == TypeKind::U32
            || k == TypeKind::Isize || k == TypeKind::Usize
            || k == TypeKind::IntLit;
    };
    auto floaty = [](TypeKind k) {
        return k == TypeKind::F32 || k == TypeKind::F64;
    };
    bool ok;
    if (src.kind == TypeKind::Unknown) ok = true;   // 出错兜底，不级联误报
    else if (intLike(src.kind) && (intLike(dst.kind) || floaty(dst.kind))) ok = true;
    else if (floaty(src.kind) && (intLike(dst.kind) || floaty(dst.kind))) ok = true;
    else if (src.kind == TypeKind::Bool && intLike(dst.kind)) ok = true;
    else if (src.kind == TypeKind::Char && intLike(dst.kind)) ok = true;
    else ok = false;
    if (!ok)
        semaError("不支持的类型转换：" + src.toString() + " as " + dst.toString());
    lastType_ = dst;
    lastDiverges_ = false;
}

// 块内条目：嵌套 fn / 局部 const/static——注册进当前作用域并检查体
void BodyChecker::visit(ItemStmt& node) {
    if (auto fn = std::dynamic_pointer_cast<Function>(node.item)) {
        // 签名已在 visit(Block) 提升注册过；insert 失败（重名已存在）属正常，忽略
        Symbol sym;
        sym.kind = SymbolKind::Function;
        sym.name = fn->name;
        for (auto& p : fn->params) sym.paramTypes.push_back(p.type);
        sym.type = fn->returnType ? *fn->returnType : Type{TypeKind::Unit};
        symtab_.insert(sym);
        fn->accept(*this);   // visit(Function) 自带作用域和返回类型保存/恢复
    } else if (auto cd = std::dynamic_pointer_cast<ConstDef>(node.item)) {
        cd->accept(*this);   // 初值类型比对
        Symbol sym{SymbolKind::Const, cd->name, cd->type, false};
        long long v;         // 局部 const 的编译期值（如 [bool; N] 的 N）
        if (ConstEvaluator::evalIntLiteral(cd->value, v)) { sym.constValue = v; sym.hasConstValue = true; }
        if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
    } else if (auto sd = std::dynamic_pointer_cast<StaticDef>(node.item)) {
        sd->accept(*this);
        Symbol sym{SymbolKind::Static, sd->name, sd->type, sd->isMut};
        if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
    } else if (auto st = std::dynamic_pointer_cast<StructDef>(node.item)) {
        // 块内 struct 定义：注册类型符号进当前作用域
        Symbol sym;
        sym.kind = SymbolKind::StructType;
        sym.name = st->name;
        sym.structDef = st;
        if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
    }
    lastDiverges_ = false;
}

// 顶层不会出现这些，防御性留空
void BodyChecker::visit(StructDef&) {}
void BodyChecker::visit(ImplBlock& node) {
    // impl 里的方法也是 Function，要逐个检查函数体
    currentImplType_ = node.typeName;
    for (auto& m : node.methods) m->accept(*this);
    currentImplType_.clear();
}
