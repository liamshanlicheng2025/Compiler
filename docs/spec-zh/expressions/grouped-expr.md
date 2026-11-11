# 括号表达式（Grouped expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/grouped-expr.md`

r[expr.paren.syntax]
```grammar,expressions
GroupedExpression -> `(` Expression `)`
```

r[expr.paren.intro]
*括号表达式*包裹单个表达式，求值结果就是该表达式。括号表达式的语法是：一个 `(`，然后一个表达式（称为*被包裹的操作数*），然后一个 `)`。

r[expr.paren.evaluation]
括号表达式求值的结果是被包裹操作数的值。

r[expr.paren.place-or-value]
与其他表达式不同，括号表达式既是位置表达式也是值表达式：当被包裹的操作数是位置表达式时，它就是位置表达式；当被包裹的操作数是值表达式时，它就是值表达式。

r[expr.paren.override-precedence]
括号可以用来显式修改表达式内部子表达式的优先级顺序。

括号表达式的例子：

```rust
let x: i32 = 2 + 3 * 4; // 没加括号
let y: i32 = (2 + 3) * 4; // 加了括号
assert_eq!(x, 14);
assert_eq!(y, 20);
```

一个必须使用括号的例子：调用结构体成员中的函数指针时：

```rust
# struct A {
#    f: fn() -> &'static str
# }
# impl A {
#    fn f(&self) -> &'static str {
#        "The method f"
#    }
# }
# let a = A{f: || "The field f"};
#
assert_eq!( a.f (), "The method f");
assert_eq!((a.f)(), "The field f");
```
