# AST 阶段代码走读（Code Review 用）

> 范围：从源代码到 AST 的完整链路。按"数据怎么流"的顺序讲，
> 每个文件/每段代码说明它在做什么、为什么这么写。

## 0. 一张图看全局

```
main.cpp
  │  ① ANTLR 运行时（RustLexer/RustParser，由 grammar/*.g4 自动生成）
  │     字符流 → token 流 → ParseTree（CrateContext 为根）
  ▼
AstBuilder（src/builder/）
  │  ② 继承 RustParserBaseVisitor，重写 34 个 visitXxx，
  │     自底向上把 ParseTree 组装成 AST，节点间用 std::any 传指针
  ▼
AST（src/ast/ast.h 定义的 28 个节点类，Crate 为根）
  │  ③ ASTPrinter（src/ast/ast_printer.*）把树打印成人可读文本（调试证据）
  ▼
（下一阶段：src/semantic/ 语义检查，骨架已就位）
```

## 1. grammar/ —— 文法层（ANTLR 的输入）

- `RustLexer.g4`：词法规则，`lexer grammar` 声明，大写规则名是 token
  （`KW_LET`、`INTEGER_LITERAL`……）。注释/空白走 `channel(HIDDEN)`，
  不进语法分析。`options { superClass = RustLexerBase; }` 挂上手写基类。
- `RustParser.g4`：语法规则，`parser grammar` 声明，小写规则名。
  核心是 `expression` 那条左递归规则——**备选的书写顺序就是运算优先级**；
  `# AssignmentExpression` 这类 labeled alternative 决定生成的 Context 类名
  和 visit 方法名。
- `cpp/RustLexerBase.* / RustParserBase.*`：g4 里 superClass 指向的手写基类，
  处理 Rust 词法的特殊逻辑（如 raw string 的井号匹配）。
- **适配改动**：g4 原本面向 Java 目标，`this.` 有 5 处改成了 `this->`（C++ 语法）。
- `build/generated/`：ANTLR 生成的解析器代码，构建时自动产出，不进 git。

## 2. src/main.cpp —— 流水线入口

四段式：

1. **参数解析**：`./compiler <file.rs> [--tree]`，`--tree` 只打印 ANTLR
   解析树（`toStringTree`），是扩展 AST 时对照语法结构的调试工具。
2. **阶段 1（ANTLR 解析）**：`ANTLRInputStream → RustLexer →
   CommonTokenStream → RustParser`，`parser.crate()` 得到 ParseTree 根。
   这段是模板代码，不用动。
3. **阶段 2（构建 AST）**：`AstBuilder::build(tree)` 返回 `Crate*`。
4. **调试输出**：用 `ASTPrinter` 打印 AST。之后接语义检查时默认行为会改。

## 3. src/ast/ —— AST 节点定义层

### ast.h（节点定义，28 个节点类）

内存策略：全项目统一 std::shared_ptr（CR 后从裸指针修订），节点间传递用
CratePtr/ItemPtr/StmtPtr/ExprPtr/BlockPtr 别名，visitor 接口仍按引用传递。

类层次（三个抽象层不实现 accept，具体节点各自实现）：

```
Node
├── Item（顶层条目）── Crate、Function、StructDef、ImplBlock、
│                     EnumDef、ConstDef、StaticDef
├── Stmt（语句）──── LetStmt、ExprStmt、MacroStmt
└── Expr（表达式）── Literal、BinaryExpr、UnaryExpr、PathExpr、
                    CallExpr、IfExpr、WhileExpr、LoopExpr、
                    ReturnExpr、BreakExpr、ContinueExpr、
                    StructLiteralExpr、FieldExpr、MethodCallExpr、
                    ArrayExpr、IndexExpr、MatchExpr
（Block 直接继承 Node——它既不是 Stmt 也不是 Expr，是函数体/分支体）
```

关键设计决策（review 可能会问）：

- **结构化 Type**：类型标注存 `Type`（ast/type.h 的判别枚举+复合字段），
  不再存字符串原文；Literal 仍存文本原文，语义阶段再细化。
- **Block::tailExpr**：Rust 的块最后一条不带分号的表达式是块的值，
  所以 Block 除了 `statements` 还有一个 `tailExpr`。对应 g4 里
  `statements: statement+ expression?` 的结构。
