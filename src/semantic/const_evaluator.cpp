#include "semantic/const_evaluator.h"

#include "semantic/semantic_error.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "symbol_table.h"

// ---- 常量表达式求值器：仅 + - * / %、括号、整数字面量、const 名 ----
// 递归下降，用一个下标游标扫字符串；任何意外都置 ok = false
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

bool ConstEvaluator::evalIntLiteral(const ExprPtr& e, long long& out) {
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

bool ConstEvaluator::constEval(const std::string& expr, long long& out) {
    ConstParser p{expr, 0, symtab_, true};
    long long v = p.parseExpr();
    p.skipWs();
    if (!p.ok || p.i != expr.size()) return false;
    out = v;
    return true;
}

void ConstEvaluator::resolveArrayLens(Type& t) {
    if (t.kind == TypeKind::Ref && t.elem) { resolveArrayLens(*t.elem); return; }
    if (t.kind != TypeKind::Array) return;
    if (t.elem) resolveArrayLens(*t.elem);
    if (t.arrayLen != 0 || t.arrayLenExpr.empty()) return;
    // 裸标识符：必须是带编译期值的 const（数组长度不能用运行期变量）
    // TODO（关联常量）："[i32; Config::N]" 这类限定名含 "::"，
    //   裸标识符判断会落空、字符串求值也失败 → 长度静默留 0。
    //   如需支持：检出 "::" 时按 head/tail 拆开后查 symtab_.implConsts
    bool bareIdent = std::all_of(t.arrayLenExpr.begin(), t.arrayLenExpr.end(),
                                 [](char c){ return std::isalnum(c) || c == '_'; });
    if (bareIdent) {
        Symbol* s = symtab_.lookup(t.arrayLenExpr);
        if (!s || s->kind != SymbolKind::Const || !s->hasConstValue)
            semaError("数组长度必须是常量：" + t.arrayLenExpr);
        // 数组长度常量必须是 usize 类型（const N: bool = true; 用作长度要拒）
        if (s -> type.kind != TypeKind::Usize)
            semaError("数组长度必须是 usize：" + t.arrayLenExpr
                      + "，实际类型 " + s->type.toString());
        t.arrayLen = static_cast<size_t>(s->constValue);
        return;
    }
    // 复杂常量表达式（如 ARR - 1）：尝试编译期求值；求不出则留 0 作长度通配
    long long v;
    if (constEval(t.arrayLenExpr, v)) t.arrayLen = static_cast<size_t>(v);
}

// ---- 统一预求值：查环 + 求值 + 缓存 ----

std::string ConstEvaluator::normalizePath(const std::string& name) const {
    if (name.rfind("Self::", 0) == 0 && !currentSelfType_.empty())
        return currentSelfType_ + name.substr(4);   // "Self::X" → "Config::X"
    return name;
}

void ConstEvaluator::evaluateAll(const CratePtr& crate) {
    for (auto& item : crate->items) {
        if (auto cd = std::dynamic_pointer_cast<ConstDef>(item))
            consts_[cd->name] = cd;
        // 关联常量：限定名 "Config::N" 作键，并记下所属类型供 Self 归一化
        if (auto im = std::dynamic_pointer_cast<ImplBlock>(item)) {
            for (auto& cd : im->consts) {
                std::string qname = im->typeName + "::" + cd->name;
                consts_[qname] = cd;
                selfTypeOf_[qname] = im->typeName;
            }
        }
    }
    std::unordered_map<std::string, int> state;   // 0=未访问 1=求值中 2=已完成
    for (auto& [name, cd] : consts_) {
        if (state[name] == 0) evalConst(name, state);
    }
}

// 三色 DFS 求值一个顶层 const（灰=求值中，撞灰即环；黑=已求值，结果在 cache_）。
// 检查点：
//   1. 初值引用的 const 正在求值中（灰）→ 常量依赖环
//   2. 无符号类型（u32/usize）算出负值 → 报错（const N: u32 = -1u32;）
//   3. 整型常量的初值求不出 → 初值里有非常量内容（如函数名 value）
long long ConstEvaluator::evalConst(const std::string& name,
                                    std::unordered_map<std::string, int>& state) {
    auto& def = consts_[name];
    if (!def -> value) {          // 无初值（g4 语法允许）：无可求，直接涂黑
        state[name] = 2;
        return 0;
    }
    // 求值前设置 Self 上下文（关联常量 → 所属类型名，顶层 const → 空），
    // 求值后恢复；semaError 抛异常的路径不用恢复（整个编译随即中止）
    std::string savedSelf = currentSelfType_;
    currentSelfType_ = selfTypeOf_.count(name) ? selfTypeOf_[name] : "";
    state[name] = 1;              // 涂灰：开始求值
    std::vector<std::string> deps;
    collectDeps(def -> value, deps);
    for (auto& dep : deps) {
        if (consts_.find(dep) == consts_.end()) {
            continue;             // 非顶层 const（如块内局部 const）：不归这里管
        }
        if (state[dep] == 1) {    // 检查 1：撞灰 = 依赖环
            semaError("常量依赖环：" + name + " -> " + dep);
        }
        if (state[dep] == 0) evalConst(dep, state);   // 先算依赖（后序）
    }
    // 走到这里依赖已全部求出、躺在 cache_ 里，可以算自己了
    long long v;
    if (evalConstExpr(def -> value, v)) {
        TypeKind k = def -> type.kind;
        if ((k == TypeKind::U32 || k == TypeKind::Usize) && v < 0) {   // 检查 2
            semaError("无符号常量不能为负：" + name);
        }
        cache_[name] = v;
        def -> constValue = v;          // 写回节点：关联常量经 implConsts 取值用
        def -> hasConstValue = true;
        if (Symbol* s = symtab_.lookup(name)) {   // 写进符号表，BodyChecker 直接用
            s -> constValue = v;
            s -> hasConstValue = true;
        }
    } else {
        TypeKind k = def -> type.kind;
        bool intTy;
        if (k == TypeKind::Isize || k == TypeKind::I32 || k == TypeKind::U32 || k == TypeKind::Usize) intTy = true;
        else intTy = false;
        if (intTy) {              // 检查 3：整型常量却求不出值
            semaError("const 初值不是常量表达式：" + name);
        }
        // 非整型常量（const B: bool = true; 等）：合法，不求值
    }
    state[name] = 2;              // 涂黑
    currentSelfType_ = savedSelf; // 恢复 Self 上下文（必须在所有 return 之前）
    if (cache_.count(name) == 0) return 0;
    return cache_[name];
}

// AST 递归求值常量表达式。返回"求出来没有"，值经输出参数 out 带出。
// 各分支检查内容：
//   Literal     —— 只认整数字面量（true/"str" 等返回 false，不算整型常量）
//   PathExpr    —— 只认已求值的顶层 const（查 cache_）；函数名/局部变量
//                  不在 cache_ 里，返回 false，由 evalConst 的检查 3 报错
//   BinaryExpr  —— 只认 + - * / %；除/模先查右操作数非 0；比较/逻辑运算
//                  结果不是整数，返回 false
//   UnaryExpr   —— 只认负号（!、*、& 都不是整型常量运算）
//   CastExpr    —— as 转换对整数值无影响，直接取子表达式的值
//   其余节点    —— 函数调用、if、块等都不是常量表达式，返回 false
bool ConstEvaluator::evalConstExpr(const ExprPtr& e, long long& out) {
    if (auto* lit = dynamic_cast<const Literal*>(e.get())) {
        if (lit -> kind == LiteralKind::Int && evalIntLiteral(e, out)) return true;
        return false;
    }
    if (auto* p = dynamic_cast<PathExpr*>(e.get())) {
        // 归一化后查 cache_：顶层 const 是裸名，关联常量是限定名 "Config::N"，
        // impl 初值里的 "Self::X" 由 normalizePath 按 currentSelfType_ 改写
        auto it = cache_.find(normalizePath(p -> name));
        if (it == cache_.end()) return false;
        out = it -> second;
        return true;
    }
    if (auto* b = dynamic_cast<BinaryExpr*>(e.get())) {
        long long l, r;
        if (!evalConstExpr(b -> lhs, l) || !evalConstExpr(b -> rhs, r)) return false;
        if (b -> op == "+") {
            out = l + r;
            return true;
        }
        if (b -> op == "-") {
            out = l - r;
            return true;
        }
        if (b -> op == "*") {
            out = l * r;
            return true;
        }
        if (b -> op == "/" || b -> op == "%") {
            if (r == 0) return false;
            if (b -> op == "/") out = l / r;
            else out = l % r;
            return true;
        }
        return false;
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) {
        if (u -> op != "-") return false;
        long long v;
        if (!evalConstExpr(u -> operand, v)) return false;
        out = -v;
        return true;
    }
    if (auto* c = dynamic_cast<CastExpr*>(e.get())) {
        return evalConstExpr(c -> expr, out);
    }
    return false;
}

// 收集表达式里引用的顶层 const 名（给 evalConst 建依赖图用）。
// 各分支检查内容：
//   PathExpr    —— 先 normalizePath 归一化（"Self::X" → "Config::X"）；
//                  归一化后的名字在 consts_ 里才算依赖（顶层裸名、
//                  关联常量限定名都已被 evaluateAll 收进 consts_）
//   BinaryExpr / UnaryExpr / CastExpr —— 递归子表达式
//   Literal 及其余节点 —— 不引用任何名字，不贡献依赖
void ConstEvaluator::collectDeps(const ExprPtr& e, std::vector<std::string>& out) {
    if (auto* p = dynamic_cast<const PathExpr*>(e.get())) {
        std::string q = normalizePath(p -> name);
        if (consts_.find(q) != consts_.end()) {
            out.push_back(q);
        }
        return;
    }
    if (auto* b = dynamic_cast<BinaryExpr*>(e.get())) {
        collectDeps(b -> lhs, out);
        collectDeps(b -> rhs, out);
        return;
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) {
        collectDeps(u -> operand, out);
        return;
    }
    if (auto* c = dynamic_cast<CastExpr*>(e.get())) {
        collectDeps(c -> expr, out);
        return;
    }
}
