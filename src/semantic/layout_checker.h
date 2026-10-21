#pragma once

// ============================================================
// LayoutChecker：全局约束——struct 按值包含必须有限（8 阶段模型的第 7 阶段）
//
// 问题：struct A { b: B } 且 struct B { a: A } 会让对象大小无限展开，
// 必须报语义错误。引用（&T / &mut T）是间接边，不构成按值包含，不算环。
//
// TODO（待实现）算法骨架：
//   1. 建图：遍历 Crate 顶层 StructDef，对每个字段类型递归找"按值包含的
//      struct 名"——字段类型是 Named(S) 则加边 A → S；数组 [T; n] 要穿透
//      elem 继续找（数组按值展开）；Ref 不穿透；基础类型无视。
//   2. 查环：对每个 struct 做 DFS（三色标记：未访问/在栈上/已完成），
//      遇到"在栈上"的节点即报：
//        semaError("struct 按值包含成环：" + 环上的名字链);
//   3. 调用时机：main 里 CrateIndex::run 之后、BodyChecker::run 之前。
//
// 注意：本检查只看"形状"，不需要求字段大小；数组长度不参与判断。
// ============================================================

#include "ast/ast.h"

class LayoutChecker {
public:
    // 入口：收集顶层 struct 定义，逐个 DFS 查环（当前为骨架，不做事）
    void run(const CratePtr& crate);

private:
    // TODO：void collectFieldEdges(const StructDef& def, 建图);
    // TODO：void dfs(const std::string& name, 三色标记, 栈路径);
};
