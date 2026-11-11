# Semantic 小测考点清单

> 依据：课程版 SPEC（`reference/RCompiler-Spec/src/`，中译见 `docs/spec-zh/`）。
> 按考察概率从高到低排列。每条给出：规则、SPEC 出处（`r[...]` 编号）、正误例、可能考法。
> 已确认不考的：enum/match（助教明确去掉）、trait 系统（derive 之外）、泛型、闭包、宏、生命周期推导。

---

## 第一梯队：几乎必考

### 1. Coercion（类型强制转换）与 LUB ★★★

SPEC：`type-coercions.md`，`r[coerce.site.*]`、`r[coerce.types.*]`、`r[coerce.least-upper-bound.*]`

**考点 A：coercion 只能发生在特定的 coercion site**——let 带显式类型标注、const/static 声明、函数实参、struct 字段实例化、函数返回值（块尾表达式和 return）。**没有 coercion site 就没有 coercion**。

**考点 B：允许哪些 coercion**（课程范围内实际就这几条）：
- 自反：`T` 是 `U` 的子类型（基本就是类型相等）
- `&mut T` → `&T`（经典例题）
- `!`（never 类型）→ 任意类型
- 数组/块/括号会**传播** coercion site 到子表达式

```rust
let x: &i32 = &mut 42;   // 对：let 有类型标注是 coercion site，&mut i32 -> &i32
fn f(_: &i32) {}
f(&mut 42);              // 对：实参是 coercion site
```

**考点 C：LUB（最小上界）用在哪 5 个地方**——if 各分支、match 各分支（不考）、数组各元素、多 return 的闭包（不考）、多 return 的函数。**数组元素和 if 两臂是课程范围内最可能考的**。

**考点 D：LUB 的迭代算法**——目标类型初始为 T0；对每个新类型 Ti：Ti 能 coerce 到当前目标→不变；否则目标能 coerce 到 Ti→换成 Ti；再不行→找共同父类型/报错。

```rust
let a = if c { &mut x } else { &x };  // LUB(&mut i32, &i32) = &i32
let arr = [&mut x, &x];               // 同理，元素类型 &i32
if c { 1 } else { true }              // 错：i32 和 bool 无公共类型
```

### 2. Place 表达式 vs Value 表达式 ★★★

SPEC：`expressions.md`，`r[expr.place-value.*]`

**考点 A：哪些表达式是 place expression**（背下来）：
- 局部变量、static 变量
- 解引用 `*expr`
- 数组下标 `expr[expr]`
- 字段访问 `expr.f`
- 带括号的位置表达式（括号不改变类别！）
- 其余**全部**是 value expression（字面量、调用、算术、块、if……）

**考点 B：哪些上下文是 place context**：复合赋值左操作数、`&`/`&mut`/`*` 的操作数、字段表达式的操作数、下标的被索引方、隐式借用的操作数。

**考点 C：place 在 value context 中求值 = 读取那个位置的值**（Copy 则复制，否则可能 move）。

**考点 D：只有哪些 place 能被 move 出**：未被借用的变量、临时值、可移出且不实现 Drop 的字段、对 `Box<T>` 解引用的结果。**数组元素不能 move 出**（只能 Copy）。

### 3. 可变性（Mutability）规则 ★★★

SPEC：`expressions.md` `r[expr.mut.*]`、`variables.md`

**考点 A：什么操作要求"可变位置表达式"**：赋值、可变借用 `&mut`、隐式可变借用、`ref mut` 绑定。

**考点 B：哪些是可变位置表达式**（背）：
- 未被借用的 **mut 变量**
- mut static、临时值
- 字段（可变性**继承**自容器）
- `*mut T` 或 `&mut T` 的解引用
- 实现 IndexMut 的下标

**考点 C：变量默认不可变**；`mut` 只作用于紧随其后的那个绑定（`fn f(mut x: i32, y: i32)` 只有 x 可变）。

```rust
let x = 1;
x = 2;           // 错：x 不可变
let mut y = 1;
y = 2;           // 对
let r = &y;
*r = 3;          // 错：r 是 &i32，解引用不可写
let m = &mut y;
*m = 3;          // 对
```

