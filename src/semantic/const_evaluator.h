#pragma once

// ============================================================
// ConstEvaluator：受限常量上下文的编译期求值（8 阶段模型的第 4 阶段）
//
// 负责的常量场景：
//   - const 初值：编译期算出，记进 Symbol::constValue（符号表）
//   - 数组类型长度：[i32; ARR - 1] 这类，见 resolveArrayLens
//   - 数组重复长度 [v; N] 的次数、常量下标越界检查（evalIntLiteral）
//
// 两种表达式来源：
//   - const 初值是结构化 ExprPtr → evalConstExpr 递归求值
//   - 数组长度存在 Type::arrayLenExpr 里（原文字符串）→ constEval 字符串解析
//
// 统一预求值（evaluateAll，main 里 CrateIndex 之后、BodyChecker 之前调用）：
//   1. 收集顶层 const 进 consts_（名字 → 定义）
//   2. 对每个 const 跑 evalConst 三色 DFS：
//      灰 = 求值中（撞灰 = 依赖环，报错）；黑 = 已求值（cache_ 直接拿）
//      DFS 后序天然是依赖顺序：算自己时依赖已在 cache_ 里，
//      前向引用（const B = -A; 写在 const A = 3; 前面）自然解决
//   3. 求出的值同时写进符号表（hasConstValue/constValue），
//      BodyChecker 和 resolveArrayLens 的既有逻辑不用改
//
// 已知缺口（暂不做）：impl 里的关联常量（Self::A 互相引用成环）——
//   builder 目前丢掉 impl 内的 const 条目，要做需先扩 AST（ImplBlock.consts）
// ============================================================

#include "ast/ast.h"
#include "ast/type.h"
#include "semantic/symbol_table.h"

#include <unordered_map>
#include <vector>

class ConstEvaluator {
public:
    explicit ConstEvaluator(SymbolTable& symtab) : symtab_(symtab) {}

    // 整数字面量求值（带类型后缀也可）；不是整数字面量返回 false
    static bool evalIntLiteral(const ExprPtr& e, long long& out);

    // 常量表达式求值（字符串版，服务 arrayLenExpr）：
    // + - * / %、括号、整数字面量、const 名；求不出返回 false
    bool constEval(const std::string& expr, long long& out);

    // 解析类型里的数组长度（递归处理嵌套数组/引用）
    void resolveArrayLens(Type& t);

    // 统一预求值入口：收集顶层 const → 三色 DFS 查环 + 求值 + 缓存
    void evaluateAll(const CratePtr& crate);

private:
    SymbolTable& symtab_;
    // 已求出的常量值：名字 → 值（DFS 黑节点的结果，避免重复求值）。
    // 顶层 const 用裸名（"VALUE"），关联常量用限定名（"Config::N"）
    std::unordered_map<std::string, long long> cache_;
    // const 定义表：键同 cache_（顶层裸名 / 关联常量限定名）
    std::unordered_map<std::string, std::shared_ptr<ConstDef>> consts_;
    // 关联常量限定名 → 所属类型名（"Config::N" → "Config"），
    // evalConst 据此设置 currentSelfType_
    std::unordered_map<std::string, std::string> selfTypeOf_;
    // 正在求值的关联常量所属类型（"Config"）；求顶层 const 时为空。
    // 用于把初值里的 "Self::X" 归一化成 "Config::X"
    std::string currentSelfType_;

    // 路径名归一化（已实现）："Self::X" → "<currentSelfType_>::X"，其余原样返回
    std::string normalizePath(const std::string& name) const;

    // ---- 以下三个待实现（逐行伪代码在 const_evaluator.cpp 对应函数体里）----

    // 三色 DFS 求值：查依赖环 + 依赖后序求值 + 写 cache_ 和符号表
    long long evalConst(const std::string& name,
                        std::unordered_map<std::string, int>& state);

    // 递归求值常量表达式（AST 版），求不出返回 false
    bool evalConstExpr(const ExprPtr& e, long long& out);

    // 收集表达式里引用的顶层 const 名（建依赖图用）
    void collectDeps(const ExprPtr& e, std::vector<std::string>& out);
};
