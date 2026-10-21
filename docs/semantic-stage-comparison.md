# 语义分析阶段对照：赵睿城的 8 阶段 vs 我们的实现

> 依据：赵睿城分享 PDF 中"语义分析的执行顺序"一页的 8 个阶段，
> 逐个对照我们的代码（`src/semantic/`），说明每个阶段我们有没有、在哪、差多少。
>
> 我们的总验收：semantic-1 221/221，semantic-2 50/50（2026-10-21）。

## 总览表

| # | 他的阶段 | 我们有吗 | 对应我们的代码 | 完成度 |
|---|---|---|---|---|
| 1 | 声明收集 CrateIndex | 有 | `DeclCollector`（Pass 1） | ✅ 完成（单名字空间简化） |
| 2 | 结构体身份 + impl 头部 | 有 | `registerStruct` / `registerImpl` | ✅ 完成 |
| 3 | 常量与函数登记、入口检查 | 大半 | `registerConst/Static/Function` | ⚠️ 缺 main 入口合法性检查 |
| 4 | 常量求值 ConstEvaluator | 简化版 | `constEval` / `evalIntLiteral` | ⚠️ 无依赖环检测、无缓存 |
| 5 | 结构体字段 StructResolver | 有，但时机不同 | builder 期 parseType + Pass 2 现用现查 | ✅ 完成（机制不同） |
| 6 | 函数签名 FunctionResolver | 有 | `registerFunction`（paramTypes/returnType） | ✅ 完成 |
| 7 | 全局约束 LayoutChecker / DeriveChecker | 没有 | —— | ❌ 缺失（测试集无此用例） |
| 8 | 函数体检查 BodyChecker | 有 | `SemanticChecker`（Pass 2） | ✅ 完成 |

---

## 第 1 阶段：声明收集 CrateIndex

**他做的**：建 CrateIndex，把名字解析成"身份"（SymbolId），分三个名字空间：
类型名字空间、值名字空间、关联成员名字空间。同名 struct 和 fn 可以共存。

**我们做的**：`DeclCollector::run`（`decl_collector.cpp`）——内建函数 +
遍历顶层 Item，按节点类型分发到 register* 系列函数。

差异：
- 他是**三名字空间**（类型/值/关联成员分开登记），我们是**每个 Scope 一张表，
  用 `SymbolKind` 区分种类**。同名 struct 和函数共存时我们要靠 kind 判断兜底，
  测试集没踩到这种碰撞，所以够用；他的设计在"名字即身份"上更干净。
- 他的 SymbolId 是整数 ID + 集中表；我们的"身份"就是 AST 节点的 shared_ptr
  （`Symbol::structDef`、`implMethods` 里的 `FunctionPtr`）。效果相同，风格不同。

## 第 2 阶段：结构体身份登记 + impl 头部

**他做的**：先为所有 struct 登记身份（StructResolver 前半），确认 impl 目标合法，
登记关联成员（ImplResolver）。

**我们做的**：`registerStruct`（Symbol 回指 AST 定义节点）+
`registerImpl`（检查目标是 StructType、方法重名查重、**Self 归一化**）。

结论：完全等价。我们的 `Self` → 具体类型名的归一化（basic5 的
`Stack::new() -> Self`）就是在这一步做的，和他"使用当前结构体上下文解析 Self"
是同一件事。

## 第 3 阶段：常量与函数登记、入口检查

**他做的**：登记 const/static/函数，并 `checkEntryPoint`——要求源程序有合法的
`main`（无参、返回 unit/i32 之类）。

**我们做的**：登记全做了；**入口检查没做**——我们的编译器不验证 main 是否存在、
签名是否合法。测试集没把这个当判定条件（compileexitcode 不看 main），所以
能过；严格说这是个小缺口，语义规范上是应该报的。

## 第 4 阶段：常量求值 ConstEvaluator

**他做的**：受限常量上下文（const 初值、数组长度、数组重复长度）的求值器，
先建依赖图查环，再递归求值并缓存。

**我们做的**：
- `evalIntLiteral`：const 初值是整数字面量时把值记进 `Symbol::constValue`；
- `constEval`：50 行递归下降求值器（+ - * / %、括号、const 名查表），
  用于 `resolveArrayLens` 解析 `[i32; ARR - 1]` 这类数组长度。

缺的：
- **依赖环检测**：`const A: i32 = B; const B: i32 = A;` 我们会报"不是常量"，
  他会报"依赖环"。报错信息不同，但都会失败——结果等价。
