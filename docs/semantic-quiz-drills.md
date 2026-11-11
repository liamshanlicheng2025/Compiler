# Semantic 小测刷题集（判断/分析题）

> 答案一律以**课程 SPEC** 为准（中译见 `docs/spec-zh/`）。
> 大部分题的答案已用我们自己的编译器实际跑过验证（标注 ✅通过 / ❌拒绝）；
> 少数题我们的编译器与 SPEC 有偏差，会标注 ⚠️ 并以 SPEC 答案为准
> （这些偏差测试集没覆盖，239/239 也没暴露，小测请以 SPEC 为准）。
> 题目代码里的 `// 答` 之前先自己判断。

---

## A 组：判断对错（能通过语义检查吗？）

### A1
```rust
fn main() { let x: i32 = loop { break loop {}; }; }
```
<details><summary>答案</summary>
✅ 正确。内层 `loop {}` 无 break，发散，类型 `!`；`break <!>` 使外层 loop 类型为 `!`，`!` 可 coerce 到 `i32`。
（`r[expr.loop.infinite.diverging]` + `r[coerce.types.never]`）
</details>

### A2
```rust
fn main() { if (true) { 1 } else { 2 } }
```
<details><summary>答案</summary>
❌ 错误。带块表达式（if）当语句用时类型必须是 `()`，这里两臂是 `i32`。
加分号 `if (true) { 1 } else { 2 };` 就正确（值被丢弃）。
（`r[statement.expr.constraint-block]`，编译器验证 ❌拒绝）
</details>

### A3
```rust
fn main() { let z: i32 = while (false) { break 5; }; }
```
<details><summary>答案</summary>
❌ 错误。`while` 永远求值为 `()`，break 不能给它传值；`()` 不能赋给 `i32`。
改成 `loop` 就正确。（`r[expr.loop.while.eval]` / `r[expr.loop.explicit-result]`，验证 ❌拒绝）
</details>

### A4
```rust
fn main() { let a: &i32 = &mut 5; }
```
<details><summary>答案</summary>
✅ 正确。带类型标注的 `let` 是 coercion site，`&mut i32 → &i32` 是允许的 coercion。
（`r[coerce.site.let]` + `r[coerce.types.mut-reborrow]`，验证 ✅通过）
</details>

### A5
```rust
fn main() { let mut x: i32 = 1; let a = &mut x; let b: &i32 = a; }
```
<details><summary>答案</summary>
✅ 正确。同上：`a` 的类型是 `&mut i32`，let 标注处 coerce 为 `&i32`。（验证 ✅通过）
</details>

### A6
```rust
fn main() { let mut x: i32 = 1; let arr = [&mut x, &x]; }
```
<details><summary>答案</summary>
✅ 正确。数组元素类型用 **LUB** 合并：LUB(`&mut i32`, `&i32`) = `&i32`。
（`r[coerce.least-upper-bound]`"为数组元素找公共类型"，验证 ✅通过）
</details>

### A7 ⚠️
```rust
const C: bool = 1 < 2;
fn main() { }
```
<details><summary>答案</summary>
❌ 错误（按 SPEC）。常量表达式里**算术**运算符可以，但**比较/逻辑/短路**运算符永远不行。
对比：`const N: usize = 1 + 2;` 正确。
（`r[const-eval.const-expr.builtin-arith-logic]` vs `r[const-eval.const-expr.builtin-logic]`）
⚠️ 我们的编译器目前会放行这题（实现偏差），小测按 SPEC 答"不合法"。
</details>

### A8 ⚠️
```rust
struct Foo { x: i32 }
fn main() { let a = [Foo { x: 1 }; 3]; }
```
<details><summary>答案</summary>
❌ 错误（按 SPEC）。重复形式 `[v; N]` 当 N>1 时，要求 `v` 的类型实现 `Copy`，
或 `v` 是常量表达式/常量路径。`Foo` 不 Copy，结构体表达式也不是常量表达式。
对比：`[0; 3]` 正确（i32 是 Copy）。
（`r[expr.array.repeat-copy]`）
⚠️ 我们的编译器在旧方言下放行了这题（rx 方言下会正确拒绝），小测按 SPEC 答"不合法"。
</details>