### 4. 语句与分号规则 ★★★

SPEC：`statements.md` `r[statement.*]`、`block-expr.md` `r[expr.block.type]`

**考点 A：三种语句**：item 声明、let 语句、表达式语句。

**考点 B：ExpressionWithBlock（if/while/loop/块）当语句用时可省略分号，但此时类型必须是 `()`**；ExpressionWithoutBlock 必须有分号才是语句。

```rust
if true { 1 } else { 2 }    // 作为语句：错！两臂是 i32 不是 ()
if true { 1 } else { 2 };   // 加了分号：对（值被丢弃）
if true { }                 // 对：类型 ()
foo();                      // 对：普通表达式语句
```

**考点 C：块的类型 = 尾表达式的类型；没有尾表达式就是 `()`**。

**考点 D：let 语句必须显式标注类型、必须有初始化表达式**（课程 spec 的简化规定！和标准 Rust 不同，容易挖坑）。

### 5. 运算符语义 ★★★

SPEC：`operator-expr.md`

**考点 A：赋值/复合赋值的求值顺序——先右后左**（与其他表达式相反！）。`a = b` 先算 b 再算 a。

**考点 B：赋值表达式和复合赋值表达式的值都是 `()`**（和 C/C++ 不同！）。`z = x = y;` 合法，z 的类型是 `()`。

**考点 C：短路求值**：`&&` 只在左边为 true 时算右边；`||` 只在左边为 false 时算右边。对比 `&`/`|`（布尔上）两边都算。if/while 必须支持短路。

**考点 D：比较运算符不能链式**：`a == b == c` 非法，必须 `(a == b) == c`。

**考点 E：比较运算符隐式借用操作数**（在 place context 求值，不 move）；算术/逻辑运算符在 value context 求值（move/copy）。

**考点 F：溢出规则**（debug 下 panic）：
- `+` `-` `*` 超范围
- 一元 `-` 作用于有符号最小值（**字面量豁免**：`-128_i8` 合法）
- `INT_MIN / -1`、`INT_MIN % -1`
- 移位量 ≥ 位数或为负
- 除零 panic

**考点 G：整数除法向零取整；`%` 与被除数同号；有符号右移是算术右移**。

```rust
-7 / 2    // = -3（向零取整，不是 -4）
-7 % 2    // = -1（与被除数同号）
7 % -2    // = 1
-10 >> 2  // = -3（算术右移）
```

### 6. if / while / loop 表达式 ★★☆

SPEC：`if-expr.md`、`loop-expr.md`

**考点 A：条件必须是 bool 类型**（课程 spec 条件要带括号：`if (expr)`；Rx 新测试集不带括号——如果题目按 SPEC 原文就以带括号为准）。

**考点 B：if 表达式的值 = 被执行块的值；所有分支类型必须一致（LUB）；没有 else 且条件不成立时值为 `()`**（所以无 else 的 if 当表达式用时类型只能是 `()`）。

**考点 C：while 永远求值为 `()`**。

**考点 D：只有 loop 能用 break 带值**；`break` 等价于 `break ()`。带 break 的 loop 不是发散的，类型与各 break 值兼容（LUB）。

**考点 E：break/continue 关联最内层循环；只能出现在循环体内**。

**考点 F：无 break 的无限 loop 类型是 `!`**（发散），`!` 可 coerce 到任何类型。

```rust
let x: i32 = loop { break 42; };        // 对
let y = loop { break; };                // y: ()
let z: i32 = while true { break 1; };   // 错：while 恒为 ()
```

---

## 第二梯队：很可能考

### 7. 命名空间 ★★☆

SPEC：`names/namespaces.md`

**考点 A：类型命名空间装什么**：struct/enum/枚举变体、trait、内建类型（bool/i32/...）、`Self` 类型。
**值命名空间装什么**：函数、const、struct 构造器、枚举变体构造器、关联函数/常量、let 绑定和函数参数。

**考点 B：两个命名空间独立，所以同名不冲突**：

