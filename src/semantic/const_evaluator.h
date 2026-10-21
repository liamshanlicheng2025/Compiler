#pragma once

// ============================================================
// ConstEvaluator：受限常量上下文的编译期求值
//
// 负责的常量场景：
//   - const 初值：整数字面量直接记进 Symbol::constValue（符号表）
//   - 数组类型长度：[i32; ARR - 1] 这类，见 resolveArrayLens
//   - 数组重复长度 [v; N] 的次数、常量下标越界检查（evalIntLiteral）
//
// TODO（升级方向，对照 8 阶段模型的"常量求值"阶段）：
//   1. 依赖图：const A = B + 1; 这类声明先建"名字 → 依赖名字集"的图；
//   2. 查环：DFS 三色标记（未访问/访问中/已完成），访问中再遇即报"常量依赖环"；
//   3. 缓存：cache_[name] 存已求出的值，同一常量不求第二次；
//   4. 统一入口：在 CrateIndex 登记完 const 后、BodyChecker 跑之前
//      把所有 const 预求值一遍（现在是"用到才算"的惰性模式）。
// ============================================================

#include "ast/ast.h"
#include "ast/type.h"
#include "semantic/symbol_table.h"

class ConstEvaluator {
public:
    explicit ConstEvaluator(SymbolTable& symtab) : symtab_(symtab) {}

    // 整数字面量求值（带类型后缀也可）；不是整数字面量返回 false
    static bool evalIntLiteral(const ExprPtr& e, long long& out);

    // 常量表达式求值：+ - * / %、括号、整数字面量、const 名；求不出返回 false
    bool constEval(const std::string& expr, long long& out);

    // 解析类型里的数组长度（递归处理嵌套数组/引用）
    void resolveArrayLens(Type& t);

private:
    SymbolTable& symtab_;
    // TODO（升级）：std::unordered_map<std::string, long long> cache_;
};
