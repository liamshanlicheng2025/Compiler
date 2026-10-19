#include "semantic/semantic_checker.h"

#include "semantic/semantic_error.h"
#include <iostream>

static bool isInt(const Type& t){
    return t.kind == TypeKind::I32 || t.kind == TypeKind::U32 || t.kind == TypeKind::Isize
        || t.kind == TypeKind::Usize || t.kind == TypeKind::IntLit;
}

// 整数字面量归一化：落到符号表/数组元素等"存起来"的地方时定版为 I32
static void normalizeIntLit(Type& t){
    if (t.kind == TypeKind::IntLit) t.kind = TypeKind::I32;
}

// 解析数组长度：递归处理嵌套数组/引用
void SemanticChecker::resolveArrayLens(Type& t) {
    if (t.kind == TypeKind::Ref && t.elem) { resolveArrayLens(*t.elem); return; }
    if (t.kind != TypeKind::Array) return;
    if (t.elem) resolveArrayLens(*t.elem);
    if (t.arrayLen != 0 || t.arrayLenExpr.empty()) return;
    // 单个标识符：必须是带编译期值的 const
    bool bareIdent = !t.arrayLenExpr.empty()
        && std::all_of(t.arrayLenExpr.begin(), t.arrayLenExpr.end(),
                       [](char c){ return std::isalnum(c) || c == '_'; });
    if (bareIdent) {
        Symbol* s = symtab_.lookup(t.arrayLenExpr);
        if (!s || s->kind != SymbolKind::Const || !s->hasConstValue)
            semaError("数组长度必须是常量：" + t.arrayLenExpr);
        t.arrayLen = static_cast<size_t>(s->constValue);
    }
    // 复杂常量表达式（如 ARR - 1）：尝试编译期求值（basic35）；
    // 求不出（含变量/函数调用等）则留 0 作长度通配
    long long v;
    if (constEval(t.arrayLenExpr, v)) t.arrayLen = static_cast<size_t>(v);
}

// ---- 编译期常量表达式求值：仅 + - * / % 、括号、整数字面量、const 名 ----
// 递归下降，用一个下标游标扫字符串；任何意外都返回 false
namespace {
struct ConstParser {
    const std::string& s;
    size_t i = 0;
    SymbolTable& symtab;
    bool ok = true;

    void skipWs() { while (i < s.size() && std::isspace(s[i])) ++i; }

    long long parseExpr() {                      // expr := term (('+'|'-') term)*
        long long v = parseTerm();
        while (ok) {
            skipWs();
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                char op = s[i++];
                long long r = parseTerm();
                v = (op == '+') ? v + r : v - r;
            } else break;
        }
        return v;
    }
    long long parseTerm() {                      // term := factor (('*'|'/'|'%') factor)*
        long long v = parseFactor();
        while (ok) {
            skipWs();
            if (i < s.size() && (s[i] == '*' || s[i] == '/' || s[i] == '%')) {
                char op = s[i++];
                long long r = parseFactor();
                if ((op == '/' || op == '%') && r == 0) { ok = false; return 0; }
                v = (op == '*') ? v * r : (op == '/' ? v / r : v % r);
            } else break;
        }
        return v;
    }
    long long parseFactor() {                    // factor := 数字 | 名字 | '(' expr ')' | '-' factor
        skipWs();
        if (i >= s.size()) { ok = false; return 0; }
        if (s[i] == '-') { ++i; return -parseFactor(); }
        if (s[i] == '(') {
            ++i;
            long long v = parseExpr();
            skipWs();
            if (i >= s.size() || s[i] != ')') { ok = false; return 0; }
            ++i;
            return v;
        }
        if (std::isdigit(s[i])) {
            size_t j = i;
            while (j < s.size() && std::isdigit(s[j])) ++j;
            long long v = std::stoll(s.substr(i, j - i));
            i = j;
            return v;
        }
        if (std::isalpha(s[i]) || s[i] == '_') {
            size_t j = i;
            while (j < s.size() && (std::isalnum(s[j]) || s[j] == '_')) ++j;
            std::string name = s.substr(i, j - i);
            i = j;
            Symbol* sym = symtab.lookup(name);
            if (!sym || sym->kind != SymbolKind::Const || !sym->hasConstValue) {
                ok = false;
                return 0;
            }
            return sym->constValue;
        }
        ok = false;
        return 0;
    }
};
} // namespace

