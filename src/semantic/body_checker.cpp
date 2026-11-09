#include "semantic/body_checker.h"

#include "semantic_error.h"
#include "semantic/semantic_error.h"

static bool isInt(const Type& t){
    return t.kind == TypeKind::I32 || t.kind == TypeKind::U32 || t.kind == TypeKind::Isize
        || t.kind == TypeKind::Usize || t.kind == TypeKind::IntLit;
}

// 剥掉所有引用层（==/!= 相等比较的对象是引用指向的值）
static Type peelRefs(Type t){
    while (t.kind == TypeKind::Ref) t = *t.elem;
    return t;
}

// 只剥一层引用（算术/位运算/移位只允许一层共享引用，&&a + &b 要拦）
static Type peelOneRef(Type t){
    if (t.kind == TypeKind::Ref) t = *t.elem;
    return t;
}

// TODO(同学完成)：隐式类型转换（coercion）——src 的值能否隐式当作 dst 用。
//
// 用在所有"值流动"的位置：let 初值→类型标注、赋值右值→左值类型、
// 函数/方法实参→形参类型、return 值→函数返回类型、struct 字段初值。
//
// 规则：
//   1. src.equal(dst) 成立 → true
//      （equal 已覆盖：Unknown/Never 通配、IntLit 兼容整型、
//      引用外层 &mut→& 的 reborrow）
//   2. src 和 dst 都是引用（Ref）时，额外允许"解引用 coercion"：
//      src 指向一个 Box 容器链，dst 指向链上更内层的类型。即：
//        &Box<Box<i32>> 可以隐式转成 &Box<i32>、&i32（以及它自身）
//      实现思路：写一个递归的 derefChain(from, to)——
//        from 与 to "严格相同"（kind/name/isMut 逐字段相等，elem 递归）→ true；
//        from 是 Box<X>（isBoxType(from)，X = *from.elem）→ 递归 derefChain(X, to)；
//        否则 false。
//      然后：src.kind == Ref && dst.kind == Ref
//            && src 的可变性兼容（src.isMut 或 !dst.isMut——&mut 可以当 & 用）
//            && derefChain(*src.elem, *dst.elem) → true
//      注意：derefChain 里的"严格相同"不要直接用 Type::equal——
//      equal 对引用内层也有 &mut→& 的放行，会把
//      rej-inner-reference-mutability 误放过去
//      （内层引用的 mut 不允许被改写：&mut &i32 不能变成 &mut &mut i32，
//      上面的规则天然满足：derefChain 只穿 Box，不动 Ref 层）
//   3. 其他情况 → false
bool BodyChecker::coercible(const Type& src, const Type& dst) {
    // 引用→引用走专属路径：外层可变性兼容（&mut 可以当 & 用），
    // 目标类型沿 Box 解引用链可达
    if (src.kind == TypeKind::Ref && dst.kind == TypeKind::Ref) {
        if (!src.isMut && dst.isMut) return false;   // & 不能当 &mut 用
        // derefChain(from, to)：from 与 to 严格相同（不能用 equal——
        // 它对引用内层也有 &mut→& 放行，会误放 rej-inner-reference-mutability）；
        // from 是 Box<X> 则递归穿一层
        auto strictSame = [](const Type& a, const Type& b, auto& self) -> bool {
            // IntLit 兼容任何具体整型（&mut [0; 7] 可以当 &mut [i32; 7] 用）；
            // Unknown/Never 通配兜底
            auto concreteInt = [](TypeKind k) {
                return k == TypeKind::I32 || k == TypeKind::U32
                    || k == TypeKind::Isize || k == TypeKind::Usize;
            };
            if (a.kind == TypeKind::IntLit && concreteInt(b.kind)) return true;
            if (b.kind == TypeKind::IntLit && concreteInt(a.kind)) return true;
            if (a.kind == TypeKind::Unknown || a.kind == TypeKind::Never
                || b.kind == TypeKind::Unknown || b.kind == TypeKind::Never) return true;
            if (a.kind != b.kind) return false;
            if (a.kind == TypeKind::Named && a.name != b.name) return false;
            if (a.kind == TypeKind::Ref && a.isMut != b.isMut) return false;
            if (a.kind == TypeKind::Array && a.arrayLen != 0 && b.arrayLen != 0
                && a.arrayLen != b.arrayLen) return false;
            if (static_cast<bool>(a.elem) != static_cast<bool>(b.elem)) return false;
            if (a.elem) return self(*a.elem, *b.elem, self);
            return true;
        };
        const Type* from = src.elem.get();
        const Type* to = dst.elem.get();
        while (true) {
            if (strictSame(*from, *to, strictSame)) return true;
            if (isBoxType(*from) && from->elem) {
                from = from->elem.get();
                continue;
            }
            return false;
        }
    }
    return src.equal(dst);
}

