# 语义分析阶段总结（Code Review 用）

> 整理日期：2026-11-06
> 本文回答：semantic 阶段我们做了什么、架构怎么设计、运行流程怎么走、
> 每个文件分别负责什么。

## 1. 开发历程

| 时间 | 里程碑 | 验证结果 |
|---|---|---|
| 10-19 | 语义分析初版：两遍 DFS（第一遍收集声明，第二遍检查函数体）+ 符号表 + 结构化类型系统 | semantic-1/2 开始过点 |
| 10-21 | 按赵睿城分享的阶段模型重组：拆成 CrateIndex / ConstEvaluator / LayoutChecker / BodyChecker 四个模块；同步 2025 官方新测试集（rx-compiler-testcases） | semantic-1 221/221、semantic-2 50/50 |
| 10-22 | 补三个全局检查：入口检查（main 形态）、struct 布局查环、常量统一预求值 + 依赖环 | rx 套件常量子目录满分 |
| 10-28 | impl 关联常量（`Config::N`）；表达式类型旁表 exprTypes（语义→IR 的桥） | rx 154/239（缺口均为子集外特性） |

过程中的两次重要返工：

1. **类型系统重构**（cr 反馈）：AST 里的类型标注原来存字符串，改为判别枚举
   `TypeKind` + 按需启用字段的结构体 `Type`（`src/ast/type.h`），语义阶段直接复用。
2. **内存策略重构**（cr 反馈）：AST 节点从裸指针改为 `shared_ptr` 持有，
   杜绝手动 delete 和泄漏；旁表得以放心用节点地址当键。

## 2. 设计架构

### 2.1 总体：四模块 + 两基础设施

```
┌─────────────────────────────────────────────────┐
│ SymbolTable（符号表）      SemanticError（报错） │
├─────────────────────────────────────────────────┤
│ ① CrateIndex     声明收集：所有顶层名字进符号表   │
│ ② ConstEvaluator 常量预求值 + 依赖环检查         │
│ ③ LayoutChecker  struct 按值包含查环             │
│ ④ BodyChecker    函数体：名字解析+类型检查+控制流 │
└─────────────────────────────────────────────────┘
```

为什么拆成"先收集、后检查"两遍：**Rust 允许先用后定义**（函数互相递归、
常量前向引用），单遍遍历必然漏名字。CrateIndex 只看顶层签名不看函数体，
BodyChecker 检查函数体时所有名字已可见。

### 2.2 数据流（main.cpp 的串行流水线）

```
源代码
  │  ANTLR（g4 自动生成）
  ▼
解析树 ──AstBuilder──► AST
  │
  ▼
CrateIndex.run(ast)        产出：符号表（函数/类型/常量/impl 方法表）+ 入口检查
  │
  ▼
ConstEvaluator.evaluateAll(ast)   产出：每个 const 的编译期值（写进符号表/节点）
  │
  ▼
LayoutChecker.run(ast)     产出：无（只报错）
  │
  ▼
BodyChecker.run(ast)       产出：exprTypes 旁表（每个表达式 → 类型）→ IR 阶段
  │
  ▼ 任何一步失败 → 抛 SemanticError → exit 1
通过 → IR 生成
```

### 2.3 核心设计决策

**符号表三层结构**：`Symbol`（一个名字的全部信息）→ `Scope`（一层作用域，
parent 链）→ `SymbolTable`（作用域栈）。进块压栈、出块弹栈，链式查找天然
实现遮蔽。impl 方法和关联常量不在作用域体系里——它们靠**类型名**查（receiver
类型 → 方法表），单独挂在 SymbolTable 的 `implMethods`/`implConsts` 两张
public 表上。

**类型传递用 lastType_ 通道**：visit 返回 void，表达式类型经成员变量
`lastType_` 父读子写，配合 `evalExpr()` 包装（visit + 读走 + 记旁表）。
IR 阶段的 `lastValue_` 沿用了同一模式。

**exprTypes 旁表**：不污染 AST 节点，用 `unordered_map<const Expr*, Type>`
外挂。AST 节点 shared_ptr 持有、地址稳定，指针可直接当键。这是语义阶段
留给 IR 阶段的核心资产——选指令（sdiv/udiv）、算布局都靠它。

**常量求值独立成模块**：const 初值、数组长度 `[i32; N-1]`、关联常量都要
编译期求值，且允许前向引用。ConstEvaluator 用三色 DFS（灰=求值中，撞灰
= 环）统一预求值，DFS 后序天然是依赖顺序。

**报错即异常**：任何检查失败 `semaError()` 抛 `SemanticError`，main 捕获后
exit 1——测试集按退出码判定，不需要错误恢复。

## 3. 每个文件在做什么

| 文件 | 职责 |
|---|---|
| `semantic/symbol_table.h/.cpp` | 符号/作用域/符号表三层结构；内建函数注册（printlnInt/printInt/getInt/exit + snake_case 别名）；impl 方法表与关联常量表的容器 |
| `semantic/crate_index.h/.cpp` | **声明收集**：遍历顶层条目，把函数签名、const/static、struct/enum 注册进全局作用域，impl 方法挂类型表；入口检查（main 必须存在、无参、无泛型、返回 ()） |
| `semantic/const_evaluator.h/.cpp` | **编译期求值**：const 初值三色 DFS 预求值（查依赖环）；数组长度表达式解析；常量下标越界检查；关联常量值写回 ConstDef 节点 |
| `semantic/layout_checker.h/.cpp` | **布局查环**：struct 按值包含建图（引用是间接边不算环，数组穿透 elem），三色 DFS 查环——`struct A{b:B}` + `struct B{a:A}` 必须报错 |
| `semantic/body_checker.h/.cpp` | **函数体检查**（最大的模块）：逐函数做名字解析（每个名字符号表可查）、类型检查（每表达式求类型、不匹配报错）、控制流检查（break/continue 在循环内、return 类型匹配、尾表达式 = 声明返回类型）；填充 exprTypes 旁表 |
| `semantic/semantic_error.h` | 异常类型 + 统一报错入口 `semaError()`（`[[noreturn]]`，报错后不用补 return） |

支撑文件（不在 semantic/ 但强相关）：

- `ast/type.h/.cpp`：结构化类型 `Type`（TypeKind 枚举 + elem/arrayLen/isMut/name
  按需启用）；`equal()` 类型匹配；`IntLit` 未定型整数字面量的归一化
- `ast/ast.h`：AST 节点定义（shared_ptr 持有）；`ConstDef` 上的
  `hasConstValue/constValue` 回写字段；`ImplBlock.consts` 关联常量表
- `main.cpp`：把四个模块串成流水线（见 §2.2）

## 4. 验证情况

| 套件 | 结果 | 说明 |
|---|---|---|
| semantic-1（旧官方） | **221/221** | 编译期判定（该过/该拒） |
| semantic-2（旧官方） | **50/50** | 同上 |
| rx-compiler-testcases（新官方） | **154/239** | 常量子目录满分；缺口集中在 vec/trait/lifetime 等课程子集外特性 |
| CI | GitHub Actions 每次 push 自动构建 + 回归 | 见 `ci-cd-record.md` |

## 5. 已知边界（课程子集裁剪，助教确认）

- enum / match 不实现（AST 留了壳，所有阶段走死桩）
- 泛型不支持（`Function.hasGenericParams` 仅用于入口检查拒绝 `fn main<T>`）
- 借用检查的完整规则（我们只查可变性/悬垂的明显错误，不做 NLL 生命周期推导）
