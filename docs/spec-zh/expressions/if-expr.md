# `if` 表达式

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/if-expr.md`

r[expr.if.syntax]
```grammar,expressions
IfExpression ->
    `if` Conditions BlockExpression
    (`else` ( BlockExpression | IfExpression ) )?

Conditions -> `(` Expression _except [StructExpression]_ `)`
```

r[expr.if.intro]
`if` 表达式的语法是：一个或多个由 `&&` 分隔的条件操作数，后跟一个 consequent 块（条件成立时执行的块），然后任意数量的 `else if` 条件和块，以及一个可选的末尾 `else` 块。

r[expr.if.condition]
条件操作数必须是布尔类型的表达式。

r[expr.if.condition-true]
如果所有条件操作数都求值为 `true`，执行 consequent 块，并跳过后续的任何 `else if` 或 `else` 块。

r[expr.if.else-if]
如果任一条件操作数求值为 `false`，跳过 consequent 块，转而求值后续的任何 `else if` 条件。

r[expr.if.short-circuit]
`if` 表达式必须支持短路求值（short-circuit evaluation）。

```rust
fn main(){
    let mut x: i32 = 6;
    if (true || {
        x += 1;
        true
    }) {
        printlnInt(x); // 6
    }
}
```

r[expr.if.else]
如果所有 `if` 和 `else if` 条件都求值为 `false`，则执行 `else` 块（如果有）。

r[expr.if.result]
`if` 表达式求值的结果就是被执行的那个块的值；如果没有执行任何块，则为 `()`。

r[expr.if.type]
`if` 表达式在所有情况下必须具有相同的类型。

```rust
# let x = 3;
if (x == 4) {
    println!("x is four");
} else if (x == 3) {
    println!("x is three");
} else {
    println!("x is something else");
}

// `if` 可以用作表达式。
let y = if (12 * 15 > 150) {
    "Bigger"
} else {
    "Smaller"
};
assert_eq!(y, "Bigger");
```