bool SemanticChecker::constEval(const std::string& expr, long long& out) {
    ConstParser p{expr, 0, symtab_, true};
    long long v = p.parseExpr();
    p.skipWs();
    if (!p.ok || p.i != expr.size()) return false;
    out = v;
    return true;
}

// 数组长度等场景需要的编译期求值：仅支持整数字面量（带后缀也可）
static bool evalIntLiteral(const ExprPtr& e, long long& out) {
    auto* lit = dynamic_cast<Literal*>(e.get());
    if (!lit || lit->kind != LiteralKind::Int) return false;
    std::string d = lit->text;
    for (const char* suf : {"usize", "isize", "i32", "u32"}) {
        auto p = d.rfind(suf);
        if (p != std::string::npos && p + std::strlen(suf) == d.size()) {
            d = d.substr(0, p);
            break;
        }
    }
    try { out = std::stoll(d, nullptr, 0); return true; } catch (...) { return false; }
}

void SemanticChecker::run(const CratePtr& crate) {
    crate->accept(*this);
}

void SemanticChecker::visit(Crate& node) {
    // 顶层条目里只有函数有"体"要检查；const/static 的初值也要查
    for (auto& item : node.items) item->accept(*this);
}

void SemanticChecker::visit(Function& node) {
    // 记录返回类型（嵌套函数会覆盖它，进出时保存/恢复）、开作用域、
    // 注册 self（方法）和参数、查函数体、收尾比对
    Type savedRetType = currentFnRetType_;
    currentFnRetType_ = node.returnType ? *node.returnType : Type{TypeKind::Unit};
    resolveArrayLens(currentFnRetType_);   // fn make_table(n) -> [[i32; n]; n] 之类要拦
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
        // 修正：可变参数（fn f(mut n: i32)）的 isMut 由 builder 从 pattern 取出
        Type pt = p.type;
        resolveArrayLens(pt);              // 参数里的数组长度同样要解析（basic8）
        Symbol sym{SymbolKind::Variable, p.name, pt, p.isMut};
        symtab_.insert(sym);
    }
    node.body->accept(*this);
    if (!lastType_.equal(currentFnRetType_)) semaError("函数 " + node.name + " 返回类型不匹配：声明 " + currentFnRetType_.toString() + "，实际 " + lastType_.toString());
    symtab_.exitScope();
    currentFnRetType_ = savedRetType;
}

void SemanticChecker::visit(Block& node) {
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
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
        lastStmtType = lastType_;   // 语句的类型（ExprStmt 会留下其中表达式的类型）
    }
    if (node.tailExpr) {
        node.tailExpr->accept(*this);   // 块的类型 = 尾表达式类型
    } else {
        // 末尾是"无分号的带块表达式语句"（if/match/loop/裸块）时，它就是块的值
        // （basic22：函数体末尾的 if (k == p) { a[p] } else ... ）
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
        } else if (lastStmtType.kind == TypeKind::Never) {
            // 末尾语句发散（return/break）：块类型是 Never，兼容任何声明返回类型
            lastType_ = lastStmtType;
        } else {
            lastType_ = Type{TypeKind::Unit};
        }
    }
    symtab_.exitScope();
}

