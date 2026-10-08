# 第一次检查（W4）小测复习：g4 → AST

助教原话：考察「利用 g4 构建 AST 的过程」，会「画一些简单的 AST 树」，都是基础内容。
本文档按「流程理解 → 画图方法 → 典型例题 → 问答清单」组织。

---

## 一、整体流程（必考，简答/填空）

```
源文件 (.rs)
   │  ① 词法分析（Lexer，由 RustLexer.g4 生成）：字符流 → token 流
   ▼
token 流
   │  ② 语法分析（Parser，由 RustParser.g4 生成）：token 流 → 解析树 ParseTree
   ▼
ParseTree（忠实反映 g4 每一条规则的推导，含大量冗余节点）
   │  ③ AST 构建（AstBuilder，继承 ANTLR 生成的 RustParserBaseVisitor，
   │      重写 visitXxx 方法自底向上组装）
   ▼
AST（只保留语义结构的精简树，后续语义检查、代码生成都基于它）
```

### 必背问答

**Q1：g4 文件起什么作用？**
g4 是 ANTLR 的文法描述文件。Lexer g4 描述 token（关键字、标识符、字面量、运算符）
长什么样；Parser g4 描述语法规则（expression、statement、function_ 等）怎么由
token 和其他规则组合而成。ANTLR 根据 g4 自动生成词法/语法分析器的 C++ 代码
（我们项目里生成到 `build/generated/`，不进 git）。

**Q2：为什么有了 g4 / ParseTree 还不够，还要 AST？**
ParseTree 是语法推导的忠实记录，里面有大量对"含义"没有贡献的节点：
- 优先级链：一个 `1 + 2` 在 ParseTree 里要穿过 expression → ... → literalExpression
  好几层单向嵌套；
- 标点符号：括号、分号、逗号都是叶子节点；
- 语法上的包装规则（如 expressionStatement 包了 expressionWithBlock 一层）。
后续阶段（语义检查、代码生成）关心的是"这是一个加法，左操作数是 1，右操作数是 2"，
而不是推导路径。AST 把优先级、结合性、括号这些**已经在树形结构里隐式表达**的信息
留下，把冗余节点丢掉。

**Q3：visitor 模式怎么把 ParseTree 变成 AST？**
ANTLR 为 g4 里每条规则生成一个 `visitXxx(ctx)` 钩子，ctx 是该规则对应的
解析树节点上下文（能取到它的每个子节点）。我们重写这些 visit 方法：
- 叶子规则（如 literalExpression）直接造 AST 叶子节点返回；
- 中间规则递归 visit 子节点，把返回的子 AST 节点组装成自己的节点；
- 整棵树自底向上拼装。visit 之间用 `std::any` 传递指针（我们约定只存
  Crate*/Item*/Stmt*/Expr*/Block* 五种）。

**Q4：ANTLR 里优先级/结合性怎么表达？**
在 g4 里，同一规则的备选（alternative）**书写顺序就是优先级**（越靠前优先级越高），
`<assoc=right>` 标注右结合（如赋值）。ANTLR 生成的解析器按此构造解析树，
所以 `1 + 2 * 3` 的解析树天然是 `+` 在上、`*` 在下——AST 直接继承这个结构，
不需要再处理优先级。

---

## 二、g4 文法复习重点（助教特别提醒）

### 1. 两个文件的分工

| | RustLexer.g4 | RustParser.g4 |
|---|---|---|
| 声明 | `lexer grammar RustLexer;` | `parser grammar RustParser;` |
| 产物 | token（词法符号） | 规则（语法结构） |
| 命名 | **大写开头**：`KW_LET`、`INTEGER_LITERAL` | **小写开头**：`letStatement`、`expression` |
| 对应阶段 | 字符流 → token 流 | token 流 → 解析树 |

Lexer 里注意：
- 关键字都是独立 token：`KW_LET: 'let';`、`KW_FN: 'fn';`、`KW_MATCH: 'match';`……
- `INTEGER_LITERAL` 涵盖十/二/八/十六进制，可带类型后缀（`1i32`）
- 注释、空白、换行不进语法分析：`-> channel(HIDDEN)`（进隐藏通道而非丢弃）

### 2. 必会的元符号

| 符号 | 含义 | 例子 |
|---|---|---|
| `\|` | 备选（或） | `KW_TRUE \| KW_FALSE` |
| `?` | 可选（0 或 1 次） | `(COLON type_)?` |
| `*` | 0 次或多次 | `outerAttribute*` |
| `+` | 1 次或多次 | `outerAttribute+` |
| `( )` | 分组 | `(AND \| ANDAND) KW_MUT? expression` |
| `# 名字` | 给备选起标签 | `expression EQ expression # AssignmentExpression` |
| `'` | 字面文本 | `KW_LET: 'let'` |

