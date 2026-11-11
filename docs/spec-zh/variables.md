# 变量（Variables）

> 中文译文，原文：`reference/RCompiler-Spec/src/variables.md`

r[variable.intro]
*变量*是栈帧的组成部分，可以是：具名的函数参数、匿名的临时值，或具名的局部变量。

r[variable.local]
*局部变量*（或*栈局部*分配）直接持有一个值，分配在栈内存中。这个值是栈帧的一部分。

r[variable.local-mut]
局部变量默认是不可变的，除非显式声明为可变。例如：`let mut x = ...`。

r[variable.param-mut]
函数参数默认是不可变的，除非用 `mut` 声明。`mut` 关键字只作用于紧随其后的那个参数。例如：`|mut x, y|` 和 `fn f(mut x: Box<i32>, y: Box<i32>)` 声明了一个可变变量 `x` 和一个不可变变量 `y`。

r[variable.init]
局部变量在分配时不会被初始化。实际上，进入栈帧时会一次性分配整个栈帧的局部变量，全部处于未初始化状态。函数内的后续语句可能初始化、也可能不初始化这些局部变量。局部变量只有在所有可达的控制流路径上都被初始化之后，才能使用。

在下面的例子中，`init_after_if` 在 `if` 表达式之后是已初始化的，而 `uninit_after_if` 不是——因为它在 `else` 分支中没有被初始化：

```rust
# fn random_bool() -> bool { true }
fn initialization_example() {
    let init_after_if: ();
    let uninit_after_if: ();

    if (random_bool()) {
        init_after_if = ();
        uninit_after_if = ();
    } else {
        init_after_if = ();
    }

    init_after_if; // 正确
    // uninit_after_if; // 错误：使用了可能未初始化的 `uninit_after_if`
}
```
