# 命名空间（Namespaces）

> 中文译文，原文：`reference/RCompiler-Spec/src/names/namespaces.md`

r[names.namespaces.intro]
*命名空间*是对已声明名字的逻辑分组。名字按其所指实体的种类被隔离进不同的命名空间。命名空间使得一个名字在某个命名空间中的出现不会与另一个命名空间中的同名名字冲突。

存在若干个不同的命名空间，各自包含不同种类的实体。使用一个名字时，会根据上下文在不同的命名空间中查找它的声明，详见名字解析（name resolution）一章。

r[names.namespaces.kinds]
命名空间及其对应实体列表如下：

* 类型命名空间（Type Namespace）
    * 结构体、枚举、枚举变体的声明
    * Trait 条目声明
    * 内建类型：布尔、数值、文本
    * `Self` 类型
* 值命名空间（Value Namespace）
    * 函数声明
    * 常量条目声明
    * 结构体构造器
    * 枚举变体构造器
    * `Self` 构造器
    * 关联常量声明
    * 关联函数声明
    * 局部绑定 —— `let`、`if let`、`while let`、`for`、`match` 分支、函数参数

下面是一个例子，展示不同命名空间中重名的名字如何无歧义地使用：

```rust
// 本规范只支持命名字段的结构体，不支持元组结构体。
struct Foo { x: u32 }

// `Foo` 宏声明在宏命名空间中。
macro_rules! Foo {
    () => {};
}

// 参数 `f` 类型中的 `Foo` 指类型命名空间中的 `Foo`。
// `'Foo` 在生命周期命名空间中引入一个新生命周期。
fn example<'Foo>(f: Foo) {
    // `Foo` 指值命名空间中的 `Foo` 构造器。
    // `Foo` 指宏命名空间中的 `Foo` 宏。
    Foo!{}
    // `'Foo` 在标签命名空间中引入一个标签。
    'Foo: loop {
        // `'Foo` 指 `'Foo` 生命周期参数，`Foo`
        // 指类型命名空间。
        let x: &'Foo Foo;
        // `'Foo` 指标签。
        break 'Foo;
    }
}
```

r[names.namespaces.without]
## 不属于任何命名空间的具名实体

以下实体有显式的名字，但这些名字不属于任何特定的命名空间。

### 字段（Fields）

r[names.namespaces.without.fields]
尽管结构体、枚举和联合体的字段是有名字的，但这些字段名不处于任何显式的命名空间中。它们只能通过字段表达式访问，而字段表达式只会查看被访问的那个具体类型的字段名。
