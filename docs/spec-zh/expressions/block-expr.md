# 块表达式（Block expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/block-expr.md`

r[expr.block.syntax]
```grammar,expressions
BlockExpression ->
    `{`
        Statements?
    `}`

Statements ->
      Statement+
    | Statement+ ExpressionWithoutBlock
    | ExpressionWithoutBlock
```

r[expr.block.intro]
*块表达式*（block expression，简称*块*）既是一个控制流表达式，也是条目和变量声明的匿名命名空间作用域。

r[expr.block.sequential-evaluation]
作为控制流表达式，块依次执行它的各条非条目声明语句，然后执行最后那个可选的表达式。

r[expr.block.namespace]
作为匿名命名空间作用域，条目声明只在块内部处于作用域内；`let` 语句声明的变量从下一条语句开始直到块结束处于作用域内。详见作用域（scopes）一章。

r[expr.block.inner-attributes]
块的语法是：一个 `{`，然后任意数量的语句，然后一个可选的表达式（称为最终操作数，final operand），最后一个 `}`。

r[expr.block.statements]
语句通常必须跟分号，有两个例外：

1. 条目声明语句后面不需要分号。
2. 表达式语句通常需要分号，除非它的外层表达式是控制流表达式。

r[expr.block.null-statement]
此外，语句之间允许出现多余的分号，但这些分号不影响语义。

r[expr.block.evaluation]
求值一个块表达式时，除条目声明语句外，每条语句按顺序执行。

r[expr.block.result]
然后执行最终操作数（如果给出了的话）。

r[expr.block.type]
块的类型就是最终操作数的类型；如果省略了最终操作数，则为 `()`。

```rust
# fn fn_call() {}
let _: () = {
    fn_call();
};

let five: i32 = {
    fn_call();
    5
};

assert_eq!(5, five);
```

> [!NOTE]
> 作为控制流表达式，如果块表达式是表达式语句的外层表达式，那么它的期望类型是 `()`，除非它紧跟一个分号。

r[expr.block.value]
块总是值表达式（value expressions），并且其最后一个操作数在值表达式上下文中求值。

> [!NOTE]
> 必要时可以利用这一点强制移动一个值。例如，下面的例子会在调用 `consume_self` 时报错，因为结构体在块表达式中已经被移出 `s`：
>
> ```rust,compile_fail
> struct Struct;
>
> impl Struct {
>     fn consume_self(self) {}
>     fn borrow_self(&self) {}
> }
>
> fn move_by_block_expression() {
>     let s = Struct;
>
>     // 在块表达式中把值移出 `s`。
>     (&{ s }).borrow_self();
>
>     // 执行失败，因为 `s` 已经被移出。
>     s.consume_self();
> }
> ```
