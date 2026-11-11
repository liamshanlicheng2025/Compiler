# 类型强制转换（Type coercions）

> 中文译文，原文：`reference/RCompiler-Spec/src/type-coercions.md`

r[coerce.intro]
**类型强制转换（coercion）**是改变一个值类型的隐式操作。它们只在特定的位置自动发生，而且对哪些类型之间可以转换有严格限制。

r[coerce.as]
凡是被 coercion 允许的转换，也都可以用类型转换运算符 `as` 显式完成。

Coercion 最初定义于 RFC 401，后在 RFC 1558 中扩展。

r[coerce.site]
## Coercion 发生的地点（Coercion sites）

r[coerce.site.intro]
Coercion 只能发生在程序中某些特定的 coercion site 上；这些位置通常是目标类型已经显式写出、或可以从显式类型传播推导出来（不依赖类型推断）的地方。可能的 coercion site 有：

r[coerce.site.let]
* 给出了显式类型的 `let` 语句。

   例如，下面代码中 `&mut 42` 被 coerce 为 `&i8` 类型：

   ```rust
   let _: &i8 = &mut 42;
   ```

r[coerce.site.value]
* `static` 和 `const` 条目声明（与 `let` 语句类似）。

r[coerce.site.argument]
* 函数调用的实参

  被 coerce 的值是实际参数，它被 coerce 为形式参数的类型。

  例如，下面代码中 `&mut 42` 被 coerce 为 `&i8` 类型：

  ```rust
  fn bar(_: &i8) { }

  fn main() {
      bar(&mut 42);
  }
  ```

  对方法调用，接收者（`self` 参数）类型的 coerce 方式不同，详见方法调用表达式的文档。

r[coerce.site.constructor]
* 结构体、联合体或枚举变体字段的实例化

  例如，下面代码中 `&mut 42` 被 coerce 为 `&i8` 类型：

  ```rust
  struct Foo<'a> { x: &'a i8 }

  fn main() {
      Foo { x: &mut 42 };
  }
  ```

r[coerce.site.return]
* 函数返回值——包括块的最后一行（如果没有以分号结尾），以及 `return` 语句中的任何表达式

  例如，下面代码中 `x` 被 coerce 为 `&dyn Display` 类型：

  ```rust
  use std::fmt::Display;
  fn foo(x: &u32) -> &dyn Display {
      x
  }
  ```

r[coerce.site.subexpr]
如果上述 coercion site 中的表达式是一个"coercion 传播表达式"（coercion-propagating expression），那么该表达式中的相关子表达式也是 coercion site。传播会从这些新的 coercion site 递归下去。传播表达式及其相关子表达式包括：

r[coerce.site.array]
* 数组字面量：当数组类型为 `[U; n]` 时，数组字面量中的每个子表达式都是向类型 `U` coerce 的 coercion site。

r[coerce.site.repeat]
* 重复语法的数组字面量：当数组类型为 `[U; n]` 时，被重复的子表达式是向类型 `U` coerce 的 coercion site。

r[coerce.site.tuple]
* 元组：当元组向类型 `(U_0, U_1, ..., U_n)` coerce 时，每个子表达式分别向对应类型 coerce，例如第 0 个子表达式向 `U_0` coerce。

r[coerce.site.parenthesis]
* 带括号的子表达式（`(e)`）：如果表达式类型为 `U`，则子表达式是向 `U` coerce 的 coercion site。

r[coerce.site.block]
* 块：如果块的类型为 `U`，那么块中最后一个表达式（如果没有以分号结尾）是向 `U` coerce 的 coercion site。这包括作为控制流语句一部分的块（如 `if`/`else`），只要块的类型已知。

r[coerce.types]
## 允许 coercion 的类型（Coercion types）

r[coerce.types.intro]
以下类型之间允许 coercion：

r[coerce.types.reflexive]
* `T` 到 `U`，如果 `T` 是 `U` 的子类型（*自反情形*）

r[coerce.types.transitive]
* `T_1` 到 `T_3`，其中 `T_1` 可 coerce 到 `T_2` 且 `T_2` 可 coerce 到 `T_3`（*传递情形*）

    注意：传递情形目前尚未完全支持。

r[coerce.types.mut-reborrow]
* `&mut T` 到 `&T`

r[coerce.types.mut-pointer]
* `*mut T` 到 `*const T`

r[coerce.types.ref-to-pointer]
* `&T` 到 `*const T`

r[coerce.types.mut-to-pointer]
* `&mut T` 到 `*mut T`

r[coerce.types.deref]
* `&T` 或 `&mut T` 到 `&U`，如果 `T` 实现了 `Deref<Target = U>`。例如：

  ```rust
  use std::ops::Deref;

  struct CharContainer {
      value: char,
  }

  impl Deref for CharContainer {
      type Target = char;

      fn deref<'a>(&'a self) -> &'a char {
          &self.value
      }
  }

  fn foo(arg: &char) {}

  fn main() {
      let x = &mut CharContainer { value: 'y' };
      foo(x); // &mut CharContainer 被 coerce 为 &char。
  }
  ```

r[coerce.types.deref-mut]
* `&mut T` 到 `&mut U`，如果 `T` 实现了 `DerefMut<Target = U>`。

r[coerce.types.unsize]
* TyCtor(`T`) 到 TyCtor(`U`)，其中 TyCtor(`T`) 是以下之一：
    - `&T`
    - `&mut T`
    - `*const T`
    - `*mut T`
    - `Box<T>`

    且 `U` 可以由 `T` 通过 unsized coercion（非定长强制转换，见下文）得到。

