# 循环与其他可 break 的表达式（Loops and other breakable expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/loop-expr.md`

r[expr.loop.syntax]
```grammar,expressions
LoopExpression ->
    InfiniteLoopExpression
  | PredicateLoopExpression
```

r[expr.loop.intro]
Rust 支持两种循环表达式：

*   `loop` 表达式表示无限循环。
*   `while` 表达式循环直到条件为假。

r[expr.loop.break-label]
所有循环类型都支持 `break` 表达式。

r[expr.loop.continue-label]
`loop` 和 `while` 表达式支持 `continue` 表达式。

r[expr.loop.explicit-result]
只有 `loop` 表达式支持求值为非平凡值（即通过 break 带值跳出）。

r[expr.loop.infinite]
## 无限循环（Infinite loops）

r[expr.loop.infinite.syntax]
```grammar,expressions
InfiniteLoopExpression -> `loop` BlockExpression
```

r[expr.loop.infinite.intro]
`loop` 表达式不断重复执行它的循环体：`loop { println!("I live."); }`。

r[expr.loop.infinite.diverging]
无限 `loop` 表达式是发散的（diverging），类型为 `!`，这属于未定义行为。可能无限循环的代码不会出现在测试点中，例如：

```rust
loop {
    if (2 > 3) {
        break;
    }
}
```

r[expr.loop.infinite.break]
含有与之关联的 `break` 表达式的 `loop` 表达式可以终止，且其类型必须与 `break` 表达式（们）的值兼容。

r[expr.loop.while]
## 条件循环（Predicate loops）

r[expr.loop.while.grammar]
```grammar,expressions
PredicateLoopExpression -> `while` Conditions BlockExpression
```

r[expr.loop.while.intro]
`while` 循环表达式允许在一组条件保持为真时重复求值一个块。

r[expr.loop.while.syntax]
`while` 表达式的语法是一个括号括起的条件表达式，后跟一个块表达式。括号内，条件操作数可以用 `&&` 分隔形成条件链。

r[expr.loop.while.condition]
条件操作数必须是布尔类型的表达式。如果所有条件操作数都求值为 `true`，则执行循环体块。

r[expr.loop.while.repeat]
循环体成功执行后，重新求值条件操作数，以决定是否再次执行循环体。

r[expr.loop.while.exit]
如果任一条件操作数求值为 `false`，则不执行循环体，执行流程继续到 `while` 表达式之后。

r[expr.loop.while.short-circuit]
`while` 循环必须支持短路求值。

r[expr.loop.while.eval]
`while` 表达式求值为 `()`。

一个例子：

```rust
let mut i = 0;

while (i < 10) {
    println!("hello");
    i = i + 1;
}
```

r[expr.loop.break]
## `break` 表达式

r[expr.loop.break.syntax]
```grammar,expressions
BreakExpression -> `break` Expression?
```

r[expr.loop.break.intro]
遇到 `break` 时，关联循环体的执行立即终止，例如：

```rust
let mut last = 0;
let mut x = 1;
while (x < 100) {
    if (x > 12) {
        break;
    }
    last = x;
    x += 1;
}
assert_eq!(last, 12);
```

r[expr.loop.break.label]
`break` 表达式通常与包住它的最内层 `loop` 或 `while` 循环关联。

r[expr.loop.break.value]
`break` 表达式只允许出现在循环体内，形式为 `break` 或（见下文）`break EXPR`。

r[expr.loop.continue]
## `continue` 表达式

r[expr.loop.continue.syntax]
```grammar,expressions
ContinueExpression -> `continue`
```

r[expr.loop.continue.intro]
遇到 `continue` 时，关联循环体的当前这轮迭代立即终止，控制流回到循环的*头部*。

r[expr.loop.continue.while]
对 `while` 循环来说，头部就是控制循环的条件操作数。

r[expr.loop.continue.label]
`continue` 与包住它的最内层循环关联。

r[expr.loop.continue.in-loop-only]
`continue` 表达式只允许出现在循环体内。

r[expr.loop.break-value]
## `break` 与循环值

r[expr.loop.break-value.intro]
当与 `loop` 关联时，break 表达式可以用来从该循环返回一个值，形式为 `break EXPR` 或 `break 'label EXPR`，其中 `EXPR` 是一个表达式，它的结果就是 `loop` 的返回值。例如：

```rust
let (mut a, mut b) = (1, 1);
let result = loop {
    if (b > 10) {
        break b;
    }
    let c = a + b;
    a = b;
    b = c;
};
// 斐波那契数列中第一个超过 10 的数：
assert_eq!(result, 13);
```

r[expr.loop.break-value.loop]
当 `loop` 含有与之关联的 `break` 时，它不被视为发散的，且 `loop` 的类型必须与每个 `break` 表达式兼容。不带表达式的 `break` 视为与带表达式 `()` 的 `break` 相同。