```rust
struct Foo { x: i32 }
fn Foo() {}        // 合法！Foo 同时在类型和值命名空间
```

**考点 C：字段名不属于任何命名空间**，只能通过字段表达式访问。

（这正好对应我们符号表的双 map 设计，cr 也考过"int a 和 function a 同名怎么区分"。）

### 8. 作用域与遮蔽 ★★☆

SPEC：`names/scopes.md`、`statements.md`

**考点 A：模块内条目作用域是整个模块**（包括声明之前！所以函数可以先用后定义）；块内 item 的作用域是整个块。

**考点 B：let 绑定的作用域从 let 之后开始**，到块尾。

**考点 C：遮蔽规则**：局部绑定可以遮蔽几乎一切，**但不能遮蔽 const 条目和 struct/enum 构造器**。

**考点 D：同一模块/块内同一命名空间不允许同名条目**。

### 9. 常量求值（const eval） ★★☆

SPEC：`const_eval.md`

**考点 A：哪些是常量上下文**（只有 3 个）：数组类型长度 `[T; N]`、数组重复长度 `[v; N]`、const 初始化表达式。

**考点 B：哪些表达式可以是常量表达式**（操作数也得是）：字面量（含 `&str`）、指向常量的路径（不允许递归定义）、数组表达式、`usize` 下标、整数/bool/char 上的内建取负和**算术**运算符、括号表达式。

**考点 C：绝不能出现在常量表达式里的**（挑几个记）：块/let/赋值、字段表达式、结构体表达式、if/loop/while、借用（`&str` 除外）、解引用、cast、**逻辑/比较/短路运算符**（注意：算术可以，比较不行！）。

```rust
const N: usize = 1 + 2;      // 对
const A: [i32; N] = [0; N];  // 对
const B: bool = 1 < 2;       // 错！比较运算符不是常量表达式
const C: i32 = { 5 };        // 错！块表达式不行
```

**考点 D：常量上下文中越界/溢出是编译错误**（不是运行期 panic）。

### 10. 数组表达式 ★★☆

SPEC：`array-expr.md`、`types/array.md`

**考点 A：两种形式**：`[a, b, c]` 列举式（各元素 LUB 出公共类型）；`[v; N]` 重复式。

**考点 B：重复式 `[v; N]` 当 N > 1 时要求**：`v` 的类型实现 `Copy`，或 `v` 是常量表达式/常量路径。非常量表达式**只求值一次**再复制；常量表达式**求值 N 次**；N = 0 时不求值。

**考点 C：长度必须是 `usize` 常量表达式**。

**考点 D：下标必须是 `usize`**（Rx 方言下 `v[i32]` 直接报错！）；下标从 0 开始；常量下标编译期查界，否则运行期 panic。

### 11. 隐式借用与自动解引用 ★★☆

SPEC：`expressions.md` `r[expr.implicit-borrow.*]`、`method-call-expr.md`、`field-expr.md`

**考点 A：哪些位置发生隐式借用**：方法调用左操作数、字段访问左操作数、调用左操作数、下标左操作数、`*` 的操作数、**比较运算符的操作数**、复合赋值左操作数。

**考点 B：方法查找的候选接收者列表**（经典流程题）：对接收者类型反复解引用，每个类型 T 后面紧跟 `&T`、`&mut T`；按顺序先查固有方法再查 trait 方法；多候选则报错。

例：接收者 `Box<[i32;2]>` → 候选序列 `Box<[i32;2]>`、`&Box<...>`、`&mut Box<...>`、`[i32;2]`、`&[i32;2]`、`&mut [i32;2]`……

**考点 C：RCompiler 只有 `Box` 支持自动解引用**。

### 12. 移动与复制 ★★☆

SPEC：`expressions.md` `r[expr.move.*]`、`field-expr.md`

**考点 A：Copy 类型读值时复制；非 Copy 则是 move**。

**考点 B：move 出局部变量后该位置变为未初始化，重新初始化前不能再读**。

**考点 C：结构体各字段借用互相独立**（`&mut x.f1` 和 `&x.f2` 可同时存在）；字段可以单独 move 出。