- **缓存**：我们每个数组长度表达式都重新解析一次。课程规模下无所谓。

## 第 5 阶段：结构体字段解析 StructResolver

**他做的**：先身份后字段——struct 身份登记和字段类型解析分成两步，
所以字段类型可以引用"稍后定义的 struct"（前向引用、递归引用）。

**我们做的**：机制完全不同但效果一样。我们的字段类型在 **builder 阶段**就由
`parseType` 解析成结构化 Type（此时是纯语法翻译，`Named{"B"}` 只是个名字，
不需要 B 已定义）；字段合法性（类型是否存在）**推迟到 Pass 2 用到时**才查
（`visit(FieldExpr)` 拿名字查符号表）。因为"查"发生在声明收集之后，
前向引用天然成立。

结论：完成，只是把"解析"分散到了 builder + 现用现查，没有独立的
StructResolver 阶段。

## 第 6 阶段：函数签名 FunctionResolver

**他做的**：函数身份、参数（名/类型/mut）、返回类型、receiver 模式，
登记进 FunctionInfo，签名先于函数体完成——支持前向调用与递归。

**我们做的**：`registerFunction` 把参数类型表和返回类型存进
`Symbol::paramTypes / type`；`Param.isMut` 由 builder 从 pattern 取出
（`fn f(mut n: i32)`，misc12）。Pass 2 的 `visit(CallExpr)` 只读签名，
同样可以调用稍后定义的函数、可以递归。

结论：完成。我们的"签名先于函数体"就是 Pass 1/Pass 2 的划分本身。

## 第 7 阶段：全局约束（有限大小 + 能力检查）

**他做的**：
- **LayoutChecker**：struct 按值包含不能有环（`struct A { b: B }` +
  `struct B { a: A }` 会让对象大小无限展开），引用/Box/Vec 是间接边不算；
- **DeriveChecker**：按字段检查 Copy/Clone/PartialEq/Eq 能力。

**我们做的**：都没有。按值包含环在我们的实现里不会崩（字段类型是 Named，
从不展开求大小），只是"应该报的错不报"。Derive 超出我们的课程子集。

结论：**唯一完全缺失的阶段**。测试集没有这类用例（221+50 全过就是证据），
如果要补，LayoutChecker 就是一次"struct 字段依赖图"的 DFS 查环，半天工作量。

## 第 8 阶段：函数体检查 BodyChecker

**他做的**：逐函数检查，产物不只是"对错"——还有 **SemanticModel 旁表**
（以 AST 节点指针为键，存每个表达式的类型、每个调用的目标 CallPlan、
coercion 计划、place 可写性），供 lowering 直接消费。

**我们做的**：`SemanticChecker` 全部 25 个 visit——名字解析、类型检查、
控制流检查（loopDepth、return 比对、Never 传导、带值 break）都齐，
但**结果算完即弃**（`lastType_` 流水线，不落盘）。

结论：检查本身完成；**语义记录的持久化是最大的架构差距**，IR 阶段前
建议补上 `exprTypes` 旁表（见下）。

## 贯穿性机制对照

| 机制 | 他 | 我们 | 评价 |
|---|---|---|---|
| 错误处理 | DiagnosticCollector 收集全部诊断 | `semaError` 抛异常即停 | 课程只看退出码，等价；但我们的异常路径上 `exitScope` 不执行，是"碰巧安全" |
| 预期类型 | expected type 下传 + LUB + CoercionPlan | `TypeKind::IntLit` 上冒 + `equal` 对 Never/Unknown 放行 | 殊途同归，测试全过 |
| 可写性 | PlaceChecker 沿访问路径逐段判断 | `checkAssignable` 走到根变量 + `&mut` 穿透 | 极简版，够用 |
| 方法选择 | T → &T → &mut T 自动借用链 | 只有自动解引用（反方向） | 测试集没踩到 |
| 作用域 | ScopeGuard（RAII） | 手动 enter/exitScope | 他更安全，我们够用 |

## 结论

8 个阶段里：**5 个完整等价（1/2/5/6/8），2 个有简化版（3/4），1 个完全缺失（7）**。
缺失和简化都不影响当前测试集，这是"面向测试迭代"和"面向架构完备设计"
两种取向的自然结果，不存在对错。

真正建议在 IR 阶段前补的只有一件：**表达式类型旁表**（`exprTypes`），
把 Pass 2 算出的类型留下来给 lowering 用——这是他的 SemanticModel 思想
的最小化移植，我们的 AST 全部 shared_ptr 持有、节点地址稳定，
天然满足"以节点指针为键"的前提。