`# 标签` 最关键：它让同一规则的不同备选拥有**各自的 Context 类和 visit 方法**。
如 `# AssignmentExpression` → 生成 `AssignmentExpressionContext` →
我们重写 `visitAssignmentExpression`。没有标签就只能整个 expression 共用一个 visit。

### 3. expression 规则：优先级就写在备选顺序里

`grammar/RustParser.g4:440`，左递归规则，**越靠前的备选优先级越高**：

```
① 后缀：方法调用 .f() / 字段 .x / 函数调用 f() / 下标 a[i]     （最高）
② 一元：&  &mut  *  -  !
③ *  /  %
④ +  -
⑤ <<  >>
⑥ &   （位与）
⑦ ^   （位异或）
⑧ |   （位或）
⑨ 比较：==  !=  >  <  >=  <=
⑩ &&
⑪ ||
⑫ 范围：..  ..=
⑬ 赋值 = 与复合赋值 +=  -=  …                                 （最低）
```

读法练习：给 `a + b > c && ok`，从表达式备选表里找到各运算符所在行，
行号大的（优先级低）是树根：`&&` > `>` > `+`，树就是 `&&(...)` 套 `>(...)` 套 `+(...)`。

### 4. 必须眼熟的核心规则

**顶层结构**
```
crate: innerAttribute* items? EOF
item → visItem | macroItem
visItem → function_ | struct_ | enumeration | constantItem | staticItem | trait_ | implementation | ...
```

**语句**（`RustParser.g4:422`，4+1 个备选）
```
statement
    : SEMI                  // 空语句
    | item                  // 函数/struct 也可以定义在块里！
    | letStatement          // let 声明
    | expressionStatement   // 表达式语句
    | macroInvocationSemi   // 宏语句（println! 之类）
```

**expressionStatement 的两个备选**（`:434`，高频考点）
```
expressionStatement
    : expression SEMI          // 普通表达式必须带分号
    | expressionWithBlock SEMI?// if/while/loop/match/块：分号可有可无
```
这就是为什么 `if cond { }` 后面不用写分号，而 `foo();` 必须写。

**块与尾表达式**（`:537`）
```
blockExpression: LCURLYBRACE statements? RCURLYBRACE
statements: statement+ expression?    ← 最后一个不带分号的 expression 是尾表达式
```
`{ a + b }` 走 `expression?` 分支；`{ a + b; }` 里 `a + b;` 只是普通 statement。
**这条规则就是 AST 里 Block::tailExpr 的语法依据。**

**let 声明**（`:430`）：类型标注和初值各自可选，但**分号必须**
```
letStatement: KW_LET patternNoTopAlt (COLON type_)? (EQ expression)? SEMI
```

**expressionWithBlock 的成员**（`:505`）：blockExpression、loopExpression、
ifExpression、matchExpression（加上课程不做的 async/unsafe/if-let）。
判断一段代码是不是 expressionWithBlock，就看它在不在这个名单里。

**matchArms 的不对称结构**（`:692`，我们踩过的坑）
```
matchArms: (matchArm FATARROW matchArmExpression)* matchArm FATARROW expression COMMA?
matchArmExpression: expression COMMA | expressionWithBlock COMMA?
```
前 n-1 个分支体在 matchArmExpression 里，最后一个直接挂在 matchArms 下。

### 5. 练习：手工走一遍推导

给 `let x = 1 + 2;`，应该能写出这条链：

```
crate → items → item → visItem → function_ → blockExpression
  → statements → statement → letStatement
    → KW_LET patternNoTopAlt (→...→ identifier x)
    → EQ expression → expression(PLUS)
      ├── expression → literalExpression → INTEGER_LITERAL 1
      └── expression → literalExpression → INTEGER_LITERAL 2
    → SEMI
```

考试若给一段代码问"它匹配哪条规则的哪个备选"，方法就是：
先认顶层（item？statement？），再顺着备选逐个排除。

---

## 三、AST 怎么画（核心技能）

### 画图三步法

1. **读代码，找语义结构**：这句话"是什么"？（赋值？调用？循环？）它有哪些
   组成部分？（条件、循环体、操作数……）
2. **对照 g4 确认走哪条规则**（有助于确定边界，比如 else 挂哪）。
3. **画节点**：一个语义成分一个节点，子节点就是它的组成部分。
   优先级高的运算画在**下层**（先算的在下面）。

### 约定