**考点 D：借用规则**：`&` 存在期间原位置不可变（可读可再共享）；`&mut` 存在期间原位置完全不可访问。

---

## 第三梯队：可能考概念题

### 13. 函数字面规则

- 返回类型省略 = `()`（`r[items.fn.implicit-return]`）
- 尾表达式就是隐式 return；显式 return 短路
- 带 self 参数的函数只能出现在 trait/impl 里（`r[items.fn.params.self-restriction]`）
- `self`/`&self`/`&mut self` 三个简写的等价写法

### 14. struct / impl / 关联条目

- struct 声明进**类型命名空间**；unit-like struct 隐式定义同名 const
- struct 内存布局**不规定，自己设计**
- impl 可多个；固有 impl 可含关联函数（方法）和关联常量，**不能含类型别名**
- 方法 = 第一个参数叫 `self` 的关联函数
- 关联常量**被引用时才求值**

### 15. const / static 对比

| | const | static |
|---|---|---|
| 内存位置 | 无固定位置，使用处内联复制 | 有唯一地址 |
| 类型标注 | 必须 | 必须 |
| 初始化 | 常量表达式 | 常量表达式 |
| `&mut` | 最终值不能含 `&mut` | mut static 读写需 unsafe |
| 生命周期 | — | `'static`，程序结束不 drop |

### 16. 字面量类型推断

- 整数无后缀：能唯一确定就用上下文类型；约束不足默认 **i32**；约束过度报错
- 后缀必须合法（`123u32`、`0usize`）
- 进制前缀 0b/0o/0x；下划线分隔符忽略
- `-1i8` 是取负运算 + 字面量，不是一个字面量
- `char` 是 32 位 Unicode 标量；字符串字面量类型 `&'static str`

### 17. 其他零散的

- 括号表达式不改变 place/value 类别（`r[expr.paren.place-or-value]`）
- 借用运算符作用于值表达式时创建临时值（`&7` 合法）
- `&&` 在借用上下文里是两次借用：`&&10` ≡ `& &10`
- 比较运算符的操作数不求值出值，而是隐式借用 → `*a == *b` 不会 move
- `derive`：为数据结构自动生成 impl（PartialEq 逐字段 ==，Clone/Copy 等）
- 变量未初始化：所有可达路径都初始化后才能用（`r[variable.init]`）

---

## 考前自测 10 题（不看答案能做对就稳了）

1. `let y: () = x = 8;` 合法吗？`x = 8` 的类型是什么？
2. `if true { 1 } else { 2 }` 能当语句吗？加分号后呢？
3. `let a: &i32 = &mut 5;` 合法吗？为什么？coercion site 在哪？
4. `[&mut x, &x]` 的元素类型是什么？用的什么规则？
5. `while (true) { break 5; }` 的类型是什么？`loop { break 5; }` 呢？
6. `const C: bool = 1 < 2;` 合法吗？
7. `[String::new(); 3]` 合法吗？`[0; 3]` 呢？为什么？
8. `a == b == c` 合法吗？该怎么写？
9. 局部变量能遮蔽 const 吗？能遮蔽函数吗？
10. `let r = &mut v[0];` 其中 `v: Vec<i32>` 不是 mut 声明，合法吗？

<details>
<summary>答案</summary>

1. 合法，`()`（赋值表达式的值恒为 unit）。
2. 不能（类型 i32 不是 `()`）；加分号后可以，值被丢弃。
3. 合法。let 带类型标注是 coercion site，`&mut i32 → &i32`。
4. `&i32`，LUB coercion。
5. `()`（while 恒为 unit）；`i32`。
6. 不合法，比较运算符不是常量表达式（算术才是）。
7. 不合法，String 不 Copy；`[0; 3]` 合法，i32 是 Copy。
8. 不合法，比较不能链式；写 `(a == b) == c`。
9. 不能遮蔽 const（也不能遮蔽 struct/enum 构造器）；可以遮蔽函数。
10. 不合法，Vec 下标取 `&mut` 要求 Vec 本身可变。
</details>
