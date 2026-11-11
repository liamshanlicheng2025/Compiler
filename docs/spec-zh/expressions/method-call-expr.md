# 方法调用表达式（Method-call expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/method-call-expr.md`

r[expr.method.syntax]
```grammar,expressions
MethodCallExpression -> Expression `.` PathExprSegment `(`CallParams? `)`
```

r[expr.method.intro]
*方法调用*由一个表达式（*接收者*，receiver）、一个点号、一个表达式路径段，和一个括号括起的表达式列表组成。

r[expr.method.target]
方法调用会解析到特定 trait 上的关联方法：如果左边表达式的确切 `self` 类型已知，就静态分发到对应方法；如果左边表达式是间接的 trait 对象，就动态分发。

```rust
let pi: Result<f32, _> = "3.14".parse();
let log_pi = pi.unwrap_or(1.0).log(2.72);
# assert!(1.14 < log_pi && log_pi < 1.15)
```

r[expr.method.autoref-deref]
查找方法调用时，接收者可能会被自动解引用或自动借用，以便调用某个方法。这要求一个比其他函数更复杂的查找过程，因为可能有好几个候选方法可以调用。具体过程如下：

r[expr.method.candidate-receivers]
第一步是构建候选接收者类型列表。构建方法：对接收者表达式的类型反复解引用，把遇到的每个类型加入列表；最后尝试做一次 unsized coercion，如果成功则把结果类型也加入列表。

r[expr.method.candidate-receivers-refs]
然后，对每个候选类型 `T`，把 `&T` 和 `&mut T` 紧接在 `T` 之后加入列表。

例如，如果接收者的类型是 `Box<[i32;2]>`，那么候选类型依次是：`Box<[i32;2]>`、`&Box<[i32;2]>`、`&mut Box<[i32;2]>`、`[i32; 2]`（由解引用得到）、`&[i32; 2]`、`&mut [i32; 2]`、`[i32]`（由 unsized coercion 得到）、`&[i32]`，最后是 `&mut [i32]`。

r[expr.method.candidate-search]
然后，对每个候选类型 `T`，在以下位置查找接收者为该类型的可见方法：

1. `T` 的固有方法（inherent methods，直接实现在 `T` 上的方法）。
1. `T` 实现的任何可见 trait 提供的方法。
   如果 `T` 是类型参数，先查找 `T` 的 trait 约束提供的方法，然后查找作用域内所有其余方法。

> [!NOTE]
> 查找是按类型顺序逐个进行的，这偶尔会导致出人意料的结果。下面的代码会打印 "In trait impl!"：因为先查找 `&self` 方法，所以 trait 方法先于结构体的 `&mut self` 方法被找到。
>
> ```rust
> struct Foo {}
>
> trait Bar {
>   fn bar(&self);
> }
>
> impl Foo {
>   fn bar(&mut self) {
>     println!("In struct impl!")
>   }
> }
>
> impl Bar for Foo {
>   fn bar(&self) {
>     println!("In trait impl!")
>   }
> }
>
> fn main() {
>   let mut f = Foo{};
>   f.bar();
> }
> ```

r[expr.method.ambiguous-target]
如果这样得到多个可能的候选方法，则是错误；必须把接收者转换为合适的接收者类型（使用消歧调用语法）才能调用该方法。

r[expr.method.receiver-constraints]
这个过程不考虑接收者的可变性或生命周期，也不考虑方法是否是 `unsafe` 的。一旦查找到方法，如果因为上述一个（或多个）原因无法调用，结果就是编译错误。

r[expr.method.ambiguous-search]
如果走到某一步时出现多个可能的方法（例如泛型方法或 trait 被认为相同），则是编译错误。这些情况需要使用函数调用消歧语法来调用方法和函数。

r[expr.method.edition2021]
> [!EDITION-2021]
> 在 2021 版（edition）之前，查找可见方法时，如果候选接收者类型是数组类型，标准库 `IntoIterator` trait 提供的方法会被忽略。
>
> 用于此目的的 edition 由表示方法名的那个 token 决定。
>
> 这个特例将来可能会移除。

> [!WARNING]
> 对 trait 对象，如果存在与 trait 方法同名的固有方法，在方法调用表达式中调用该方法会产生编译错误。你可以改用函数调用消歧语法来调用——但那样调用的是 trait 方法，而不是固有方法。没有办法调用那个固有方法。只要不在 trait 对象上定义与 trait 方法同名的固有方法，就不会有问题。
