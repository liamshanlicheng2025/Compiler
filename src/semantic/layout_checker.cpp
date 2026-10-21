#include "semantic/layout_checker.h"

// 骨架：仅完成第 1 步的"收集 struct 定义"，建边和 DFS 待实现。
// 实现后把 main.cpp 里的调用取消注释。

void LayoutChecker::run(const CratePtr& crate) {
    // 伪代码：
    //   for (item : crate->items)
    //       if (auto* sd = dynamic_cast<StructDef*>(item.get()))
    //           structs_[sd->name] = sd;
    //   for (auto& [name, sd] : structs_)
    //       dfs(name, ...);   // 有环 → semaError
    (void)crate;
}
