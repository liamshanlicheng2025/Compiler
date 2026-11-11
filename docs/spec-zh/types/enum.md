# 枚举类型（Enumerated types）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/enum.md`

r[type.enum.intro]
*枚举类型*是一种具名的、异构的不相交并集类型（disjoint union），用 `enum` 条目的名字来指代。[^enumtype]

r[type.enum.declaration]
一个 `enum` 条目同时声明了类型本身和若干*变体*（variant），每个变体都有独立的名字，语法上可以是结构体、元组结构体或类单元结构体的形式。

r[type.enum.constructor]
`enum` 的新实例可以用结构体表达式构造。

r[type.enum.value]
任何 `enum` 值占用的内存等于其所属 `enum` 类型中最大变体的大小，再加上存储判别值（discriminant）所需的大小。

r[type.enum.name]
枚举类型不能以*结构式*的写法表示，必须通过对 `enum` 条目的具名引用来表示。例如：

```rust
fn g(e: enum { A, B }) { }
```

这种写法不受支持，应该写成：

```rust
enum E { A, B }
fn g(e: E) { }
```

r[type.enum.comparison]
同一个 `enum` 的变体之间可以用 `==` 比较相等、用 `!=` 比较不等。

[^enumtype]: `enum` 类型类似于 Haskell 中的 `data` 构造器声明，或 Limbo 语言中的 *pick ADT*。