// TODO(同学完成)：类型合并（最小上界 LUB）。
//
// 用在三处：if/else 两臂类型合并、数组字面量元素类型合并、
// loop 多个带值 break 的值类型合并。
//
// 规则：
//   1. a 是 Never → out = b，返回 true；b 是 Never → out = a，返回 true
//      （发散的一侧不影响结果，这条要放在第 2 条之前——
//      equal 对 Never 通配，不先处理会得到 Never）
//   2. a.equal(b) → out = a，返回 true
//   3. 两边都是引用且 elem 相等 →
//      out = Ref{ kind: Ref, isMut: a.isMut && b.isMut, elem: a.elem }
//      （&mut i32 和 &i32 合并成 &i32；两个 &mut 合并还是 &mut）
//   4. 无法合并 → 返回 false（out 不用管）
bool BodyChecker::lub(const Type& a, const Type& b, Type& out) {
    if (a.kind == TypeKind::Never) { out = b; return true; }
    if (b.kind == TypeKind::Never) { out = a; return true; }
    if (a.equal(b)) { out = a; return true; }
    // 两边都是引用且 elem 相等：合并成更共享的一侧
    //（&mut i32 和 &i32 合并成 &i32；两个 &mut 合并还是 &mut）
    if (a.kind == TypeKind::Ref && b.kind == TypeKind::Ref
        && a.elem && b.elem && a.elem->equal(*b.elem)) {
        out = a;
        out.isMut = a.isMut && b.isMut;
        return true;
    }
    return false;
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
        // Vec/Box 必须带恰好一个具体的元素类型（let b: Box = ... 和
        // Box<i32, bool> 都要拦；builder 把参数个数不对的 elem 留空）
        if ((isVecType(t) || isBoxType(t)) && !t.elem)
            semaError(t.name + " 需要恰好一个类型参数");
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
        // 目标是 &mut 且初值是从某个位置取出的 &mut 值时，涉及可变重借
        //（共享化 &mut→& 不受限：acc-shared-coercions）
        if (node.type && node.type->kind == TypeKind::Ref && node.type->isMut)
            checkReborrowUse(node.init);
        if (node.type) {
            Type ann = *node.type;
            resolveSelfType(ann);
            checkTypeResolvable(ann);
            constEval_.resolveArrayLens(ann);         // 标注里的数组长度（[i32; ARR] 等）
            if (!coercible(initType, ann))
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

    // 赋值可写性检查：普通位置要求整体可写（沿 Field/Index 到根变量查
    // 可变绑定，遇引用层由引用可变性决定）；解引用赋值要求引用本身可写，
    // 且存放这个 &mut 值的位置允许可变重借（Vec 元素不绕过 Vec 可变性）
    auto checkAssignable = [&](const ExprPtr& e) {
        if (!isLValue(e)) semaError("赋值目标不可赋值");
        if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) {
            if (u->op == "*") {
                evalExpr(u->operand);
                if (lastType_.kind == TypeKind::Ref) {
                    if (!lastType_.isMut || !reborrowOk(u->operand))
                        semaError("通过不可变引用赋值");
                } else if (isBoxType(lastType_)) {
                    if (!placeWritable(u->operand))
                        semaError("通过不可变的 Box 拥有者赋值");
                } else {
                    semaError("通过不可变引用赋值");
                }
                return;
            }
        }
        if (!placeWritable(e)) semaError("赋值目标不可写（需要可变位置）");
    };

    if (node.op == "=") {                       // ① 赋值
        checkAssignable(node.lhs);
        if (!coercible(rhsT, lhsT))
            semaError("赋值类型不匹配：左 " + lhsT.toString() + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "+=" || node.op == "-=" || node.op == "*="
            || node.op == "/=" || node.op == "%=") {   // ② 复合赋值
        checkAssignable(node.lhs);
        Type r = peelOneRef(rhsT);   // 右操作数自动解引用一层（x -= &2 合法）
        if (!isInt(lhsT) || !lhsT.equal(r))
            semaError("复合赋值要求两边都是相同整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "&=" || node.op == "|=" || node.op == "^=") {
        // 复合位运算赋值：整型同型，或 bool 两边（b &= false 是合法 Rx）
        checkAssignable(node.lhs);
        Type r = peelOneRef(rhsT);
        bool intOk  = isInt(lhsT) && lhsT.equal(r);
        bool boolOk = lhsT.kind == TypeKind::Bool && r.kind == TypeKind::Bool;
        if (!intOk && !boolOk)
            semaError("复合位运算赋值要求两边同为整型或同为 bool：左 "
                      + lhsT.toString() + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "<<=" || node.op == ">>=") {   // 移位赋值：同移位规则
        checkAssignable(node.lhs);
        Type r = peelOneRef(rhsT);
        if (!isInt(lhsT) || !isInt(r))
            semaError("移位赋值要求两边都是整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = Type{TypeKind::Unit};
    } else if (node.op == "+" || node.op == "-" || node.op == "*" || node.op == "/"
            || node.op == "%" || node.op == "&" || node.op == "|" || node.op == "^") {
        // 算术/位运算：两边操作数自动解引用一层（&a + &b 比的是指向的值；
        // &&a + &b 超过一层要拦，见 rej-arithmetic-supports-only-one-shared-reference-layer）
        Type l = peelOneRef(lhsT), r = peelOneRef(rhsT);
        // bool 的 & | ^ 是合法（非短路）逻辑运算，结果仍 bool
        if ((node.op == "&" || node.op == "|" || node.op == "^")
            && l.kind == TypeKind::Bool && r.kind == TypeKind::Bool) {
            lastType_ = Type{TypeKind::Bool};
            lastDiverges_ = false;
            return;
        }
        if (!isInt(l) || !l.equal(r))
            semaError("算术/位运算要求两边都是相同整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        // 结果取具体的一侧（1 + x（u32）→ u32）；两侧都是字面量则保持 IntLit
        lastType_ = (l.kind == TypeKind::IntLit) ? r : l;
    } else if (node.op == "<<" || node.op == ">>") {   // 移位：两边都整型即可，
        Type l = peelOneRef(lhsT), r = peelOneRef(rhsT); // 位数类型可与左操作数不同
        if (!isInt(l) || !isInt(r))
            semaError("移位运算要求两边都是整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        lastType_ = l;
    } else if (node.op == "==" || node.op == "!=" || node.op == "<" || node.op == ">"
            || node.op == "<=" || node.op == ">=") {   // ④ 比较：Bool 是产出
        bool ordering = (node.op != "==" && node.op != "!=");
        // 排序不允许引用与值混用（a < &b 要拦）；==/!= 可以混
        if (ordering && (lhsT.kind == TypeKind::Ref) != (rhsT.kind == TypeKind::Ref))
            semaError("排序比较两边必须同为引用或同为值：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        if (ordering && lhsT.kind == TypeKind::Ref && rhsT.kind == TypeKind::Ref) {
            // 引用间排序是不对称的：右侧必须能隐式转换到左侧类型
            //（&i32 < &mut i32 可以；&mut i32 < &i32 要拦），比较的是指向的值
            if (!coercible(rhsT, lhsT))
                semaError("排序比较右侧不能转换为左侧类型：左 " + lhsT.toString()
                          + "，右 " + rhsT.toString());
            Type l = peelRefs(lhsT), r = peelRefs(rhsT);
            if (!l.equal(r))
                semaError("比较运算两边类型不一致：左 " + lhsT.toString()
                          + "，右 " + rhsT.toString());
        } else {
            // ==/!= 及非引用混合：两侧自动解引用后按目标值比较
            Type l = peelRefs(lhsT), r = peelRefs(rhsT);
            if (!l.equal(r))
                semaError("比较运算两边类型不一致：左 " + l.toString()
                          + "，右 " + r.toString());
            // struct/容器比较要求支持 PartialEq（derive 或内建容器 Vec/Box）
            if (l.kind == TypeKind::Named && !hasPartialEqType(l))
                semaError("类型 " + l.toString() + " 不支持相等比较（缺少 PartialEq）");
        }
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
    // Named(枚举名)；struct 关联函数当值用给 Unknown 兜底。
    // impl 方法里的 Self::N 指向当前 impl 的目标类型
    if (node.name.find("::") != std::string::npos) {
        std::string head = node.name.substr(0, node.name.find("::"));
        if (head == "Self") {
            if (currentImplType_.empty())
                semaError("Self 只能在 impl 块内使用");
            head = currentImplType_;
        }
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
        // 允许对一层共享引用指向的值取负（-&a 合法）；可变引用不支持
        //（rej-mutable-reference-unary-arithmetic-unsupported）
        if (t.kind == TypeKind::Ref) {
            if (t.isMut)
                semaError("一元 - 不支持可变引用操作数：" + t.toString());
            t = *t.elem;
        }
        // 只有有符号整型能取负（-1u32 要拦）
        if (t.kind != TypeKind::I32 && t.kind != TypeKind::Isize
            && t.kind != TypeKind::IntLit)
            semaError("一元 - 只适用于有符号整型，实际 " + t.toString());
        lastType_ = t;
    } else if (node.op == "!") {
        // 同 -：允许一层共享引用（!&a），&mut 不支持
        if (t.kind == TypeKind::Ref) {
            if (t.isMut)
                semaError("一元 ! 不支持可变引用操作数：" + t.toString());
            t = *t.elem;
        }
        // bool 取反得 bool；整型的 ! 是按位取反（Rust 语义，c29），类型不变
        if (t.kind != TypeKind::Bool && !isInt(t))
            semaError("一元 ! 要求 bool 或整型，实际 " + t.toString());
        lastType_ = t;
    } else if (node.op == "&" || node.op == "&mut") {
        // &mut 借用要求位置可写（变量可变绑定，或经 &mut 引用层）；
        // 借用解引用位置（&mut *p）按可变重借规则判定
        if (node.op == "&mut") {
            if (auto* u = dynamic_cast<UnaryExpr*>(node.operand.get())) {
                if (u->op == "*" && !reborrowOk(u->operand))
                    semaError("对不可变位置取 &mut 引用");
            } else if (!placeWritable(node.operand)) {
                semaError("对不可变位置取 &mut 引用");
            }
        }
        Type r;
        r.kind = TypeKind::Ref;
        r.isMut = (node.op == "&mut");
        r.elem = std::make_shared<Type>(t);
        lastType_ = r;
    } else if (node.op == "*") {
        // 解引用：引用 *p 得指向的值；Box 是唯一拥有者，*b 取得堆上的值
        if (isBoxType(t)) {
            lastType_ = *t.elem;
        } else {
            if (t.kind != TypeKind::Ref) semaError("对非引用类型解引用：" + t.toString());
            lastType_ = *t.elem;
        }
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
        // 两臂类型合并（引用可变性取更宽的一侧；发散臂不影响结果）
        Type merged;
        if (lub(thenT, elseT, merged)) {
            lastType_ = merged;
        } else {
            semaError("if 两分支类型不一致：then " + thenT.toString()
                      + "，else " + elseT.toString());
        }
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
        if (!coercible(lastType_, currentFnRetType_))
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
        if (hasLoopBreakValue_) {
            // 同一层 loop 的多个带值 break：值类型合并
            //（loop { break &b; break &mut f; } 的循环类型是 &i32）
            Type merged;
            if (lub(loopBreakType_, lastType_, merged)) {
                loopBreakType_ = merged;
            } else {
                semaError("同一 loop 的 break 值类型不一致："
                          + loopBreakType_.toString() + " 与 "
                          + lastType_.toString());
            }
        } else {
            loopBreakType_ = lastType_;
            hasLoopBreakValue_ = true;
        }
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
            if (!coercible(lastType_, params[i]))
                semaError("函数 " + fnName + " 第 " + std::to_string(i + 1)
                          + " 个参数类型不匹配：期望 " + params[i].toString()
                          + "，实际 " + lastType_.toString());
            // 实参是从某个位置取出的 &mut 值且形参是 &mut 时，涉及可变重借
            if (params[i].kind == TypeKind::Ref && params[i].isMut)
                checkReborrowUse(node.args[i]);
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
        // 内建容器关联函数：Vec::<T>::new() / Vec::<T>::clone(&v)、
        // Box::<T>::new(v) / Box::<T>::clone(&b)
        if (typeName == "Vec" || typeName == "Box") {
            Type elem;
            if (callee->genericArg) elem = *callee->genericArg;
            Type containerT = (typeName == "Vec") ? vecOf(elem) : boxOf(elem);
            Type expectRef;                        // &Vec<T> / &Box<T>
            expectRef.kind = TypeKind::Ref;
            expectRef.elem = std::make_shared<Type>(containerT);
            Type expectMutRef = expectRef;         // &mut Vec<T> / &mut Box<T>
            expectMutRef.isMut = true;
            if (fnName == "new") {
                if (typeName == "Vec") {
                    if (!node.args.empty()) semaError("Vec::new 不接受参数");
                    if (!callee->genericArg)
                        semaError("Vec::new 需要显式元素类型（Vec::<T>::new()）");
                    lastType_ = vecOf(elem);
                } else {
                    if (node.args.size() != 1) semaError("Box::new 需要 1 个参数");
                    evalExpr(node.args[0]);
                    if (!callee->genericArg) {
                        elem = lastType_;       // 没写 turbofish 就从实参推
                    } else if (!coercible(lastType_, elem)) {
                        semaError("Box::new 参数类型不匹配：期望 " + elem.toString()
                                  + "，实际 " + lastType_.toString());
                    }
                    lastType_ = boxOf(elem);
                }
                lastDiverges_ = false;
                return;
            }
            if (fnName == "clone") {
                if (node.args.size() != 1)
                    semaError(typeName + "::clone 需要 1 个参数");
                evalExpr(node.args[0]);
                if (!callee->genericArg) {
                    // 没写 turbofish：实参必须是 &Vec<_>/&Box<_>，元素类型从实参取
                    Type t = lastType_;
                    if (t.kind == TypeKind::Ref) t = *t.elem;
                    if ((typeName == "Vec" && !isVecType(t))
                        || (typeName == "Box" && !isBoxType(t)))
                        semaError(typeName + "::clone 的参数必须是 &" + typeName + "<T>");
                    lastType_ = t;
                } else {
                    // 容器 clone 传导到元素：Vec<不可Clone> 的 clone 要拦
                    //（rej-even-empty-container-clone-requires-element-clone）
                    if (!isCloneableType(elem))
                        semaError(typeName + "<" + elem.toString()
                                  + "> 不可克隆（元素类型不支持 Clone）");
                    if (!coercible(lastType_, expectRef))
                        semaError(typeName + "::clone 参数类型不匹配：期望 &"
                                  + containerT.toString() + "，实际 "
                                  + lastType_.toString());
                    lastType_ = containerT;
                }
                lastDiverges_ = false;
                return;
            }
            // Vec 方法的关联函数形式：Vec::<T>::len(&v)、
            // Vec::<T>::push(&mut v, x)、Vec::<T>::remove(&mut v, i)、
            // Vec::<T>::is_empty(&v)——第一个实参就是 receiver
            if (typeName == "Vec"
                && (fnName == "len" || fnName == "is_empty"
                    || fnName == "push" || fnName == "remove")) {
                bool needsMut = (fnName == "push" || fnName == "remove");
                size_t expectArgs = (fnName == "push" || fnName == "remove") ? 2 : 1;
                if (node.args.size() != expectArgs)
                    semaError("Vec::" + fnName + " 需要 "
                              + std::to_string(expectArgs) + " 个参数");
                evalExpr(node.args[0]);
                if (!coercible(lastType_, needsMut ? expectMutRef : expectRef))
                    semaError("Vec::" + fnName + " 的第一个参数必须是 &"
                              + std::string(needsMut ? "mut " : "")
                              + containerT.toString() + "，实际 "
                              + lastType_.toString());
                if (fnName == "push") {
                    evalExpr(node.args[1]);
                    if (!coercible(lastType_, elem))
                        semaError("Vec::push 参数类型不匹配：期望 " + elem.toString()
                                  + "，实际 " + lastType_.toString());
                    checkReborrowUse(node.args[1]);
                    lastType_ = Type{TypeKind::Unit};
                } else if (fnName == "remove") {
                    evalExpr(node.args[1]);
                    if (rxDialect_) {
                        if (lastType_.kind != TypeKind::Usize
                            && lastType_.kind != TypeKind::IntLit)
                            semaError("Vec::remove 的下标必须是 usize，实际 "
                                      + lastType_.toString());
                    }
                    lastType_ = elem;
                } else if (fnName == "len") {
                    lastType_ = Type{TypeKind::Usize};
                } else {
                    lastType_ = Type{TypeKind::Bool};
                }
                lastDiverges_ = false;
                return;
            }
            semaError(typeName + " 不支持关联函数 " + fnName);
        }
        auto it = symtab_.implMethods.find(typeName);
        // 带 turbofish 的调用（f::<i32>()、Type::f::<i32>()）：Rx 的函数没有类型参数
        if (callee->genericArg)
            semaError("函数 " + callee->name + " 不支持类型参数");
        if (it != symtab_.implMethods.end()) {
            for (auto& m : it->second) {
                if (m->name == fnName) {
                    Type ret = m->returnType ? *m->returnType : Type{TypeKind::Unit};
                    if (m->isMethod) {
                        // 关联函数形式调方法：第一个实参是 receiver
                        //（Counter::add(&mut c, 2)，self 不在 params 里）
                        if (node.args.size() != m->params.size() + 1)
                            semaError("方法 " + callee->name + " 参数个数不对：期望 "
                                      + std::to_string(m->params.size() + 1)
                                      + " 个（含 receiver），实际 "
                                      + std::to_string(node.args.size()) + " 个");
                        Type selfT;
                        if (m->selfByValue) {
                            selfT.kind = TypeKind::Named;
                            selfT.name = typeName;
                        } else {
                            selfT.kind = TypeKind::Ref;
                            selfT.isMut = m->selfMut;
                            Type inner;
                            inner.kind = TypeKind::Named;
                            inner.name = typeName;
                            selfT.elem = std::make_shared<Type>(inner);
                        }
                        evalExpr(node.args[0]);
                        if (!coercible(lastType_, selfT))
                            semaError("方法 " + callee->name
                                      + " 的 receiver 类型不匹配：期望 "
                                      + selfT.toString() + "，实际 "
                                      + lastType_.toString());
                        for (size_t i = 0; i < m->params.size(); ++i) {
                            evalExpr(node.args[i + 1]);
                            if (!coercible(lastType_, m->params[i].type))
                                semaError("方法 " + callee->name + " 第 "
                                          + std::to_string(i + 2)
                                          + " 个参数类型不匹配：期望 "
                                          + m->params[i].type.toString() + "，实际 "
                                          + lastType_.toString());
                        }
                        lastType_ = ret;
                        lastDiverges_ = false;
                        return;
                    }
                    std::vector<Type> params;
                    for (auto& p : m->params) params.push_back(p.type);
                    checkArgs(callee->name, params, ret);
                    lastDiverges_ = false;
                    return;
                }
            }
        }
        // 派生 Clone 的关联形式 Type::clone(&v)：用户没在 impl 里自定义
        // clone 方法时，derive(Clone) 自动生成一个（bypass 点调用查找）
        if (fnName == "clone" && node.args.size() == 1) {
            Symbol* s = symtab_.lookupType(typeName);
            if (s && s->isClone) {
                evalExpr(node.args[0]);
                Type expectRef;
                expectRef.kind = TypeKind::Ref;
                Type named;
                named.kind = TypeKind::Named;
                named.name = typeName;
                expectRef.elem = std::make_shared<Type>(named);
                if (coercible(lastType_, expectRef)) {
                    lastType_ = named;
                    lastDiverges_ = false;
                    return;
                }
                semaError(typeName + "::clone 参数类型不匹配：期望 &" + typeName
                          + "，实际 " + lastType_.toString());
            }
        }
        semaError("类型 " + typeName + " 没有关联函数 " + fnName);
    }
    Symbol* sym = symtab_.lookup(callee->name);
    if (!sym || sym->kind != SymbolKind::Function)
        semaError("未声明的函数：" + callee->name);
    if (callee->genericArg)
        semaError("函数 " + callee->name + " 不支持类型参数");
    checkArgs(callee->name, sym->paramTypes, sym->type);
    lastDiverges_ = false;
}

// 字段访问 p.x：object 必须是 Named 类型；按类型名查 StructType 符号，
// 回指 structDef 找字段定义，字段类型写进 lastType_
void BodyChecker::visit(FieldExpr& node) {
    evalExpr(node.object);
    Type objT = lastType_;
    // 自动解引用：&S / Box<S> 都能直接取字段
    while (objT.kind == TypeKind::Ref || isBoxType(objT)) objT = *objT.elem;
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
    Type origT = lastType_;   // receiver 原始类型（clone 的回退判定要用）
    Type recvT = origT;
    // 自动解引用：&Point / Box<Point> 的 receiver 也能调 Point 的方法
    //（&self 本来就是这么传的；Box 是唯一拥有者，方法调用穿透它）
    while (recvT.kind == TypeKind::Ref || isBoxType(recvT)) recvT = *recvT.elem;
    // 内建方法：数组 len()；字符串 len()/is_empty()/to_string()；整型 to_string()
    // （课程测试用到的标准库方法极简子集，返回类型直接写死）
    if (recvT.kind == TypeKind::Array && node.method == "len") {
        if (!node.args.empty()) semaError("len() 不接受参数");
        lastType_ = Type{TypeKind::Usize};
        lastDiverges_ = false;
        return;
    }
    // Vec<T> 内建方法：push/remove/len/is_empty
    //（clone 不在此拦截，走 implMethods 之后的通用 clone 兜底）
    if (isVecType(recvT) && node.method != "clone") {
        if (node.method == "push") {
            // receiver 必须可写：变量是可变绑定，或经 &mut 引用层；
            // receiver 是存放在某个位置里的 &mut 值时还要可变重借
            //（Vec<&mut Vec<T>> 的元素不绕过外层 Vec 的可变性）
            if (origT.kind == TypeKind::Ref) {
                if (!origT.isMut || !reborrowOk(node.receiver))
                    semaError("Vec::push 的 receiver 必须可变");
            } else if (!placeWritable(node.receiver)) {
                semaError("Vec::push 的 receiver 必须是可变位置");
            }
            if (node.args.size() != 1) semaError("Vec::push 需要 1 个参数");
            evalExpr(node.args[0]);
            if (!coercible(lastType_, *recvT.elem))
                semaError("Vec::push 参数类型不匹配：期望 " + recvT.elem->toString()
                          + "，实际 " + lastType_.toString());
            checkReborrowUse(node.args[0]);
            lastType_ = Type{TypeKind::Unit};
        } else if (node.method == "remove") {
            if (origT.kind == TypeKind::Ref) {
                if (!origT.isMut || !reborrowOk(node.receiver))
                    semaError("Vec::remove 的 receiver 必须可变");
            } else if (!placeWritable(node.receiver)) {
                semaError("Vec::remove 的 receiver 必须是可变位置");
            }
            if (node.args.size() != 1) semaError("Vec::remove 需要 1 个参数");
            evalExpr(node.args[0]);
            // 同数组下标：Rx 方言要求 usize（字面量 IntLit 可推导）
            if (rxDialect_) {
                if (lastType_.kind != TypeKind::Usize
                    && lastType_.kind != TypeKind::IntLit)
                    semaError("Vec::remove 的参数必须是 usize，实际 "
                              + lastType_.toString());
            } else if (!isInt(lastType_)) {
                semaError("Vec::remove 的参数必须是整型，实际 " + lastType_.toString());
            }
            lastType_ = *recvT.elem;
        } else if (node.method == "len") {
            if (!node.args.empty()) semaError("len() 不接受参数");
            lastType_ = Type{TypeKind::Usize};
        } else if (node.method == "is_empty") {
            if (!node.args.empty()) semaError("is_empty() 不接受参数");
            lastType_ = Type{TypeKind::Bool};
        } else {
            semaError("Vec 不支持方法 " + node.method);
        }
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
    // .clone() 兜底（impl 里没有用户自定义 clone 方法时）：
    //   - 沿共享引用剥到的类型本身可克隆（标量/数组/Vec/Box/derive(Clone) 的
    //     struct/String）→ 返回该类型（view.clone() 克隆的是容器 Box，不是引用）
    //   - 剥到底仍不可克隆（如没 derive(Clone) 的 struct）→ 克隆的是
    //     最外层共享引用本身（&T 总是可克隆的），返回 &T
    auto tryCloneFallback = [&]() {
        if (node.method != "clone" || !node.args.empty()) return false;
        Type t = origT;
        while (t.kind == TypeKind::Ref && !t.isMut) t = *t.elem;
        if (isCloneableType(t)) {
            lastType_ = t;
            lastDiverges_ = false;
            return true;
        }
        if (origT.kind == TypeKind::Ref && !origT.isMut) {
            lastType_ = origT;
            lastDiverges_ = false;
            return true;
        }
        return false;
    };
    if (recvT.kind != TypeKind::Named) {
        // 非 struct 的 .clone()（标量/数组/引用）走内建规则
        if (tryCloneFallback()) return;
        semaError("对非 struct 类型调方法：" + recvT.toString());
    }
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
                    if (!coercible(lastType_, m->params[i].type))
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
    // impl 里没有用户自定义 clone 时，走内建 clone 兜底
    if (tryCloneFallback()) return;
    semaError("类型 " + recvT.name + " 没有方法 " + node.method);
}

// 结构体字面量 Point { x: 1, y: 2 }：名字必须是已注册 struct；
// 字段不缺不多、每个初值类型匹配；类型为 Named(结构体名)。
// impl 方法里的 Self { ... } 指向当前 impl 的目标类型
void BodyChecker::visit(StructLiteralExpr& node) {
    std::string name = node.name;
    if (name == "Self") {
        if (currentImplType_.empty())
            semaError("Self 只能在 impl 块内使用");
        name = currentImplType_;
    }
    Symbol* sym = symtab_.lookupType(name);
    if (!sym || sym->kind != SymbolKind::StructType)
        semaError("未定义的 struct：" + node.name);
    if (node.fields.size() != sym->structDef->fields.size())
        semaError("struct " + name + " 字段个数不对：期望 "
                  + std::to_string(sym->structDef->fields.size()) + " 个，实际 "
                  + std::to_string(node.fields.size()) + " 个");
    for (auto& [fname, valueExpr] : node.fields) {
        const Type* fieldType = nullptr;
        for (auto& f : sym->structDef->fields) {
            if (f.name == fname) { fieldType = &f.type; break; }
        }
        if (!fieldType)
            semaError("struct " + name + " 没有字段 " + fname);
        evalExpr(valueExpr);
        if (!coercible(lastType_, *fieldType))
            semaError("struct " + name + " 字段 " + fname
                      + " 类型不匹配：期望 " + fieldType->toString()
                      + "，实际 " + lastType_.toString());
    }
    lastType_ = Type{TypeKind::Named};
    lastType_.name = name;
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
            // 元素类型逐个合并（[&mut a, &b] 的元素类型是 &i32）
            Type merged;
            if (lub(firstT, lastType_, merged)) {
                firstT = merged;
            } else {
                semaError("数组元素类型不一致：第 1 个是 " + firstT.toString()
                          + "，第 " + std::to_string(i + 1) + " 个是 "
                          + lastType_.toString());
            }
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
        // 重复形式 [v; N]：N > 1 时元素必须可 Copy（[Box::new(1); 2] 要拦）；
        // N == 1 是直接从值构造，不需要 Copy。这条是 Rx 方言的规则，
        // 旧规范允许重复任意元素类型（misc13 的 [ListNode::new(0); 20]）
        if (rxDialect_ && len != 1 && !isCopyType(elemT))
            semaError("数组重复形式 [v; N]（N > 1）要求元素类型可 Copy："
                      + elemT.toString());
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
    // 自动解引用：&[T; N]、&Vec<T>、Box<[T; N]>、Box<Vec<T>> 都能直接用下标
    while (lastType_.kind == TypeKind::Ref || isBoxType(lastType_))
        lastType_ = *lastType_.elem;
    Type elemT;
    size_t arrLen = 0;
    if (lastType_.kind == TypeKind::Array) {
        elemT = *lastType_.elem;        // 先存走元素类型
        arrLen = lastType_.arrayLen;    // 已知长度（0 = 未知）留给越界检查
    } else if (isVecType(lastType_)) {
        elemT = *lastType_.elem;        // Vec 长度运行期才知道，不做越界检查
    } else {
        semaError("对非数组/Vec 取下标：" + lastType_.toString());
    }
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

// ============================================================
// Clone/PartialEq 能力查询与位置可写性 / 可变重借
// ============================================================

// 类型是否可克隆：标量、共享引用（&T 本身 Copy）、元素可克隆的数组、
// String、元素可克隆的 Vec/Box、derive(Clone) 的 struct
bool BodyChecker::isCloneableType(const Type& t) {
    switch (t.kind) {
        case TypeKind::I32: case TypeKind::U32: case TypeKind::Isize:
        case TypeKind::Usize: case TypeKind::IntLit:
        case TypeKind::F32: case TypeKind::F64:
        case TypeKind::Bool: case TypeKind::Char:
            return true;
        case TypeKind::Ref:
            return !t.isMut;
        case TypeKind::Array:
            return isCloneableType(*t.elem);
        case TypeKind::Named:
            if (isVecType(t) || isBoxType(t))
                // 容器的 Clone 传导到元素：Vec<不可Clone> 也不可 Clone
                return !t.elem || isCloneableType(*t.elem);
            if (t.name == "String") return true;
            if (Symbol* s = symtab_.lookupType(t.name)) return s->isClone;
            return false;
        default:
            return false;
    }
}

// 类型是否可 Copy：标量、共享引用（&T 本身 Copy）、元素可 Copy 的数组、
// derive(Copy) 的 struct；Vec/Box/String 等堆/拥有型类型不可 Copy
bool BodyChecker::isCopyType(const Type& t) {
    switch (t.kind) {
        case TypeKind::I32: case TypeKind::U32: case TypeKind::Isize:
        case TypeKind::Usize: case TypeKind::IntLit:
        case TypeKind::F32: case TypeKind::F64:
        case TypeKind::Bool: case TypeKind::Char:
            return true;
        case TypeKind::Ref:
            return !t.isMut;
        case TypeKind::Array:
            return isCopyType(*t.elem);
        case TypeKind::Named:
            if (isVecType(t) || isBoxType(t) || t.name == "String") return false;
            if (Symbol* s = symtab_.lookupType(t.name)) return s->isCopy;
            return false;
        default:
            return false;
    }
}

// 类型是否支持 ==/!=：标量、enum、String、元素支持的数组与 Vec/Box 可以；
// 用户 struct 需要 derive(PartialEq)
bool BodyChecker::hasPartialEqType(const Type& t) {
    switch (t.kind) {
        case TypeKind::I32: case TypeKind::U32: case TypeKind::Isize:
        case TypeKind::Usize: case TypeKind::IntLit:
        case TypeKind::F32: case TypeKind::F64:
        case TypeKind::Bool: case TypeKind::Char:
            return true;
        case TypeKind::Array:
            return hasPartialEqType(*t.elem);
        case TypeKind::Named:
            if (isVecType(t) || isBoxType(t))
                return !t.elem || hasPartialEqType(*t.elem);
            if (t.name == "String") return true;
            if (Symbol* s = symtab_.lookupType(t.name))
                return s->hasPartialEq || s->kind == SymbolKind::EnumType;
            return false;
        default:
            return false;
    }
}

// 位置 e 能否被整体写入（赋值目标、&mut 借用、&mut 方法的 receiver）。
// 沿 Field/Index/解引用往根走：
//   - 遇到引用层：可变性由引用自身决定（&mut → 可写，& → 冻结）
//   - 穿过 Box/数组/Vec/struct 等值容器：继承容器的可写性
//   - 根变量：必须是可变绑定
bool BodyChecker::placeWritable(const ExprPtr& e) {
    if (auto* p = dynamic_cast<PathExpr*>(e.get())) {
        Symbol* sym = symtab_.lookup(p->name);
        return sym && sym->isMut;
    }
    if (auto* f = dynamic_cast<FieldExpr*>(e.get())) {
        evalExpr(f->object);
        if (lastType_.kind == TypeKind::Ref) return lastType_.isMut;
        return placeWritable(f->object);
    }
    if (auto* ix = dynamic_cast<IndexExpr*>(e.get())) {
        evalExpr(ix->array);
        if (lastType_.kind == TypeKind::Ref) return lastType_.isMut;
        return placeWritable(ix->array);
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) {
        if (u->op == "*") {
            evalExpr(u->operand);
            if (lastType_.kind == TypeKind::Ref) return lastType_.isMut;
            // 写 Box 堆上的内容要求 Box 拥有者的位置可写
            if (isBoxType(lastType_)) return placeWritable(u->operand);
            return false;
        }
    }
    return true;   // 函数调用等临时值：本身就是可写的
}

// 存放在容器里的 &mut 值能否被可变使用的容器侧判定：
//   - 引用层（&mut Vec<...> 之类）：由引用自身可变性决定
//   - Box：穿透（唯一拥有者，保持内部 &mut 的写能力）
//   - Vec：不绕过——要求 Vec 位置本身可写
//   - 数组/struct：保持——递归看容器位置本身（Vec 层在递归里会被拦）
bool BodyChecker::reborrowContainerOk(const ExprPtr& expr, const Type& t) {
    if (t.kind == TypeKind::Ref) return t.isMut;
    if (isBoxType(t)) return reborrowContainerOk(expr, *t.elem);
    if (isVecType(t)) return placeWritable(expr);
    return reborrowOk(expr);
}

// 存放在位置 e 里的 &mut 值能否被可变使用（*e 写入、&mut *e、
// 或把 e 当 &mut 右值再借出）
bool BodyChecker::reborrowOk(const ExprPtr& e) {
    if (dynamic_cast<PathExpr*>(e.get())) return true;   // 变量：读它的值总允许
    if (auto* f = dynamic_cast<FieldExpr*>(e.get())) {
        evalExpr(f->object);
        return reborrowContainerOk(f->object, lastType_);
    }
    if (auto* ix = dynamic_cast<IndexExpr*>(e.get())) {
        evalExpr(ix->array);
        return reborrowContainerOk(ix->array, lastType_);
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) {
        if (u->op == "*") {
            evalExpr(u->operand);
            // 外层是共享引用 → 冻结（**q，q: &&mut）；
            // Box → 看这个 Box 本身是怎么到达的（&Box 后面的 Box 是只读的）
            if (lastType_.kind == TypeKind::Ref) return lastType_.isMut;
            if (isBoxType(lastType_)) return reborrowOk(u->operand);
            return true;
        }
    }
    return true;
}

void BodyChecker::checkReborrowUse(const ExprPtr& e) {
    if (dynamic_cast<PathExpr*>(e.get())) return;
    bool placeShape = dynamic_cast<FieldExpr*>(e.get())
        || dynamic_cast<IndexExpr*>(e.get());
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get()))
        placeShape = placeShape || u->op == "*";
    if (!placeShape) return;
    auto it = exprTypes_.find(e.get());
    if (it != exprTypes_.end() && it->second.kind == TypeKind::Ref
        && it->second.isMut && !reborrowOk(e))
        semaError("从不可变位置取出 &mut 引用");
}