- 节点写类型名 + 关键属性，如 `BinaryExpr op="+"`；
- 省略语法噪音：分号、括号、逗号不进 AST；
- 语句列表按顺序排成父节点的多个子节点。

### 例 1：优先级 `let x = 1 + 2 * 3;`

```
LetStmt name="x"
└── BinaryExpr op="+"
    ├── Literal 1
    └── BinaryExpr op="*"
        ├── Literal 2
        └── Literal 3
```

要点：`*` 优先级高 → 在树下层；执行时自底向上求值，正好先乘后加。
**易错**：画成 `(1+2)*3` 的形状就错了。

### 例 2：赋值与复合赋值 `a = b + 1;` / `a += b;`

```
AssignExpr               CompoundAssignExpr op="+="
├── PathExpr a           ├── PathExpr a
└── BinaryExpr op="+"    └── PathExpr b
    ├── PathExpr b
    └── Literal 1
```

要点：赋值是**表达式**（不是语句），外层若有分号会再包一层 ExprStmt。
赋值右结合：`a = b = 1` 的画法是 `a = (b = 1)`，等号右边在下层。

### 例 3：if / else if / else

```rust
if x > 0 { y += 1; } else if x < 0 { y -= 1; } else { }
```

```
IfExpr
├── cond: BinaryExpr op=">"  (x, 0)
├── then: Block
│   └── ExprStmt: CompoundAssignExpr (y, 1)
└── else: IfExpr            ← else if 链：else 的子节点是另一个 IfExpr
    ├── cond: BinaryExpr op="<"  (x, 0)
    ├── then: Block (y -= 1)
    └── else: Block （空）
```

要点：**else if 不是特殊节点**，就是 else 分支里嵌套了一个 IfExpr。

### 例 4：while / loop

```
WhileExpr                  LoopExpr
├── cond: BinaryExpr       └── body: Block
└── body: Block            （loop 没有条件子节点）
```

### 例 5：函数定义

```rust
fn add(a: i32, b: i32) -> i32 { a + b }
```

```
Function name="add" ret="i32"
├── params: [a: i32, b: i32]
└── Block
    └── tailExpr: BinaryExpr op="+"   ← 不带分号的尾表达式，是块的值
        ├── PathExpr a
        └── PathExpr b
```

要点（高频考点）：**Rust 块里最后一条不带分号的表达式是尾表达式**，
它是块的值；`{ a + b }` 和 `{ a + b; }` 的 AST 不一样——后者是
ExprStmt 且块值为 unit。

### 例 6：调用与方法调用

```
let n = p.norm2();        foo(x, y);
CallExpr                  MethodCallExpr method="norm2"
├── func: PathExpr foo    ├── receiver: PathExpr p
└── args: [x, y]          └── args: []  （self 不进 args）
```

### 例 7：enum 与 match

```rust
enum Cell { X, O, Empty }
match c { Cell::X => 1, _ => { 0 } }
```

```
EnumDef Cell
├── variant X（无负载）
├── variant O
└── variant Empty

MatchExpr
├── scrutinee: PathExpr c
├── arm Cell::X => Literal 1
└── arm _      => Block (Literal 0)
```

要点：tuple 式负载 `Circle(f64)` 在变体节点下挂类型子节点；
match 分支体是块就挂 Block，是普通表达式就直接挂表达式。

### 例 8：一元/引用/解引用

```rust
let r = &x;  *m = 10;  -a
UnaryExpr op="&"    AssignExpr            UnaryExpr op="-"
└── PathExpr x      ├── UnaryExpr op="*"  └── PathExpr a
                    │   └── PathExpr m
                    └── Literal 10
```

要点：`-`、`!`、`*`（解引用）、`&`/`&mut`（引用）都是一元运算，
统一 UnaryExpr 节点，区别只在 op。

### 例 9：混合优先级 `a + b > c * 2 && ok`

```
BinaryExpr op="&&"
├── BinaryExpr op=">"
│   ├── BinaryExpr op="+"  (a, b)
│   └── BinaryExpr op="*"  (c, 2)
└── PathExpr ok
```

要点：优先级 `* / %` > `+ -` > 比较 `> < == !=` > `&&` > `||` > 赋值。
一道题里出现多组运算符时，先按优先级分层，最**低**优先级的运算符在树根。

### 例 10：连续赋值（右结合）`a = b = 0;`

```
AssignExpr
├── PathExpr a
└── AssignExpr          ← 右结合：右边的赋值在下层
    ├── PathExpr b
    └── Literal 0
```

对比：算术是左结合，`a - b - c` 画成 `(a - b) - c`，即左下的子树更深：

