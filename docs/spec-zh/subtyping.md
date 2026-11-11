# 子类型与型变（Subtyping and Variance）

> 中文译文，原文：`reference/RCompiler-Spec/src/subtyping.md`

r[subtype.intro]
子类型关系是隐式的，可以出现在类型检查或类型推断的任何阶段。

r[subtype.kinds]
子类型只限于两种情形：关于生命周期的型变（variance），以及高阶生命周期（higher-ranked lifetimes）类型之间的子类型关系。如果把生命周期从类型中抹去，那么剩下的子类型关系就只有类型相等。

考虑下面的例子：字符串字面量总是具有 `'static` 生命周期。尽管如此，我们可以把 `s` 赋给 `t`：

```rust
fn bar<'a>() {
    let s: &'static str = "hi";
    let t: &'a str = s;
}
```

因为 `'static` 比生命周期参数 `'a` 活得更久（outlive），所以 `&'static str` 是 `&'a str` 的子类型。

r[subtype.higher-ranked]
高阶函数指针和 trait 对象之间存在另一种子类型关系：它们是把高阶生命周期做替换后得到的类型的子类型。一些例子：

```rust
// 这里把 'a 替换成了 'static
let subtype: &(for<'a> fn(&'a i32) -> &'a i32) = &((|x| x) as fn(&_) -> &_);
let supertype: &(fn(&'static i32) -> &'static i32) = subtype;

// trait 对象也是类似的
let subtype: &(dyn for<'a> Fn(&'a i32) -> &'a i32) = &|x| x;
let supertype: &(dyn Fn(&'static i32) -> &'static i32) = subtype;

// 也可以把一个高阶生命周期替换成另一个
let subtype: &(for<'a, 'b> fn(&'a i32, &'b i32)) = &((|x, y| {}) as fn(&_, &_));
let supertype: &for<'c> fn(&'c i32, &'c i32) = subtype;
```

r[subtyping.variance]
## 型变（Variance）

r[subtyping.variance.intro]
型变是泛型类型相对于其类型参数的一种性质。一个泛型类型在某个参数上的*型变*，描述的是该参数的子类型关系如何影响整个类型的子类型关系。

r[subtyping.variance.covariant]
* 如果 `T` 是 `U` 的子类型意味着 `F<T>` 是 `F<U>` 的子类型（子类型关系"穿透"过去），则 `F<T>` 对 `T` 是*协变的*（covariant）。

r[subtyping.variance.contravariant]
* 如果 `T` 是 `U` 的子类型意味着 `F<U>` 是 `F<T>` 的子类型，则 `F<T>` 对 `T` 是*逆变的*（contravariant）。

r[subtyping.variance.invariant]
* 其他情况下 `F<T>` 对 `T` 是*不变的*（invariant）（无法推出任何子类型关系）。

r[subtyping.variance.builtin-types]
内建类型的型变自动确定如下：

| 类型                          | 对 `'a` 的型变  | 对 `T` 的型变   |
|-------------------------------|----------------|----------------|
| `&'a T`                       | 协变           | 协变           |
| `&'a mut T`                   | 协变           | 不变           |
| `*const T`                    |                | 协变           |
| `*mut T`                      |                | 不变           |
| `[T]` 和 `[T; n]`             |                | 协变           |
| `fn() -> T`                   |                | 协变           |
| `fn(T) -> ()`                 |                | 逆变           |
| `std::cell::UnsafeCell<T>`    |                | 不变           |
| `std::marker::PhantomData<T>` |                | 协变           |
| `dyn Trait<T> + 'a`           | 协变           | 不变           |

r[subtyping.variance.user-composite-types]
其他 `struct`、`enum`、`union` 类型的型变，由它们字段类型的型变决定。如果某个参数被用在型变不同的位置上，那么该参数就是不变的。例如，下面这个结构体对 `'a` 和 `T` 协变，对 `'b`、`'c` 和 `U` 不变。

```rust
use std::cell::UnsafeCell;
struct Variance<'a, 'b, 'c, T, U: 'a> {
    x: &'a U,               // 这使 Variance 对 'a 协变，本来也会使它对 U
                            // 协变，但 U 后面还有别的用法
    y: *const T,            // 对 T 协变
    z: UnsafeCell<&'b f64>, // 对 'b 不变
    w: *mut U,              // 对 U 不变，使整个结构体对 U 不变

    f: fn(&'c ()) -> &'c () // 既协变又逆变，使 'c 在整个结构体中不变
}
```

r[subtyping.variance.builtin-composite-types]
当参数用在 `struct`、`enum`、`union` 之外时，每个位置上的型变是分别检查的。

```rust
# use std::cell::UnsafeCell;
fn generic_tuple<'short, 'long: 'short>(
    // 'long 在元组内部同时出现在协变和不变的位置上。
    x: (&'long u32, UnsafeCell<&'long u32>),
) {
    // 因为这些位置上的型变是分别计算的，
    // 我们可以在协变位置上自由地把 'long 缩小。
    let _: (&'short u32, UnsafeCell<&'long u32>) = x;
}

fn takes_fn_ptr<'short, 'middle: 'short>(
    // 'middle 同时出现在协变和逆变的位置上。
    f: fn(&'middle ()) -> &'middle (),
) {
    // 因为这些位置上的型变是分别计算的，
    // 我们可以在协变位置上自由地把 'middle 缩小、
    // 在逆变位置上把它放大。
    let _: fn(&'static ()) -> &'short () = f;
}
```
