# 类型（Types）

> 中文译文，原文：`reference/RCompiler-Spec/src/types.md`

r[type.intro]
Rust 程序中的每个变量、条目和值都有一个类型。值的*类型*定义了如何解读存放它的内存，以及可以对这个值执行哪些操作。

r[type.builtin]
内建类型与语言紧密集成，其集成方式很重要，是用户自定义类型无法模拟的。

r[type.user-defined]
用户自定义类型的能力有限。

r[type.kinds]
类型列表如下：

* 原生类型（Primitive types）：
    * 布尔类型 —— `bool`
    * 数值类型 —— 整数
    * 文本类型 —— `char` 和 `str`
* 序列类型（Sequence types）：
    * 数组（Array）
    * 切片（Slice）
* 用户自定义类型：
    * 结构体（Struct）
    * 枚举（Enum）
* 指针类型：
    * 引用（References）
* 单元类型（Unit type）：
    * 单元类型 —— `()`

r[type.name]
## 类型表达式（Type expressions）

r[type.name.syntax]
```grammar,types
Type -> TypeNoBounds

TypeNoBounds ->
      TypePath
    | ReferenceType
    | ArrayType
    | UnitType
```

r[type.name.intro]
*类型表达式*由上面 `Type` 语法规则定义，是在代码中指代一个类型的语法。它可以指代：

r[type.name.sequence]
* 序列类型（数组、切片）。

r[type.name.path]
* 类型路径（Type paths），可以引用：
    * 原生类型（布尔、数值、文本）。
    * 指向某个条目的路径（结构体、枚举、联合体、类型别名、trait）。
    * `Self` 路径，其中 `Self` 是正在实现的类型。

r[type.name.pointer]
* 指针类型（引用）。

r[type.name.inference]
* 推断类型（inferred type），即请求编译器自行确定类型。

r[type.recursive]
## 递归类型（Recursive types）

r[type.recursive.intro]
具名类型 —— 结构体 —— 可以是递归的。也就是说，结构体的每个字段都可以直接或间接地引用它所属的 `struct` 类型本身。

r[type.recursive.constraint]
这种递归有以下限制：

* 递归中必须包含一个具名类型（不能是数组等其他结构式类型）。所以 `type Rec = &'static [Rec]` 是不允许的。
* 递归类型的大小必须是有限的；换句话说，类型中递归的字段必须是指针类型。

一个*递归*类型及其使用的例子：

```rust
struct List {
    data: usize,
    // 结构体可以引用自身（只要不构成无限递归）。
    next: Option<Box<Self>>,
}

impl List {
    fn new(data: usize) -> Self {
        List {
            data,
            next: None,
        }
    }

    fn append(&mut self, value: usize) {
        match &mut self.next {
            Some(next) => next.append(value),
            None => self.next = Some(Box::new(Self::new(value))),
        }
    }
}
```
