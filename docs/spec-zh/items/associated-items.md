# 关联条目（Associated Items）

> 中文译文，原文：`reference/RCompiler-Spec/src/items/associated-items.md`

r[items.associated.syntax]
```grammar,items
AssociatedItem -> ConstantItem | Function 
```

r[items.associated.intro]
*关联条目*是在 trait 中声明、或在 impl 中定义的条目。之所以叫"关联"，是因为它们定义在一个关联类型上——即 impl 中的那个类型。

r[items.associated.kinds]
它们是模块中可声明条目种类的一个子集。具体包括：关联函数（包括方法）、关联类型、关联常量。

r[items.associated.related]
当某个条目与关联它的条目在逻辑上相关时，关联条目就很有用。例如，`Option` 上的 `is_some` 方法与 Option 内在相关，所以应该是关联的。

r[items.associated.decl-def]
每种关联条目都有两种形式：包含实际实现的*定义*（definitions），和为定义声明签名的*声明*（declarations）。

r[items.associated.trait-items]
正是这些"声明"构成了 trait 的契约，也是泛型类型上可用的内容。

r[items.associated.fn]
## 关联函数与方法

r[items.associated.fn.intro]
*关联函数*是与某个类型关联的函数。

r[items.associated.fn.decl]
*关联函数声明*为关联函数定义声明签名。写法与函数条目相同，只是函数体被替换为一个 `;`。

r[items.associated.name]
标识符就是函数的名字。

r[items.associated.same-signature]
关联函数的泛型、参数列表、返回类型和 where 子句必须与关联函数声明一致。

r[items.associated.fn.def]
*关联函数定义*定义一个与另一类型关联的函数。写法与函数条目相同。

> [!NOTE]
> 一个常见的例子是名为 `new` 的关联函数，它返回与其关联类型的值。

```rust
struct Struct {
    field: i32
}

impl Struct {
    fn new() -> Struct {
        Struct {
            field: 0i32
        }
    }
}

fn main () {
    let _struct = Struct::new();
}
```

r[items.associated.fn.qualified-self]
当关联函数声明在 trait 上时，也可以用指向该 trait 的路径拼接函数名来调用。此时它会被替换为 `<_ as Trait>::function_name`。

```rust
trait Num {
    fn from_i32(n: i32) -> Self;
}

impl Num for f64 {
    fn from_i32(n: i32) -> f64 { n as f64 }
}

// 这种情况下，以下 4 种写法等价。
let _: f64 = Num::from_i32(42);
let _: f64 = <_ as Num>::from_i32(42);
let _: f64 = <f64 as Num>::from_i32(42);
let _: f64 = f64::from_i32(42);
```

r[items.associated.fn.method]
### 方法（Methods）

r[items.associated.fn.method.intro]
第一个参数名为 `self` 的关联函数称为*方法*，可以用方法调用运算符调用（例如 `x.foo()`），也可以用通常的函数调用记法调用。

r[items.associated.fn.method.self-ty]
如果显式指定了 `self` 参数的类型，它仅限于解析为以下语法生成的类型（其中 `'lt` 表示任意生命周期）：

```text
P = &S | &mut S | Box<S> | Rc<S> | Arc<S> | Pin<P>
S = Self | P
```

该语法中的 `Self` 终结符表示解析为被实现类型的类型。这也可以包括上下文中的类型别名 `Self`、其他类型别名，或解析为被实现类型的关联类型投影。

```rust
# use std::rc::Rc;
# use std::sync::Arc;
# use std::pin::Pin;
// 在结构体 `Example` 上实现的方法的例子。
struct Example;
type Alias = Example;
trait Trait { type Output; }
impl Trait for Example { type Output = Example; }
impl Example {
    fn by_value(self: Self) {}
    fn by_ref(self: &Self) {}
    fn by_ref_mut(self: &mut Self) {}
    fn by_box(self: Box<Self>) {}
    fn by_rc(self: Rc<Self>) {}
    fn by_arc(self: Arc<Self>) {}
    fn by_pin(self: Pin<&Self>) {}
    fn explicit_type(self: Arc<Example>) {}
}
```