```
BinaryExpr op="-"
├── BinaryExpr op="-"   ← 左结合：左边的运算在下层
│   ├── PathExpr a
│   └── PathExpr b
└── PathExpr c
```

### 例 11：括号 `(a + b) * c`

```
BinaryExpr op="*"
├── BinaryExpr op="+"   ← 括号改变了默认优先级，AST 直接反映分组结果
│   ├── PathExpr a
│   └── PathExpr b
└── PathExpr c
```

要点：**AST 里没有"括号节点"**，括号的作用完全体现在树的形状上。

### 例 12：return / break / continue

```rust
fn early(x: i32) -> i32 {
    if x < 0 { return 0; }
    loop { if x > 5 { break; } x += 1; continue; }
    x
}
```

```
Function early
└── Block
    ├── IfExpr
    │   ├── cond: BinaryExpr op="<" (x, 0)
    │   └── then: Block
    │       └── ExprStmt: ReturnExpr
    │           └── Literal 0        ← return 的值是它的子节点
    ├── ExprStmt: LoopExpr
    │   └── Block
    │       ├── IfExpr (... break → BreakExpr，无子节点)
    │       ├── ExprStmt: CompoundAssignExpr (x, 1)
    │       └── ExprStmt: ContinueExpr（无子节点）
    └── tailExpr: PathExpr x
```

要点：ReturnExpr 带一个可选的值子节点（`return;` 则没有）；
BreakExpr / ContinueExpr 是叶子（课程范围内不带标签和值）。

### 例 13：struct 全家桶

```rust
struct Point { x: i32, y: i32 }
impl Point {
    fn norm2(&self) -> i32 { self.x * self.x + self.y * self.y }
}
let mut p = Point { x: 1, y: 2 };
p.x += 1;
```

```
StructDef Point                ImplBlock for Point
├── field x: i32               └── Function norm2 (&self, ret i32)
└── field y: i32                   └── Block → tailExpr:
                                       BinaryExpr op="+"
                                       ├── BinaryExpr op="*" (self.x, self.x)
                                       └── BinaryExpr op="*" (self.y, self.y)

LetStmt p                        ExprStmt
└── StructLiteralExpr Point      └── CompoundAssignExpr op="+="
    ├── x: Literal 1                 ├── FieldExpr (p . x)
    └── y: Literal 2                 └── Literal 1
```

要点：字段访问 `self.x` 是 FieldExpr（object + 字段名）；
struct 字面量每个字段挂一个值表达式；`Point { x, y }` 简写等价于 `x: x`。

### 例 14：数组与下标

```rust
let arr: [i32; 5] = [1, 2, 3, 4, 5];
arr[0] = arr[1] + arr[2];
```

```
LetStmt arr (type "[i32; 5]")     ExprStmt
└── ArrayExpr (5 个 Literal)      └── AssignExpr
                                      ├── IndexExpr
                                      │   ├── PathExpr arr
                                      │   └── Literal 0
                                      └── BinaryExpr op="+"
                                          ├── IndexExpr (arr, 1)
                                          └── IndexExpr (arr, 2)
```

要点：数组类型标注 `[i32; 5]` 存在 LetStmt 的类型字符串里；
`arr[i]` 是 IndexExpr（数组表达式 + 下标表达式）。

### 例 15：const / static

```rust
const MAX: i32 = 100;
static mut COUNT: i32 = 0;
```

```
ConstDef MAX (type i32)           StaticDef COUNT (type i32, mut)
└── Literal 100                   └── Literal 0
```

要点：它们是 Item（和函数、struct 平级，挂在 Crate 下），不是 LetStmt；
StaticDef 多一个 isMut 标记。

### 例 16：if 作为值 + 嵌套块

```rust
let b = if x > 0 { 1 } else { -1 };
```

```
LetStmt b
└── IfExpr                        ← if 是表达式，可以直接当初值
    ├── cond: BinaryExpr op=">" (x, 0)
    ├── then: Block → tailExpr: Literal 1
    └── else: Block → tailExpr: UnaryExpr op="-" (Literal 1)
```

要点：两个分支块的**尾表达式**就是 if 表达式的值，这与例 3 里
块中只有语句的 if 形状不同——注意区分"语句位置的 if"和"值位置的 if"。

### 例 17：宏调用 `println!("x = {}", x);`

```
ExprStmt
└── MacroInvoke "println!"
    └── args 原文: "x = {}", x   （宏参数不按表达式解析，原样保留）
```

要点：宏调用不走 CallExpr；注意 `println!` 是宏、`printlnInt` 是普通函数
（后者画成 CallExpr），两者 AST 节点不同。

