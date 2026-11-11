# 路径表达式（Path expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/path-expr.md`

r[expr.path.syntax]
```grammar,expressions
PathExpression ->
      PathInExpression
```

r[expr.path.intro]
用在表达式上下文中的路径，表示的要么是一个局部变量，要么是一个条目（item）。

r[expr.path.place]
解析到局部变量或静态变量的路径表达式是位置表达式（place expressions），其他路径是值表达式（value expressions）。

```rust
# mod globals {
#     pub static STATIC_VAR: i32 = 5;
#     pub static mut STATIC_MUT_VAR: i32 = 7;
# }
let local_var = 3;
local_var;
```

r[expr.path.const]
关联常量（associated constants）的求值方式与 `const` 块相同。
