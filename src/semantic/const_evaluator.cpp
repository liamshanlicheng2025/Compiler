#include "semantic/const_evaluator.h"

#include "semantic/semantic_error.h"

#include <algorithm>
#include <cctype>
#include <cstring>

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
    bool bareIdent = std::all_of(t.arrayLenExpr.begin(), t.arrayLenExpr.end(),
                                 [](char c){ return std::isalnum(c) || c == '_'; });
    if (bareIdent) {
        Symbol* s = symtab_.lookup(t.arrayLenExpr);
        if (!s || s->kind != SymbolKind::Const || !s->hasConstValue)
            semaError("数组长度必须是常量：" + t.arrayLenExpr);
        t.arrayLen = static_cast<size_t>(s->constValue);
        return;
    }
    // 复杂常量表达式（如 ARR - 1）：尝试编译期求值；求不出则留 0 作长度通配
    long long v;
    if (constEval(t.arrayLenExpr, v)) t.arrayLen = static_cast<size_t>(v);
}
