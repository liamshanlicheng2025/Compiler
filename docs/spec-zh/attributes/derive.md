# Derive

> 中文译文，原文：`reference/RCompiler-Spec/src/attributes/derive.md`

r[attributes.derive.intro]
*`derive` 属性*允许为数据结构自动生成新的条目。

r[attributes.derive.syntax]
它使用 MetaListPaths 语法，指定一个要实现的 trait 的列表，或要处理的 derive 宏的路径列表。

例如，下面的代码会为 `Foo` 生成 `PartialEq` 和 `Clone` 这两个 trait 的 `impl` 条目，并且类型参数 `T` 会在相应的 `impl` 中被加上 `PartialEq` 或 `Clone` 约束：

```rust
#[derive(PartialEq, Clone)]
struct Foo<T> {
    a: i32,
    b: T,
}
```

为 `PartialEq` 生成的 `impl` 等价于：

```rust
# struct Foo<T> { a: i32, b: T }
impl<T: PartialEq> PartialEq for Foo<T> {
    fn eq(&self, other: &Foo<T>) -> bool {
        self.a == other.a && self.b == other.b
    }
}
```

r[attributes.derive.proc-macro]
你可以通过过程宏（procedural macros）为你自己的 trait 实现 `derive`。

r[attributes.derive.automatically_derived]
## `automatically_derived` 属性

*`automatically_derived` 属性*会被自动添加到由 `derive` 属性为内建 trait 创建的 impl 上。它本身没有直接效果，但工具和诊断 lint 可以用它来识别这些自动生成的实现。