// let 声明：确定变量类型（标注优先，无标注则从初值推导），
// 检查标注与初值匹配，然后注册进当前作用域
void SemanticChecker::visit(LetStmt& node) {
    Type varType;
    if (node.init) {
        node.init->accept(*this);           // 初值求类型，结果在 lastType_
        Type initType = lastType_;          // 立刻存走——之后的 visit 会覆盖它
        if (node.type) {
            Type ann = *node.type;
            resolveArrayLens(ann);         // 标注里的数组长度（[i32; ARR] 等）
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
        resolveArrayLens(varType);
    }
    Symbol sym{SymbolKind::Variable, node.name, varType, node.isMut};
    normalizeIntLit(sym.type);   // 存进符号表前定版：let x = 1; → x 是 i32
    // Rust 允许同作用域 let 遮蔽（c2：同名 let mut 声明两次）；重复定义检查
    // 只针对函数/类型/常量等顶层名字（DeclCollector 那边仍报错）
    symtab_.insertOrShadow(sym);
}

void SemanticChecker::visit(ExprStmt& node) {
    if (node.expr) node.expr->accept(*this);   // 语句位置的表达式，类型丢弃
}

void SemanticChecker::visit(Literal& node) {
    // 种类在 builder 里已标好（LiteralKind），这里一对一翻译成类型
    switch (node.kind){
        case LiteralKind::Int: {
            // 溢出检查（misc57）：剥掉类型后缀后按范围判断。
            // u32/usize 后缀上限是 u32 最大值（c3 的 2166136261u32 合法）；
            // 无后缀按 i32 判断，但一元 - 正下方允许 2147483648（i32 MIN，misc27）
            std::string digits = node.text;
            long long limit = 2147483647LL;
            for (const char* suf : {"usize", "isize", "i32", "u32"}) {
                auto p = digits.rfind(suf);
                if (p != std::string::npos && p + std::strlen(suf) == digits.size()) {
                    if (suf[0] == 'u') limit = 4294967295LL;   // u32/usize
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
            lastType_ = Type{TypeKind::IntLit};   // 未定型整数字面量
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
    }
}

// 二元运算：按 op 分五类——赋值 / 复合赋值 / 算术位运算 / 比较 / 逻辑
void SemanticChecker::visit(BinaryExpr& node) {
    node.lhs->accept(*this);
    Type lhsT = lastType_;              // 立刻存走——visit rhs 会覆盖 lastType_
    node.rhs->accept(*this);
    Type rhsT = lastType_;

    auto isLValue = [](const ExprPtr& e) {
        if (dynamic_cast<PathExpr*>(e.get()))  return true;
        if (dynamic_cast<FieldExpr*>(e.get())) return true;
        if (dynamic_cast<IndexExpr*>(e.get())) return true;
        if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) return u->op == "*";
        return false;
    };

    // 赋值可变性检查：沿 FieldExpr/IndexExpr 走到根上的变量名，查符号的 isMut。
    // （basic30/basic32：result[i][j] = ...、seq[0] = 99 对不可变变量要报错）
    auto checkAssignable = [&](const ExprPtr& e) {
        if (!isLValue(e)) semaError("赋值目标不可赋值");
        ExprPtr cur = e;
        while (true) {
            if (auto* p = dynamic_cast<PathExpr*>(cur.get())) {
                Symbol* sym = symtab_.lookup(p->name);
                if (sym && (!sym->isMut || sym->kind == SymbolKind::Const)) {
                    // 透过 &mut 引用改指向的内容合法（misc5 的 arr: &mut [T; 10]）；
                    // 但直接给引用变量本身赋值仍需 isMut（cur == e 即直接赋值）
                    bool throughMutRef = (cur != e) && sym->type.kind == TypeKind::Ref
                                         && sym->type.isMut;
                    if (!throughMutRef)
                        semaError("给不可变变量赋值：" + p->name);
                }
                return;
            }
            if (auto* f = dynamic_cast<FieldExpr*>(cur.get())) { cur = f->object; continue; }
            if (auto* ix = dynamic_cast<IndexExpr*>(cur.get())) { cur = ix->array; continue; }
            return;   // 解引用等形态：可变性由引用本身保证，不查
        }
    };

    // 修正：五类改用 else if 链（op 只会命中一类，互不相关）
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
    } else if (node.op == "+" || node.op == "-" || node.op == "*" || node.op == "/"
            || node.op == "%" || node.op == "&" || node.op == "|" || node.op == "^") {
        if (!isInt(lhsT) || !lhsT.equal(rhsT))
            semaError("算术/位运算要求两边都是相同整型：左 " + lhsT.toString()
                      + "，右 " + rhsT.toString());
        // 结果取具体的一侧（1 + x（u32）→ u32）；两侧都是字面量则保持 IntLit
        lastType_ = (lhsT.kind == TypeKind::IntLit) ? rhsT : lhsT;
    } else if (node.op == "<<" || node.op == ">>") {   // 移位：两边都整型即可，
        if (!isInt(lhsT) || !isInt(rhsT))              // 位数类型可与左操作数不同（c29）
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
}

// 路径表达式：查符号表，把名字的类型写进 lastType_
void SemanticChecker::visit(PathExpr& node) {
    // 多段路径：首段必须是已注册类型。enum 变体（Operation::Add）的类型 =
    // Named(枚举名)（misc11）；struct 关联函数当值用给 Unknown 兜底
    if (node.name.find("::") != std::string::npos) {
        std::string head = node.name.substr(0, node.name.find("::"));
        Symbol* s = symtab_.lookup(head);
        if (!s || (s->kind != SymbolKind::StructType && s->kind != SymbolKind::EnumType))
            semaError("未声明的名字：" + node.name);
        if (s->kind == SymbolKind::EnumType) {
            Type t; t.kind = TypeKind::Named; t.name = head;
            lastType_ = t;
        } else {
            lastType_ = Type{TypeKind::Unknown};
        }
        return;
    }
    Symbol* sym = symtab_.lookup(node.name);
    if (!sym) semaError("未声明的名字：" + node.name);
    // 修正：符号的种类是 SymbolKind（不是 TypeKind——那是类型的枚举）
    if (sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Const
        || sym->kind == SymbolKind::Static) {
        lastType_ = sym->type;
    } else {
        // Function / StructType 不当值用：给 Unknown 兜底（equal 对 Unknown 放行）
        lastType_ = Type{TypeKind::Unknown};
    }
}

// 一元运算：-（整型取负）、!（bool 取反）、&/&mut（引用）、*（解引用）
void SemanticChecker::visit(UnaryExpr& node) {
    // 一元 - 正下方的整数字面量允许取到 2147483648（合起来是 i32 MIN）
    bool savedAllow = allowI32MinLit_;
    if (node.op == "-")
        if (auto* lit = dynamic_cast<Literal*>(node.operand.get()))
            if (lit->kind == LiteralKind::Int) allowI32MinLit_ = true;
    node.operand->accept(*this);
    allowI32MinLit_ = savedAllow;
    Type t = lastType_;
    if (node.op == "-") {
        if (!isInt(t)) semaError("一元 - 要求整型，实际 " + t.toString());
        lastType_ = t;
    } else if (node.op == "!") {
        // bool 取反得 bool；整型的 ! 是按位取反（Rust 语义，c29），类型不变
        if (t.kind != TypeKind::Bool && !isInt(t))
            semaError("一元 ! 要求 bool 或整型，实际 " + t.toString());
        lastType_ = t;
    } else if (node.op == "&" || node.op == "&mut") {
        // 补全：&mut 要求操作数可写
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
}

// if 表达式：条件必须 bool；有 else 时两分支类型一致（值 = 分支类型），无 else 值为 ()
void SemanticChecker::visit(IfExpr& node) {
    if (!node.condGrouped) semaError("if 条件缺少括号");
    node.cond->accept(*this);
    if (lastType_.kind != TypeKind::Bool && lastType_.kind != TypeKind::Never)
        semaError("if 条件必须是 bool，实际 " + lastType_.toString());
    node.thenBlock->accept(*this);
    Type thenT = lastType_;
    if (node.elseBlock) {
        node.elseBlock->accept(*this);
        Type elseT = lastType_;
        if (!elseT.equal(thenT))
            semaError("if 两分支类型不一致：then " + thenT.toString()
                      + "，else " + elseT.toString());
        // 一侧发散（return/break）时，结果取另一侧的类型（return15）
        lastType_ = (thenT.kind == TypeKind::Never) ? elseT : thenT;
    } else {
        lastType_ = Type{TypeKind::Unit};
    }
}

// while 循环：条件必须 bool（Never 放行）；值为 ()。
// 体内的带值 break 不上传（Rust 不允许 while 带值 break），进出时保存/恢复
void SemanticChecker::visit(WhileExpr& node) {
    if (!node.condGrouped) semaError("while 条件缺少括号");
    node.cond->accept(*this);
    if (lastType_.kind != TypeKind::Bool && lastType_.kind != TypeKind::Never)
        semaError("while 条件必须是 bool，实际 " + lastType_.toString());
    ++loopDepth_;
    bool savedHas = hasLoopBreakValue_; Type savedT = loopBreakType_;
    bool savedAny = hasAnyBreak_;
    hasLoopBreakValue_ = false;
    hasAnyBreak_ = false;
    node.body->accept(*this);
    hasLoopBreakValue_ = savedHas; loopBreakType_ = savedT;
    hasAnyBreak_ = savedAny;
    --loopDepth_;
    lastType_ = Type{TypeKind::Unit};
}

// loop 无限循环：值 = 体内带值 break 的类型；无 break 时只能靠 return 离开，
// 类型是 Never（发散，兼容任何上下文，misc15）；有普通 break 则 ()
void SemanticChecker::visit(LoopExpr& node) {
    ++loopDepth_;
    bool savedHas = hasLoopBreakValue_; Type savedT = loopBreakType_;
    bool savedAny = hasAnyBreak_;
    hasLoopBreakValue_ = false;
    hasAnyBreak_ = false;
    node.body->accept(*this);
    if (hasLoopBreakValue_) lastType_ = loopBreakType_;
    else if (!hasAnyBreak_) lastType_ = Type{TypeKind::Never};
    else lastType_ = Type{TypeKind::Unit};
    hasLoopBreakValue_ = savedHas; loopBreakType_ = savedT;
    hasAnyBreak_ = savedAny;
    --loopDepth_;
}

// return：有值则与函数声明返回类型比对，无值则要求函数返回 ()；自身类型 Never（发散）
void SemanticChecker::visit(ReturnExpr& node) {
    if (node.value) {
        node.value->accept(*this);
        if (!lastType_.equal(currentFnRetType_))
            semaError("return 类型不匹配：函数声明返回 " + currentFnRetType_.toString()
                      + "，实际 " + lastType_.toString());
    } else {
        if (currentFnRetType_.kind != TypeKind::Unit)
            semaError("函数声明返回 " + currentFnRetType_.toString()
                      + "，但 return 没有返回值");
    }
    lastType_ = Type{TypeKind::Never};   // 发散：equal 对 Never 放行，兼容一切上下文
}

// break/continue：必须在循环内（loopDepth_ > 0）；自身类型 Never
// break 带值（break expr;）：记录值类型，由最近的 loop 取走作为循环的类型
void SemanticChecker::visit(BreakExpr& node) {
    if (loopDepth_ == 0) semaError("break 不在循环内");
    hasAnyBreak_ = true;
    if (node.value) {
        node.value->accept(*this);
        loopBreakType_ = lastType_;
        hasLoopBreakValue_ = true;
    }
    lastType_ = Type{TypeKind::Never};
}

void SemanticChecker::visit(ContinueExpr& node) {
    if (loopDepth_ == 0) semaError("continue 不在循环内");
    lastType_ = Type{TypeKind::Never};
}

// 函数调用：callee 必须是 PathExpr。两种形态：
//   add(1, 2)        → 符号表查 Function 符号，签名在 Symbol::paramTypes / type
//   Point::new(1, 2) → 名字含 "::"，去 implMethods[类型名] 里按方法名找，
//                      签名在该方法的 AST 节点（params / returnType）上
void SemanticChecker::visit(CallExpr& node) {
    auto* callee = dynamic_cast<PathExpr*>(node.callee.get());
    if (!callee) semaError("不支持的调用形式");
    auto checkArgs = [&](const std::string& fnName,
                         const std::vector<Type>& params, const Type& ret) {
        if (node.args.size() != params.size())
            semaError("函数 " + fnName + " 参数个数不对：期望 "
                      + std::to_string(params.size()) + " 个，实际 "
                      + std::to_string(node.args.size()) + " 个");
        for (size_t i = 0; i < node.args.size(); ++i) {
            node.args[i]->accept(*this);
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
            node.args[0]->accept(*this);
            Type t; t.kind = TypeKind::Named; t.name = "String";
            lastType_ = t;
            return;
        }
        // Self::method()：当前 impl 目标类型（misc29）
        if (typeName == "Self") typeName = currentImplType_;
        auto it = symtab_.implMethods.find(typeName);
        if (it != symtab_.implMethods.end()) {
            for (auto& m : it->second) {
                if (m->name == fnName) {
                    std::vector<Type> params;
                    for (auto& p : m->params) params.push_back(p.type);
                    Type ret = m->returnType ? *m->returnType : Type{TypeKind::Unit};
                    checkArgs(callee->name, params, ret);
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
}

// 字段访问 p.x：object 必须是 Named 类型；按类型名查 StructType 符号，
// 回指 structDef 找字段定义，字段类型写进 lastType_
void SemanticChecker::visit(FieldExpr& node) {
    node.object->accept(*this);
    Type objT = lastType_;
    while (objT.kind == TypeKind::Ref) {
        objT = *objT.elem;
    }
    if (objT.kind != TypeKind::Named)
        semaError("对非 struct 类型取字段：" + objT.toString());
    Symbol* sym = symtab_.lookup(objT.name);
    if (!sym || sym->kind != SymbolKind::StructType)
        semaError("类型 " + objT.name + " 不是 struct");
    if (!sym->structDef)
        semaError("内建类型 " + objT.name + " 没有字段可访问");
    for (auto& f : sym->structDef->fields) {
        if (f.name == node.field) {
            lastType_ = f.type;
            return;
        }
    }
    semaError("struct " + objT.name + " 没有字段 " + node.field);
}

// 方法调用 p.shift(3)：由 receiver 的类型定位 implMethods，找到方法后
// 比对实参（self 不在 params 里）；&mut self 方法要求 receiver 可写
void SemanticChecker::visit(MethodCallExpr& node) {
    node.receiver->accept(*this);
    Type recvT = lastType_;
    // 自动解引用：&Point 的 receiver 也能调 Point 的方法（&self 本来就是这么传的）
    while (recvT.kind == TypeKind::Ref) recvT = *recvT.elem;
    // 内建方法：数组 len()；字符串 len()/is_empty()/to_string()；整型 to_string()
    // （课程测试用到的标准库方法极简子集，返回类型直接写死）
    if (recvT.kind == TypeKind::Array && node.method == "len") {
        if (!node.args.empty()) semaError("len() 不接受参数");
        lastType_ = Type{TypeKind::Usize};
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
        return;
    }
    if (isInt(recvT) && node.method == "to_string") {
        Type t; t.kind = TypeKind::Named; t.name = "String";
        lastType_ = t;
        return;
    }
    if (recvT.kind != TypeKind::Named)
        semaError("对非 struct 类型调方法：" + recvT.toString());
    auto it = symtab_.implMethods.find(recvT.name);
    if (it != symtab_.implMethods.end()) {
        for (auto& m : it->second) {
            if (m->name == node.method) {
                if (node.args.size() != m->params.size())
                    semaError("方法 " + node.method + " 参数个数不对：期望 "
                              + std::to_string(m->params.size()) + " 个，实际 "
                              + std::to_string(node.args.size()) + " 个");
                for (size_t i = 0; i < node.args.size(); ++i) {
                    node.args[i]->accept(*this);
                    if (!lastType_.equal(m->params[i].type))
                        semaError("方法 " + node.method + " 第 "
                                  + std::to_string(i + 1)
                                  + " 个参数类型不匹配：期望 "
                                  + m->params[i].type.toString() + "，实际 "
                                  + lastType_.toString());
                }
                // &mut self 方法要求 receiver 可写：沿 Field/Index/解引用走到
                // 根变量查符号（self.heap.push() 在 &mut self 方法里合法——
                // 根 self 的 isMut 来自方法的 selfMut；misc33/expr34/expr38）
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
                    bool ok = s && (s->isMut
                        || (s->type.kind == TypeKind::Ref && s->type.isMut));
                    if (!ok)
                        semaError("&mut self 方法 " + node.method
                                  + " 的 receiver 必须是可变变量");
                }
                lastType_ = m->returnType ? *m->returnType : Type{TypeKind::Unit};
                return;
            }
        }
    }
    semaError("类型 " + recvT.name + " 没有方法 " + node.method);
}

// 结构体字面量 Point { x: 1, y: 2 }：名字必须是已注册 struct；
// 字段不缺不多、每个初值类型匹配；类型为 Named(结构体名)
void SemanticChecker::visit(StructLiteralExpr& node) {
    Symbol* sym = symtab_.lookup(node.name);
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
        valueExpr->accept(*this);
        if (!lastType_.equal(*fieldType))
            semaError("struct " + node.name + " 字段 " + fname
                      + " 类型不匹配：期望 " + fieldType->toString()
                      + "，实际 " + lastType_.toString());
    }
    lastType_ = Type{TypeKind::Named};
    lastType_.name = node.name;
}

// 数组字面量两种形态：列举 [1, 2, 3]（repeatValue 为空）与重复 [0; 5]
void SemanticChecker::visit(ArrayExpr& node) {
    if (!node.repeatValue) {
        // ---- 列举形式 ----
        if (node.elements.empty())
            semaError("空数组字面量无法推导类型");
        node.elements[0]->accept(*this);
        Type firstT = lastType_;
        for (size_t i = 1; i < node.elements.size(); ++i) {
            node.elements[i]->accept(*this);
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
    } else {
        // ---- 重复形式 [0; 5] ----
        node.repeatValue->accept(*this);
        Type elemT = lastType_;
        node.repeatCount->accept(*this);
        if (!isInt(lastType_))
            semaError("数组重复次数必须是整型，实际 " + lastType_.toString());
        // 次数是字面量时取具体数值；
        // 不是字面量则长度记 0（equal 比较数组时长度 0 只影响数组间比较，兜底用）
        size_t len = 0;
        if (auto* lit = dynamic_cast<Literal*>(node.repeatCount.get()))
            len = static_cast<size_t>(std::stoull(lit->text));
        Type arr;
        arr.kind = TypeKind::Array;
        arr.elem = std::make_shared<Type>(elemT);
        arr.arrayLen = len;
        lastType_ = arr;
    }
}

void SemanticChecker::visit(IndexExpr& node) {
    node.array->accept(*this);
    // 自动解引用：&[T; N] 也能直接用下标（autoref5）
    while (lastType_.kind == TypeKind::Ref) lastType_ = *lastType_.elem;
    if (lastType_.kind != TypeKind::Array)
        semaError("对非数组取下标：" + lastType_.toString());
    Type elemT = *lastType_.elem;        // 先存走元素类型
    size_t arrLen = lastType_.arrayLen;  // 已知长度（0 = 未知）留给越界检查
    node.index->accept(*this);
    if (!isInt(lastType_)) semaError("数组下标必须是整型");
    // 常量下标越界检查（basic15：b.grid[3][3]，grid 是 [[i32; 3]; 3]）
    long long idx;
    if (arrLen != 0 && evalIntLiteral(node.index, idx)
        && (idx < 0 || idx >= static_cast<long long>(arrLen)))
        semaError("数组下标越界：下标 " + std::to_string(idx)
                  + "，长度 " + std::to_string(arrLen));
    lastType_ = elemT;
}

void SemanticChecker::visit(ConstDef& node) {
    // 全局常量：初值类型必须等于标注类型
    //   node.value->accept(*this);
    //   if (!lastType_.equal(node.type)) semaError("const " + node.name + " 初值类型不匹配");
    // （常量求值留到 IR 阶段；这里只查类型。node.value 为空理论不可能，g4 要求必填）
    node.value -> accept(*this);
    if (!lastType_.equal(node.type)) semaError("const " + node.name + " 初值类型不匹配");
    // 初值是整数字面量时把编译期值记进符号（数组长度常量解析用，basic35）
    long long v;
    if (evalIntLiteral(node.value, v)) {
        if (Symbol* s = symtab_.lookup(node.name)) {
            s->constValue = v;
            s->hasConstValue = true;
        }
    }
}

void SemanticChecker::visit(StaticDef& node) {
    // 同 ConstDef：node.value->accept(*this)，与 node.type 比对
    node.value -> accept(*this);
    if (!lastType_.equal(node.type)) semaError("static " + node.name + " 初值类型不匹配");
}

// enum/match 课程不要求（助教 CR 确认）：AST 里仍可能有这些节点，
// 遇到直接跳过，不做检查
void SemanticChecker::visit(MatchExpr&) {}
void SemanticChecker::visit(EnumDef&) {}

// 语句级宏：println!/print! 的参数不解析（CodeGen 阶段处理 argsText），直接放行；
// 其他宏名不在课程范围内，报错
void SemanticChecker::visit(MacroStmt& node) {
    if (node.name != "println" && node.name != "print")
        semaError("不支持的宏：" + node.name);
}

// as 类型转换：检查子表达式后，结果类型就是目标类型（数值间转换直接放行）
void SemanticChecker::visit(CastExpr& node) {
    node.expr->accept(*this);
    lastType_ = node.target;
}

// 块内条目：嵌套 fn / 局部 const/static——注册进当前作用域并检查体
void SemanticChecker::visit(ItemStmt& node) {
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
        long long v;         // 局部 const 的编译期值（loop3 的 [bool; N]）
        if (evalIntLiteral(cd->value, v)) { sym.constValue = v; sym.hasConstValue = true; }
        if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
    } else if (auto sd = std::dynamic_pointer_cast<StaticDef>(node.item)) {
        sd->accept(*this);
        Symbol sym{SymbolKind::Static, sd->name, sd->type, sd->isMut};
        if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
    } else if (auto st = std::dynamic_pointer_cast<StructDef>(node.item)) {
        // 块内 struct 定义（c44/46/47）：注册类型符号进当前作用域
        Symbol sym;
        sym.kind = SymbolKind::StructType;
        sym.name = st->name;
        sym.structDef = st;
        if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
    }
}

// 顶层不会出现这些，防御性留空
void SemanticChecker::visit(StructDef&) {}
void SemanticChecker::visit(ImplBlock& node) {
    // impl 里的方法也是 Function，要逐个检查函数体
    currentImplType_ = node.typeName;
    for (auto& m : node.methods) m->accept(*this);
    currentImplType_.clear();
}