### 例 18：match 分支体的两种形态

```rust
match c {
    Cell::X => 1,
    Cell::O => { let t = 2; t },
    _ => if flag { 3 } else { 4 }
}
```

```
MatchExpr
├── scrutinee: PathExpr c
├── arm Cell::X => Literal 1            ← 普通表达式体
├── arm Cell::O => Block                ← 块体：多条语句 + 尾表达式
│   ├── LetStmt t (Literal 2)
│   └── tailExpr: PathExpr t
└── arm _ => IfExpr                     ← 体也可以是 if（仍是表达式）
    ├── cond: PathExpr flag
    ├── then: Block (Literal 3)
    └── else: Block (Literal 4)
```

### 例 19：一元运算叠层 `*&x`、`- -a`、`!flag`

```
UnaryExpr op="*"            UnaryExpr op="-"        UnaryExpr op="!"
└── UnaryExpr op="&"        └── UnaryExpr op="-"    └── PathExpr flag
    └── PathExpr x              └── PathExpr a
```

要点：一元运算从右往左结合（靠近变量的先算），树上体现为右深嵌套。

### 例 20：完整小程序（综合演练）

```rust
const LIMIT: i32 = 10;

fn count(mut x: i32) -> i32 {
    while x < LIMIT {
        if x == 5 { break; }
        x += 1;
    }
    x
}

fn main() {
    let r = count(0);
    printlnInt(r);
}
```

```
Crate
├── ConstDef LIMIT = 100
├── Function count (param x: i32, ret i32)
│   └── Block
│       ├── ExprStmt: WhileExpr
│       │   ├── cond: BinaryExpr op="<" (x, LIMIT)
│       │   └── Block
│       │       ├── IfExpr (x == 5 → Block{ BreakExpr })
│       │       └── ExprStmt: CompoundAssignExpr (x, 1)
│       └── tailExpr: PathExpr x
└── Function main
    └── Block
        ├── LetStmt r = CallExpr(count, [Literal 0])
        └── ExprStmt: CallExpr(printlnInt, [PathExpr r])
```

---

## 四、常见问答速查

| 问题 | 答案要点 |
|---|---|
| g4 里 `# Xxx` 标签是什么？ | labeled alternative，给规则的一个备选起名，生成 `XxxContext` 类和对应 visit 方法，区分同一规则的不同形态 |
| 终端节点/非终端节点？ | token（词法符号）是解析树叶子；规则名是非终端 |
| 解析树和 AST 的区别？ | 解析树忠实于语法推导、含冗余；AST 只留语义、结构精简 |
| visit 自顶向下还是自底向上？ | 调用自上而下递归，但**构造**自底向上：先造子节点再拼父节点 |
| 为什么 if/while/loop 是 expressionWithBlock？ | 它们以块结尾、不需要分号，且可以出现在表达式位置（Rust 里它们都是表达式） |
| AST 里括号去哪了？ | 括号只影响解析时的分组，分组结果已体现在树形里，AST 不存括号节点 |
| 语法错误怎么处理？ | ANTLR 语法分析阶段报错（我们约定 parse 失败直接终止），进不了 AST 阶段 |
| `any_cast` 抛 bad_any_cast 说明什么？ | 一般是某个语法结构没覆盖到，visit 返回了空 any 或类型不对 |

---

## 五、自测练习（画完对照 `./build/compiler <file>` 的输出）

1. `let z: i32 = a + b * c - d;`
2. `if ok { return; }`
3. `while i < 10 { y += i; i += 1; }`
4. `let b = if x > 0 { 1 } else { -1 };`
5. `p.shift(3);`
6. `enum Shape { Circle(f64), Empty }` + `match s { Shape::Circle(r) => r, _ => 0.0 }`
7. `a = b = c + 1;`（右结合 + 优先级混合）
8. `!(a > b) && flag`（一元 + 比较 + 逻辑三层）
9. `arr[i + 1] = arr[i] * 2;`（下标里套运算）
10. `struct R { w: i32 }` + `let r = R { w };`（简写字段）
11. `loop { if done { break; } continue; }`（控制流叶子节点）
12. `fn f() -> i32 { let x = 1; x }` vs `fn f() -> i32 { let x = 1; x; }`
    （尾表达式有无，经典陷阱）

验证命令：
```bash
./build/compiler --tree <file.rs>   # 看 ANTLR 解析树（冗余、忠实于 g4）
./build/compiler <file.rs>          # 看我们的 AST（精简、语义化）
```
对比两棵树就是"为什么需要 AST"的直观答案。