r[coerce.types.fn]
* 函数条目类型（function item types）到 `fn` 指针

r[coerce.types.closure]
* 不捕获环境的闭包到 `fn` 指针

r[coerce.types.never]
* `!`（never 类型）到任何 `T`

r[coerce.unsize]
### 非定长强制转换（Unsized Coercions）

r[coerce.unsize.intro]
以下 coercion 称为 `unsized coercions`，因为它们涉及把类型转换为非定长类型；在上面提到的少数情形中，它们被允许而其他 coercion 不被允许。它们同样可以发生在任何允许 coercion 的地方。

r[coerce.unsize.trait]
有两个 trait——`Unsize` 和 `CoerceUnsized`——用于辅助这一过程并把它暴露给库使用。以下 coercion 是内建的：如果 `T` 能通过其中一种 coerce 到 `U`，那么编译器会自动为 `T` 提供 `Unsize<U>` 的实现：

r[coerce.unsize.slice]
* `[T; n]` 到 `[T]`。

r[coerce.unsize.trait-object]
* `T` 到 `dyn U`，当 `T` 实现了 `U + Sized` 且 `U` 是 dyn 兼容的。

r[coerce.unsize.trait-upcast]
* `dyn T` 到 `dyn U`，当 `U` 是 `T` 的父 trait 之一。
    * 这允许丢弃 auto trait，例如允许 `dyn T + Auto` 到 `dyn U`。
    * 如果主 trait 以某 auto trait 为父 trait，也允许添加该 auto trait，例如给定 `trait T: U + Send {}`，允许 `dyn T` 到 `dyn T + Send` 或到 `dyn U + Send` 的 coercion。

r[coerce.unsized.composite]
* `Foo<..., T, ...>` 到 `Foo<..., U, ...>`，当：
    * `Foo` 是一个结构体；
    * `T` 实现了 `Unsize<U>`；
    * `Foo` 的最后一个字段的类型涉及 `T`；
    * 如果该字段类型为 `Bar<T>`，那么 `Bar<T>` 实现了 `Unsize<Bar<U>>`；
    * `T` 不出现在任何其他字段的类型中。

r[coerce.unsized.pointer]
此外，当 `T` 实现了 `Unsize<U>` 或 `CoerceUnsized<Foo<U>>` 时，类型 `Foo<T>` 可以实现 `CoerceUnsized<Foo<U>>`，从而提供到 `Foo<U>` 的 unsized coercion。

> [!NOTE]
> 虽然 unsized coercion 的定义和实现已经稳定，但这些 trait 本身尚未稳定，因此不能在稳定版 Rust 中直接使用。

r[coerce.least-upper-bound]
## 最小上界强制转换（Least upper bound coercions，LUB）

r[coerce.least-upper-bound.intro]
在某些上下文中，编译器必须把多个类型合并 coerce 到一起，试图找到最一般的类型。这称为"最小上界"（Least Upper Bound，LUB）coercion。LUB coercion 用于且仅用于以下情形：

+ 为一串 if 分支找公共类型。
+ 为一串 match 分支找公共类型。
+ 为数组元素找公共类型。
+ 为有多个 return 语句的闭包确定返回类型。
+ 检查有多个 return 语句的函数的返回类型。

r[coerce.least-upper-bound.target]
在这类情形中，有一组类型 `T0..Tn` 需要互相 coerce 到某个目标类型 `T_t`，而 `T_t` 一开始是未知的。

r[coerce.least-upper-bound.computation]
LUB coercion 的计算是迭代进行的。目标类型 `T_t` 初始为类型 `T0`。对每个新类型 `Ti`，考虑：

r[coerce.least-upper-bound.computation-identity]
+ 如果 `Ti` 能 coerce 到当前目标类型 `T_t`，则不做任何改变。

r[coerce.least-upper-bound.computation-replace]
+ 否则，检查 `T_t` 是否能 coerce 到 `Ti`；如果可以，则把 `T_t` 改为 `Ti`。（这个检查还取决于目前为止考虑过的所有源表达式是否都允许隐式 coercion。）

r[coerce.least-upper-bound.computation-unify]
+ 如果也不行，尝试计算 `T_t` 和 `Ti` 的共同父类型，作为新的目标类型。

### 例子：

```rust
# let (a, b, c) = (0, 1, 2);
// if 分支
let bar = if (true) {
    a
} else if (false) {
    b
} else {
    c
};

// match 分支
let baw = match 42 {
    0 => a,
    1 => b,
    _ => c,
};

// 数组元素
let bax = [a, b, c];

// 有多个 return 的闭包
let clo = || {
    if (true) {
        a
    } else if (false) {
        b
    } else {
        c
    }
};
let baz = clo();

// 有多个 return 的函数的类型检查
fn foo() -> i32 {
    let (a, b, c) = (0, 1, 2);
    match 42 {
        0 => a,
        1 => b,
        _ => c,
    }
}
```

在这些例子中，`ba*` 的类型都是通过 LUB coercion 求出的。编译器在处理函数 `foo` 时，会检查 `a`、`b`、`c` 的 LUB coercion 结果是否为 `i32`。

### 注意

以上描述显然是非形式化的。让它更精确，将是"更精确地规范 Rust 类型检查器"这一总体工作的一部分。