### A9
```rust
fn main() { let mut x: i32 = 0; let y: () = x = 8; }
fn main() { let mut x: i32 = 0; let z: i32 = x = 8; }   // 另一版本
```
<details><summary>答案</summary>
第一个 ✅，第二个 ❌。赋值表达式的值恒为 `()`（和 C/C++ 不同！），
所以能赋给 `()` 不能赋给 `i32`。（`r[expr.assign.result]`，验证 q11a ✅ / q11b ❌）
</details>

### A10
```rust
fn main() { let x: i32 = { 2 }; }
fn main() { let x: i32 = { 2; }; }   // 另一版本
```
<details><summary>答案</summary>
第一个 ✅，第二个 ❌。块的类型 = 尾表达式类型；`{ 2; }` 里 `2;` 是语句，
块没有尾表达式，类型是 `()`。（`r[expr.block.type]`，验证 q12a ✅ / q12b ❌）
</details>

### A11
```rust
fn main() { let x: i32 = if (true) { 1 }; }
```
<details><summary>答案</summary>
❌ 错误。没有 else 的 `if` 类型必须是 `()`（条件不成立时值为 `()`），
而 then 臂是 `i32`。（`r[expr.if.result]` / `r[expr.if.type]`，验证 ❌拒绝）
</details>

### A12
```rust
fn main() { let b = 1 < 2 < 3; }
```
<details><summary>答案</summary>
❌ 错误。比较运算符不能链式，必须加括号：`(1 < 2) < 3`
（但那样是 bool < i32，类型也错）。（`r[expr.cmp.paren-chaining]`，验证 ❌拒绝）
</details>

### A13
```rust
fn main() { let x: i32 = 1; x = 2; }
fn main() { let mut x: i32 = 1; let r = &x; *r = 2; }      // 版本二
fn main() { let mut x: i32 = 1; let r = &mut x; *r = 2; }  // 版本三
```
<details><summary>答案</summary>
❌ / ❌ / ✅。变量默认不可变；`&x` 是共享引用，解引用不可写；
只有 `&mut` 引用的解引用才可写。（`r[expr.mut.*]`、`r[expr.deref.mut]`，验证 q15a ❌ / q15b ❌ / q15c ✅）
</details>

### A14 ⚠️
```rust
const X: i32 = 1;
fn main() { let X: i32 = 2; }
```
<details><summary>答案</summary>
❌ 错误（按 SPEC）。局部绑定可以遮蔽函数等名字，但**不能遮蔽 const 条目**
（也不能遮蔽 struct/enum 构造器）。
（`r[names.scopes.pattern-bindings.shadow]`）
⚠️ 我们的编译器放行了这题（实现偏差），小测按 SPEC 答"不合法"。
</details>

### A15
```rust
fn foo() { }
fn main() { let foo: i32 = 1; }
```
<details><summary>答案</summary>
✅ 正确。局部绑定遮蔽函数是允许的（被禁的只有 const 和构造器）。
（`r[names.scopes.pattern-bindings.shadow]`，验证 ✅通过）
</details>

### A16
```rust
struct Foo { x: i32 }
fn Foo() { }
fn main() { }
```
<details><summary>答案</summary>
✅ 正确。struct 在**类型命名空间**，函数在**值命名空间**，互不冲突。
（`r[names.namespaces.kinds]`，验证 ✅通过。这就是 cr 考过的"int a 和 fn a 同名"）
</details>

### A17 ⚠️
```rust
struct Foo { x: i32 }
fn main() { let a = [Foo { x: 1 }]; let b = a[0]; }
```
<details><summary>答案</summary>
❌ 错误（按 SPEC）。`Foo` 不是 Copy，读取数组元素需要 move，
而**数组元素不在"允许 move 出"的四种 place 之列**（变量、临时值、字段、Box 解引用）。
对比：`let b = a[0].x;`（i32 是 Copy）就正确。
（`r[expr.move.movable-place]`）
⚠️ 我们的编译器放行了这题（实现偏差），小测按 SPEC 答"不合法"。
</details>

