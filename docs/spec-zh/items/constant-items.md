# 常量条目（Constant items）

> 中文译文，原文：`reference/RCompiler-Spec/src/items/constant-items.md`

r[items.const.syntax]
```grammar,items
ConstantItem ->
    `const` IDENTIFIER `:` Type ( `=` Expression )? `;`
```

r[items.const.intro]
*常量条目*是一个可以命名也可以不命名的常量值，它不与程序中特定的内存位置关联。

r[items.const.behavior]
常量本质上在使用处被内联，也就是说使用时会被直接复制到相关上下文中。这包括来自外部 crate 的常量，以及非 `Copy` 类型的常量。对同一个常量的多个引用，不保证指向相同的内存地址。

r[items.const.namespace]
常量声明把常量值定义在其所在模块或块的值命名空间中。

r[items.const.static]
常量必须显式标注类型。

```rust
const BIT1: u32 = 1 << 0;
const BIT2: u32 = 1 << 1;

const BITS: [u32; 2] = [BIT1, BIT2];
const STRING: &str = "bitstring";
```

r[items.const.no-mut-refs]
`const` 条目的最终值不能包含任何可变引用 `&mut`。

r[items.const.expr-omission]
定义常量的表达式只能在 trait 定义中省略。

r[items.const.eval]
## 求值

独立（free）常量总是在编译期求值，以便让 panic 暴露出来。细节见常量求值（const_eval）一章。
