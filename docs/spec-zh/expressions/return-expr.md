# `return` 表达式

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/return-expr.md`

r[expr.return.syntax]
```grammar,expressions
ReturnExpression -> `return` Expression?
```

r[expr.return.intro]
返回表达式用关键字 `return` 表示。

r[expr.return.behavior]
求值 `return` 表达式会把它的参数移动到当前函数调用的指定输出位置，销毁当前函数的激活帧（栈帧），并把控制流移交回调用者的栈帧。

`return` 表达式的例子：

```rust
fn max(a: i32, b: i32) -> i32 {
    if (a > b) {
        return a;
    }
    return b;
}
```
