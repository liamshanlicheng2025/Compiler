# RCompiler SPEC 中文译文（Semantic 相关章节）

课程版 Rust Reference（`reference/RCompiler-Spec/src/`）中 **semantic 阶段相关章节** 的中文翻译。
原文选取依据见 `docs/semantic-spec-notes.md` 附录的对照表。翻译保留原文的 `r[...]` 规则编号，
方便与英文原文对照；代码块和语法块保持英文原样。

## 阅读顺序

### 一、类型系统（11 篇）

| # | 文件 | 内容 |
|---|------|------|
| 1 | `types.md` | 类型总览、类型表达式、递归类型 |
| 2 | `types/boolean.md` | 布尔类型 |
| 3 | `types/numeric.md` | 数值类型（i32/u32/isize/usize） |
| 4 | `types/textual.md` | 文本类型（char/str） |
| 5 | `types/unit.md` | 单元类型 `()` |
| 6 | `types/array.md` | 数组类型 |
| 7 | `types/struct.md` | 结构体类型 |
| 8 | `types/enum.md` | 枚举类型 |
| 9 | `types/pointer.md` | 引用 `&` / `&mut` |
| 10 | `type-coercions.md` | **类型强制转换 + LUB（重点）** |
| 11 | `subtyping.md` | 子类型与型变 |

### 二、语句与表达式（15 篇）

| # | 文件 | 内容 |
|---|------|------|
| 12 | `statements.md` | 语句、let、分号规则 |
| 13 | `expressions.md` | 表达式总览、优先级、**place/value 表达式、可变性、隐式借用（重点）** |
| 14 | `expressions/literal-expr.md` | 字面量（转义、整数推断规则） |
| 15 | `expressions/path-expr.md` | 路径表达式 |
| 16 | `expressions/block-expr.md` | 块表达式 |
| 17 | `expressions/operator-expr.md` | **运算符全表：溢出、借用、解引用、算术、比较、短路、as、赋值（重点）** |
| 18 | `expressions/grouped-expr.md` | 括号表达式 |
| 19 | `expressions/array-expr.md` | 数组与下标表达式（[v; N] 的 Copy 要求） |
| 20 | `expressions/struct-expr.md` | 结构体表达式 |
| 21 | `expressions/call-expr.md` | 调用表达式 |
| 22 | `expressions/method-call-expr.md` | 方法调用（候选接收者类型列表、自动解引用） |
| 23 | `expressions/field-expr.md` | 字段访问 |
| 24 | `expressions/loop-expr.md` | loop / while / break / continue |
| 25 | `expressions/if-expr.md` | if 表达式 |
| 26 | `expressions/return-expr.md` | return 表达式 |

### 三、名字与条目（8 篇）

| # | 文件 | 内容 |
|---|------|------|
| 27 | `names/namespaces.md` | **命名空间（类型/值分开的依据，重点）** |
| 28 | `names/scopes.md` | 作用域、遮蔽规则 |
| 29 | `items/functions.md` | 函数 |
| 30 | `items/structs.md` | 结构体条目 |
| 31 | `items/implementations.md` | impl（固有实现 / trait 实现） |
| 32 | `items/associated-items.md` | 关联函数、方法、关联常量 |
| 33 | `items/constant-items.md` | const 条目 |
| 34 | `items/static-items.md` | static 条目 |

### 四、求值与属性（3 篇）

| # | 文件 | 内容 |
|---|------|------|
| 35 | `const_eval.md` | **常量求值（重点）** |
| 36 | `attributes/derive.md` | derive 属性 |
| 37 | `variables.md` | 变量与可变性 |

## 说明

- `names/name-resolution.md` 原文只是一个占位符（"placeholder for future expansion"），没有实质内容，未翻译。
- 每篇开头的引用块注明了对应的原文路径。
- 与我们编译器实现的对应关系，见 `docs/semantic-spec-notes.md` 附录对照表。