- **比较/赋值/复合赋值复用 BinaryExpr**：不新设节点，op 存运算符字符串。
- **引用/解引用复用 UnaryExpr**：op 存 `"&"` / `"&mut"` / `"*"`。
- **Function::isMethod/selfMut**：impl 方法第一个参数是 self 时标记，
  params 里不含 self，留给语义/CodeGen 阶段处理。
- **EnumVariant / MatchArmNode 是 struct 不是节点类**：它们是 EnumDef /
  MatchExpr 的组成部分，按值存在 vector 里，不参与 visitor 遍历。
- **文件底部的"已完成波次备忘"**：留痕记录每一波加了哪些节点。

### ast_visitor.h（访问者接口）

`ASTVisitor` 纯虚接口：每种具体节点一个 `visit`。**每加一个节点类就要
在这里加一行**——之后语义检查、IR 生成的新访问器都实现这个接口，
ASTPrinter 是第一个实现，当模板用。

### ast.cpp（accept 样板）

每个节点类一行机械代码：`void Xxx::accept(ASTVisitor& v) { v.visit(*this); }`。
作用：visitor 模式的双分派——节点不知道访问者是谁，访问者不知道节点
具体类型，运行时通过 accept 把两边对上。

### ast_printer.h/.cpp（打印器，调试工具）

实现 `ASTVisitor`，带缩进地把整棵树打成文本。**不在编译主链路上**，
是开发期调试和 W4 验收的证据工具（详见"ast_printer 是否核心代码"的讨论）。
每个 visit 负责打印自己这一行 + 递归子节点，用 `indent_` 控制缩进层级。

## 4. src/builder/ —— ParseTree → AST 的核心

### ast_builder.h

`AstBuilder : public RustParserBaseVisitor`，声明 34 个 visit 重写 +
`build(CrateContext*)` 入口（内部 visit 根节点并把 any 转成 `Crate*`）。

### ast_builder.cpp（483 行，34 个 visit 函数）

**通用约定**（每个函数都遵守）：

- 返回 `std::any`，里面只存五种指针之一：`Crate* / Item* / Stmt* / Expr* / Block*`，
  返回前 `static_cast` 成基类——调用方才能用 `std::any_cast<Expr*>` 统一取出。
- 子节点通过 `std::any_cast<...>(visit(ctx->xxx()))` 递归获得。
- `visitCrate` 和 `visitBlockExpression` 有 `result.has_value()` 防御 guard：
  遇到没覆盖的语法结构（visit 返回空 any）打印警告并跳过，而不是崩溃——
  让"部分覆盖"的 AST 也能跑起来对照。

按波次分组：

**入口与顶层**
| 函数 | 做什么 |
|---|---|
| `visitCrate` | 遍历 items，跳过空 any，收进 `Crate::items` |
| `visitFunction_` | 函数名、参数表（含 selfParam 检测：impl 里第一个参数是 self/&self/&mut self 时设 isMethod/selfMut）、返回类型、函数体 |
| `visitBlockExpression` | 遍历 statements；最后若有 `statements->expression()` 则作为尾表达式进 `tailExpr` |

**语句**
| 函数 | 做什么 |
|---|---|
| `visitLetStatement` | 从 pattern 规则链（patternNoTopAlt→patternWithoutRange→identifierPattern）里取出变量名；类型标注、初值均可选 |
| `visitExpressionStatement` | 包装表达式为 ExprStmt；`expressionWithBlock` 备选（if/while/loop/match）也统一包进来 |
| `visitMacroInvocationSemi` | `println!(...)` 等宏语句，名字+参数原文存进 MacroStmt（不解析参数） |

**表达式（基础）**
| 函数 | 做什么 |
|---|---|
| `visitLiteralExpression` | 字面量原文进 Literal::text |
| `visitArithmeticOrLogicalExpression` | 算术/位运算，`ctx->op` 取运算符，左右递归 visit |
| `visitComparisonExpression` | 比较运算，复用 BinaryExpr |
| `visitAssignmentExpression` | `=` 赋值，复用 BinaryExpr（op="="） |
| `visitCompoundAssignmentExpression` | `+=` 等，op 从 compoundAssignOperator 取文本 |
| `visitNegationExpression` | `-`/`!`，复用 UnaryExpr |
| `visitGroupedExpression` | 括号：**直接穿透返回内层结果**，AST 不留括号节点 |
| `visitPathExpression_` | 变量/路径引用；多段路径（`Point::new`）getText 整段存 name |

