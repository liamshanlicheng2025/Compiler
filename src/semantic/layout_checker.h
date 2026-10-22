#pragma once

// ============================================================
// LayoutChecker：全局约束——struct 按值包含必须有限（8 阶段模型的第 7 阶段）
//
// 问题：struct A { b: B } 且 struct B { a: A } 会让对象大小无限展开，
// 必须报语义错误。引用（&T / &mut T）是间接边，不构成按值包含，不算环；
// 数组 [T; n] 按值平铺元素，要穿透 elem 继续算。
//
// 流程（run 里串起来）：
//   1. 收集顶层 StructDef 进 structs_（名字 → 定义）
//   2. 对每个 struct 调 collectFieldEdges 建"按值包含图" edges_
//   3. detectCycles 三色 DFS 查环，有环 → semaError
//
// 分工：detectCycles / dfs（查环）、collectFieldEdges / collectTypeEdges（建图）。
// 注意：本检查只看"形状"，不需要求字段大小；数组长度不参与判断。
// ============================================================

#include "ast/ast.h"

#include <unordered_map>
#include <vector>

class LayoutChecker {
public:
    // 入口：收集顶层 struct 定义 → 建图 → 查环
    void run(const CratePtr& crate);

private:
    // 按值包含图：edges_[A] = A 的字段里（穿透数组后）按值包含的 struct 名
    std::unordered_map<std::string, std::vector<std::string>> edges_;
    // 顶层 struct 定义表：名字 → 定义节点
    std::unordered_map<std::string, std::shared_ptr<StructDef>> structs_;

    // 建图：对每个字段的类型递归拆，按值包含的 struct 名追加进 edges_
    void collectFieldEdges(const StructDef& def);
    // 递归拆类型：Named → 记一条边；Array → 穿透 elem；Ref/基础类型 → 不穿透
    void collectTypeEdges(const Type& t, std::vector<std::string>& out);

    // 查环（已实现）：对所有 struct 跑三色 DFS，发现环即 semaError
    void detectCycles();
    // state: 0=未访问 1=在 DFS 栈上 2=已完成；path 记录当前栈用于打印环
    void dfs(const std::string& name,
             std::unordered_map<std::string, int>& state,
             std::vector<std::string>& path);
};