### A18 ⚠️
```rust
struct Cookie;
fn main() { let c = [Cookie, Cookie {}, Cookie, Cookie {}]; }
```
<details><summary>答案</summary>
✅ 正确（按 SPEC）。unit-like struct 隐式定义一个同名同类型的 const，
所以 `Cookie`（路径，值命名空间的 const）和 `Cookie {}`（结构体表达式）是同一个值。
（`r[items.struct.unit]`）
⚠️ 我们的编译器拒绝了这题（没实现 unit struct 的隐式 const），小测按 SPEC 答"合法"。
</details>

### A19
```rust
fn main() { break; }
```
<details><summary>答案</summary>
❌ 错误。`break`/`continue` 只能出现在循环体内，且关联最内层循环。
（`r[expr.loop.break.value]` / `r[expr.loop.continue.in-loop-only]`，验证 ❌拒绝）
</details>

### A20
```rust
struct P { x: i32 }
fn main() { let p = P { x: 1 }; p.x = 2; }       // 版本一
fn main() { let mut p = P { x: 1 }; p.x = 2; }   // 版本二
```
<details><summary>答案</summary>
❌ / ✅。字段的可变性**继承**自容器：根变量 `p` 不是 mut，整条路径都不可写。
（`r[expr.mut.valid-places]`，验证 q22a ❌ / q22b ✅）
</details>

### A21
```rust
fn main() { let b: bool = 'a'; }
fn main() { if (1) { } }   // 另一版本
```
<details><summary>答案</summary>
都 ❌。`char` 和 `bool` 是不同类型，没有隐式转换；if 条件必须是 `bool`，`i32` 不行。
（`r[expr.if.condition]`，验证 q23 ❌ / q24 ❌）
</details>

### A22
```rust
fn main() { let x: i32 = -2147483648; }
```
<details><summary>答案</summary>
✅ 正确。这是取负运算作用于字面量 `2147483648`（字面量本身按类型截断已是 i32 最小值），
属于**字面量豁免**，不溢出。（`r[expr.operator.int-overflow.unary-neg]` 的 NOTE，验证 ✅通过）
</details>

### A23
```rust
fn main() {
    loop {
        loop {};
        break;
    }
}
```
<details><summary>答案</summary>
✅ 正确。`loop {};` 作为语句，类型 `!` 可 coerce 到 `()`；
后面的 `break;` 不可达，但不可达不是错误。外层 loop 类型 `()`。
（验证 ✅通过。经典陷阱题）
</details>

---

## B 组：分析/简答题

### B1 求值顺序
```rust
let mut x: i32 = 1;
x = { x = 2; 3 };
```
执行后 `x` 是多少？
<details><summary>答案</summary>
**3**。赋值表达式**先求值右操作数**（与其他表达式相反）：右边块先把 `x` 改成 2、
产出值 3；然后执行赋值，`x = 3`。（`r[expr.assign.evaluation-order]`）
</details>

### B2 方法查找流程
接收者类型是 `Box<[i32; 2]>` 时，写出方法查找的候选接收者类型列表（前 6 个）。
<details><summary>答案</summary>
`Box<[i32;2]>` → `&Box<[i32;2]>` → `&mut Box<[i32;2]>` → `[i32;2]`（解引用）→
`&[i32;2]` → `&mut [i32;2]` →（之后是 unsized coercion 得 `[i32]`、`&[i32]`、`&mut [i32]`）。
规则：反复解引用，每个类型 T 后面紧跟 `&T`、`&mut T`；按序先查固有方法再查 trait 方法；
出现多个候选就是错误。（`r[expr.method.candidate-receivers]`）
</details>

### B3 place / value 分类
判断下列表达式是 place 还是 value：`x`、`*p`、`a[0]`、`s.f`、`f()`、`1 + 2`、`(x)`、`{ x }`、`&x`、`Foo { x: 1 }`。
<details><summary>答案</summary>
place：`x`、`*p`、`a[0]`、`s.f`、`(x)`（括号不改变类别）。
value：`f()`、`1 + 2`、`{ x }`（块总是 value）、`&x`、`Foo { x: 1 }`。
（`r[expr.place-value.place-expr-kinds]` / `r[expr.block.value]` / `r[expr.paren.place-or-value]`）
</details>