**控制流**
| 函数 | 做什么 |
|---|---|
| `visitIfExpression` | cond/thenBlock/elseBlock；**else if 链**靠检测 else 后是嵌套 if 还是块 |
| `visitPredicateLoopExpression` | while：cond + body |
| `visitInfiniteLoopExpression` | loop：只有 body |
| `visitReturnExpression` / `visitBreakExpression` / `visitContinueExpression` | 值可空（裸 `return;`/`break;`） |

**函数与复合类型**
| 函数 | 做什么 |
|---|---|
| `visitCallExpression` | callee（通常是 PathExpr）+ 参数列表 |
| `visitStructStruct` | struct 定义：字段表（名字+类型原文） |
| `visitInherentImpl` | impl 块：目标类型名 + 方法列表 |
| `visitStructExpression_` | struct 字面量；**支持字段简写** `Point { x }`——此时 g4 的 expression() 为空，补一个同名 PathExpr（等价 `x: x`） |
| `visitFieldExpression` | `p.x`：object + 字段名 |
| `visitMethodCallExpression` | `p.f(a)`：receiver + 方法名 + args（**self 不进 args**） |

**数组/引用/全局条目**
| 函数 | 做什么 |
|---|---|
| `visitArrayExpression` | 两种形态：列举 `[1,2,3]`（elements）和重复 `[0; 5]`（repeatValue+repeatCount） |
| `visitIndexExpression` | `a[i]` |
| `visitBorrowExpression` / `visitDereferenceExpression` | `&`/`&mut`/`*`，复用 UnaryExpr |
| `visitConstantItem` / `visitStaticItem` | const/static 全局量，static 记 isMut |

**enum / match（最后一块）**
| 函数 | 做什么 |
|---|---|
| `visitEnumeration` | 变体名 + tuple 式负载类型列表；struct 式变体/显式判别值暂跳过（测试集不出现） |
| `visitMatchExpression` | scrutinee + 分支表。**坑 1**：matchArm 里只有 pattern，分支体在 matchArms 层（前 n-1 个在 matchArmExpression(i)，最后一个在 matchArms->expression()）。**坑 2**：`=> { }` 实际被 ANTLR 解析进 `expression` 备选（expression 规则包含 expressionWithBlock），所以用 `dynamic_cast<ExpressionWithBlock_Context*>` 判块体，抽成 fillBody lambda 统一处理 |

## 5. src/semantic/ —— 下一阶段骨架（本次 review 范围外）

- `src/ast/type.h/.cpp`：TypeKind 枚举 + Type 结构（toString 已实现，
  equal 留待语义阶段实现；CR 后从 semantic/ 移来，AST 与语义共用）
- `symbol_table.h/.cpp`：Symbol/Scope/SymbolTable 三层，作用域栈的
  机械部分（enterScope/exitScope）已实现，insert/lookup 待填
- 计划见 `docs/semantic-plan.md`

## 6. tests/ —— 验证资产

- `minimal.rs`：最小用例（空 main）
- `hello.rs`：基础 smoke test
- `syntax-tour.rs`：综合用例（struct/impl/enum/match/循环/数组/引用全走一遍；
  元组和 for 是范围外结构，已换成等价写法并注释留痕）

验证命令：
```bash
cmake --build build -j32
./build/compiler tests/syntax-tour.rs          # 打印 AST
./build/compiler --tree tests/syntax-tour.rs   # 打印 ANTLR 解析树（对照用）
```

## 7. Review 可能被问的问题（预习）

1. **visit 之间为什么用 std::any？** ANTLR 生成的 visit 签名固定返回
   std::any；我们约定只存五种基类指针，调用方 any_cast 取回。
2. **bad_any_cast 一般是什么原因？** 某个语法结构没覆盖到，visit 走了
   基类默认实现（聚合子节点结果），类型对不上或为空。
3. **为什么括号不产生 AST 节点？** 括号只影响解析分组，分组结果已体现
   在树形里，visitGroupedExpression 直接穿透。
4. **else if 怎么表示？** elseBlock 里包一个只含一条 ExprStmt(IfExpr) 的块。
5. **方法的 self 去哪了？** visitFunction_ 检测 selfParam 设 isMethod/
   selfMut，params 不含 self；调用侧 receiver 也不进 args。
6. **宏为什么不解析参数？** 宏参数不是表达式（是 token 树），课程范围内
   只有 println!/print!，原样存原文即可。
