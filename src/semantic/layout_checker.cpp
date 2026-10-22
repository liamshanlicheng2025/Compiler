#include "semantic/layout_checker.h"

#include "semantic/semantic_error.h"

#include <algorithm>

void LayoutChecker::run(const CratePtr& crate) {
    for (auto& item : crate->items) {
        if (auto sd = std::dynamic_pointer_cast<StructDef>(item))
            structs_[sd->name] = sd;
    }
    for (auto& [name, sd] : structs_) collectFieldEdges(*sd);
    detectCycles();
}
// 建图：每个字段的类型都交给 collectTypeEdges 递归拆，
// 拆出的按值包含 struct 名直接追加进 edges_[def.name]
void LayoutChecker::collectFieldEdges(const StructDef& def) {
    for (auto& field : def.fields)
        collectTypeEdges(field.type, edges_[def.name]);
}

// 递归拆类型：Named → 记录一条边（该类型是否存在由 dfs 跳过、
// CrateIndex/BodyChecker 负责报错）；Array → 穿透元素类型继续拆
// （数组按值平铺，不阻断环）；Ref/基础类型 → 间接或叶子，不穿透
void LayoutChecker::collectTypeEdges(const Type& t, std::vector<std::string>& out) {
    switch (t.kind) {
        case TypeKind::Named: out.push_back(t.name); break;
        case TypeKind::Array: collectTypeEdges(*t.elem, out); break;
        default: break;
    }
}

// 三色 DFS 查环：
//   白（0，未访问）→ 进栈时涂灰（1）→ 所有后继处理完涂黑（2）。
//   DFS 中遇到灰色节点说明从它能绕回当前栈上的祖先——即按值包含成环。
//   黑节点不会再导出环（从它出发的路径已完整查过），直接跳过。
void LayoutChecker::detectCycles() {
    std::unordered_map<std::string, int> state;   // 缺省 0 = 未访问
    std::vector<std::string> path;
    for (auto& [name, sd] : structs_) {
        if (state[name] == 0) dfs(name, state, path);
    }
}

void LayoutChecker::dfs(const std::string& name,
                        std::unordered_map<std::string, int>& state,
                        std::vector<std::string>& path) {
    state[name] = 1;
    path.push_back(name);
    auto it = edges_.find(name);
    if (it != edges_.end()) {
        for (auto& next : it->second) {
            // 字段引用了未定义的类型名：留给 CrateIndex/BodyChecker 报，这里跳过
            if (structs_.find(next) == structs_.end()) continue;
            if (state[next] == 1) {
                // 撞上灰节点：path 中从 next 首次出现的位置到栈顶就是环
                auto start = std::find(path.begin(), path.end(), next);
                std::string cycle;
                for (auto p = start; p != path.end(); ++p)
                    cycle += (p == start ? "" : " -> ") + *p;
                semaError("struct 按值包含成环：" + cycle + " -> " + next);
            }
            if (state[next] == 0) dfs(next, state, path);
        }
    }
    path.pop_back();
    state[name] = 2;
}