### B4 命名空间
为什么 `struct Foo { x: i32 } fn Foo() {}` 能共存？`Foo { x: 1 }` 和 `Foo()` 各自查哪个命名空间？
<details><summary>答案</summary>
struct 声明进**类型命名空间**，函数声明进**值命名空间**，两个命名空间独立所以同名不冲突。
`Foo { x: 1 }` 在类型位置查类型命名空间；`Foo()` 在值位置（调用）查值命名空间。
（`r[names.namespaces.kinds]`）
</details>

### B5 LUB 计算
`let mut x: i32 = 1; let y = if c { &mut x } else { &x };`，`y` 的类型是什么？写出 LUB 迭代过程。
<details><summary>答案</summary>
类型 `&i32`。过程：目标 T_t 初始为 T0 = `&mut i32`；看 T1 = `&i32`：
`&i32` 不能 coerce 到 `&mut i32`；反过来 `&mut i32` 可以 coerce 到 `&i32`，
于是 T_t 换成 `&i32`。结果 `&i32`。（`r[coerce.least-upper-bound.computation-*]`）
</details>

### B6 隐式借用
`let a: &[i32] = ...; let b: &[i32] = ...; *a == *b;`
为什么这里 `*a`、`*b` 不会被 move？等价于什么写法？
<details><summary>答案</summary>
因为**比较运算符隐式借用它的操作数**，在 place context 求值，不需要 move。
等价于 `PartialEq::eq(&*a, &*b)`。（`r[expr.cmp.place]` / `r[expr.implicit-borrow.application]`）
</details>

### B7 const vs static
说出 const 和 static 的三个区别。
<details><summary>答案</summary>
① const 无固定内存位置、使用处内联复制；static 有唯一地址，所有引用指向同一分配。
② const 的最终值不能含 `&mut`；static 可以是 mut（读写需 unsafe）。
③ static 有 `'static` 生命周期、程序结束不 drop；const 没有位置可谈生命周期。
（`r[items.const.behavior]` / `r[items.static.intro]` / `r[items.static.lifetime]`）
</details>

### B8 溢出判断
debug 模式下，下列哪些会 panic？`2147483647i32 + 1`、`-(-2147483648i32)`、`-2147483648i32 / -1`、`1 << 32`、`-128_i8`（假设有 i8）。
<details><summary>答案</summary>
全部 panic，**除了** `-128_i8`。
原因：加法和一元取负超出范围；`INT_MIN / -1` 是特例（即使关掉 overflow-checks 也查）；
移位量 ≥ 位数。而 `-128_i8` 是一元 `-` 直接作用于字面量，属于字面量豁免。
（`r[expr.operator.int-overflow.*]`）
</details>

### B9 常量表达式判断
下列哪些是常量表达式（操作数本身合法的前提下）？
`1 + 2`、`[1, 2]`、`a[0]`（usize 下标）、`x < y`、`Foo { x: 1 }`、`&s`、`x as i32`、`{ 5 }`、`*p`、`N`（指向 const 的路径）。
<details><summary>答案</summary>
是：`1 + 2`（算术）、`[1, 2]`（数组）、`a[0]`（usize 下标）、`N`（常量路径）。
不是：`x < y`（比较）、`Foo { x: 1 }`（结构体表达式）、`&s`（借用，`&str` 除外）、
`x as i32`（cast）、`{ 5 }`（块）、`*p`（解引用）。
（`r[const-eval.const-expr.list]` 对照"never"清单）
</details>

### B10 可移动性
`let v = vec![Foo{x:1}, Foo{x:2}]; let a = v.remove(0);` 为什么合法，
而 A17 的 `let b = a[0];` 不合法？
<details><summary>答案</summary>
`remove` 是方法调用，返回值来自 Vec 内部实现——对调用者来说它是一个**临时值/普通返回值**，
属于值表达式结果，不涉及"从 place move 出"的限制。
而 `a[0]` 是 place 表达式，读取非 Copy 类型就要 move，数组元素不在允许 move 的清单里。
（`r[expr.move.movable-place]`）
</details>
