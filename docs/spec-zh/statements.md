# 语句（Statements）

> 中文译文，原文：`reference/RCompiler-Spec/src/statements.md`

r[statement.syntax]
```grammar,statements
Statement ->
      `;`
    | Item
    | LetStatement
    | ExpressionStatement
```

r[statement.intro]
*语句*是块的组成部分，而块又是外层表达式或函数的组成部分。

r[statement.kind]
Rust 有两种语句：声明语句（declaration statements）和表达式语句（expression statements）。

r[statement.decl]
## 声明语句

*声明语句*会把一个或多个*名字*引入包住它的语句块中。被声明的名字可以表示新的变量或新的条目（item）。

声明语句有两种：条目声明和 `let` 语句。

r[statement.item]
### 条目声明（Item declarations）

r[statement.item.intro]
*条目声明语句*在语法形式上与条目声明完全相同。

r[statement.item.scope]
在语句块内声明条目，会把它的作用域限制在包含该语句的块内。该条目不会被赋予规范路径（canonical path），它可能声明的子条目也不会。

r[statement.item.associated-scope]
例外情况是：由 impl 定义的关联条目（associated items），只要该条目本身以及（如果适用的）trait 可访问，在外层作用域中仍然可以访问。除此之外，其含义与在模块内声明该条目完全相同。

r[statement.item.outer-generics]
不会隐式捕获外层函数的泛型参数、参数和局部变量。例如，`inner` 不能访问 `outer_var`。

```rust
fn outer() {
  let outer_var = true;

  fn inner() { /* outer_var 在这里不在作用域内 */ }

  inner();
}
```

r[statement.let]
### `let` 语句

r[statement.let.syntax]
```grammar,statements
LetStatement ->
    `let` PatternNoTopAlt `:` Type `=` Expression `;`
```

r[statement.let.intro]
*`let` 语句*引入一个由模式（pattern）给出的新变量。

r[statement.let.inference]
模式的类型必须通过类型标注显式指定。

r[statement.let.scope]
变量声明引入的任何变量，从声明点开始直到包住它的块作用域结束都可见，除非被另一个变量声明遮蔽（shadow）。

r[statement.let.initializer]
为简化起见，初始化表达式必须总是存在。

r[statement.expr]
## 表达式语句

r[statement.expr.syntax]
```grammar,statements
ExpressionStatement ->
      ExpressionWithoutBlock `;`
    | ExpressionWithBlock `;`?
```

r[statement.expr.intro]
*表达式语句*是求值一个表达式并忽略其结果的语句。一般来说，表达式语句的目的是触发其表达式求值产生的副作用。

r[statement.expr.restriction-semicolon]
只由块表达式或控制流表达式构成的表达式，如果用在允许语句出现的上下文中，可以省略末尾的分号。这可能导致它被解析为独立语句还是被解析为另一个表达式的一部分之间的歧义；在这种情况下，它被解析为语句。

r[statement.expr.constraint-block]
用作语句的 ExpressionWithBlock 表达式，其类型必须是单元类型 `()`。

```rust
# let mut v = vec![1, 2, 3];
v.pop();          // 忽略 pop 返回的元素
if (v.is_empty()) {
    v.push(5);
} else {
    v.remove(0);
}                 // 分号可以省略。
[1];              // 独立的表达式语句，不是下标索引表达式。
```

当省略末尾分号时，表达式的结果类型必须是 `()`。

```rust
// 错误：块的类型是 i32，不是 ()
// Error: expected `()` because of default return type
// if true {
//   1
// }

// 正确：块的类型是 i32
if true {
  1
} else {
  2
};
```