r[associated.fn.method.self-pat-shorthands]
也可以使用不指定类型的简写语法，对应关系如下：

| 简写                  | 等价写法
|----------------------|-----------
| `self`               | `self: Self`
| `&self`              | `self: &Self`
| `&mut self`          | `self: &mut Self`

> [!NOTE]
> 使用这种简写时，生命周期可以（而且通常都会）省略。

r[associated.fn.method.self-pat-mut]
如果 `self` 参数前面带 `mut`，它就变成一个可变变量，类似于普通参数使用带 `mut` 的标识符模式。例如：

```rust
trait Changer: Sized {
    fn change(mut self) {}
    fn modify(mut self: Box<Self>) {}
}
```

作为 trait 上方法的例子，考虑：

```rust
# type Surface = i32;
# type BoundingBox = i32;
trait Shape {
    fn draw(&self, surface: Surface);
    fn bounding_box(&self) -> BoundingBox;
}
```

这定义了一个有两个方法的 trait。所有在该 trait 处于作用域内时实现了它的值，都可以调用它们的 `draw` 和 `bounding_box` 方法。

```rust
# type Surface = i32;
# type BoundingBox = i32;
# trait Shape {
#     fn draw(&self, surface: Surface);
#     fn bounding_box(&self) -> BoundingBox;
# }
#
struct Circle {
    // ...
}

impl Shape for Circle {
    // ...
#   fn draw(&self, _: Surface) {}
#   fn bounding_box(&self) -> BoundingBox { 0i32 }
}

# impl Circle {
#     fn new() -> Circle { Circle{} }
# }
#
let circle_shape = Circle::new();
let bounding_box = circle_shape.bounding_box();
```

r[items.associated.const]
## 关联常量（Associated Constants）

r[items.associated.const.intro]
*关联常量*是与某个类型关联的常量。

r[items.associated.const.decl]
*关联常量声明*为关联常量定义声明签名。写法是：`const`，然后标识符，然后 `:`，然后类型，最后以 `;` 结束。

r[items.associated.const.name]
标识符是路径中使用的常量名。类型是定义必须实现的类型。

r[items.associated.const.def]
*关联常量定义*定义一个与类型关联的常量。写法与常量条目相同。

r[items.associated.const.eval]
关联常量定义只有在被引用时才进行常量求值。此外，包含泛型参数的定义在单态化（monomorphization）之后求值。

```rust,compile_fail
struct Struct;
struct GenericStruct<const ID: i32>;

impl Struct {
    // 定义不会被立即求值
    const PANIC: () = panic!("compile-time panic");
}

impl<const ID: i32> GenericStruct<ID> {
    // 定义不会被立即求值
    const NON_ZERO: () = if (ID == 0) {
        panic!("contradiction")
    };
}

fn main() {
    // 引用 Struct::PANIC 导致编译错误
    let _ = Struct::PANIC;

    // 没问题，ID 不是 0
    let _ = GenericStruct::<1>::NON_ZERO;

    // 以 ID=0 求值 NON_ZERO，编译错误
    let _ = GenericStruct::<0>::NON_ZERO;
}
```

### 关联常量示例

一个基本例子：

```rust
trait ConstantId {
    const ID: i32;
}

struct Struct;

impl ConstantId for Struct {
    const ID: i32 = 1;
}

fn main() {
    assert_eq!(1, Struct::ID);
}
```

使用默认值：

```rust
trait ConstantIdDefault {
    const ID: i32 = 1;
}

struct Struct;
struct OtherStruct;

impl ConstantIdDefault for Struct {}

impl ConstantIdDefault for OtherStruct {
    const ID: i32 = 5;
}

fn main() {
    assert_eq!(1, Struct::ID);
    assert_eq!(5, OtherStruct::ID);
}
```
