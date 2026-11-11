# 实现（Implementations，impl）

> 中文译文，原文：`reference/RCompiler-Spec/src/items/implementations.md`

r[items.impl.syntax]
```grammar,items
Implementation -> InherentImpl | TraitImpl

InherentImpl ->
    `impl` Type `{`
        AssociatedItem*
    `}`

TraitImpl ->
    `impl` IDENTIFIER `for` Type
    `{`
        AssociatedItem*
    `}`
```

r[items.impl.intro]
*实现*（implementation）是把一些条目与一个*被实现类型*（implementing type）关联起来的条目。实现用关键字 `impl` 定义，包含的函数属于被实现类型的实例，或静态地属于该类型。

r[items.impl.kinds]
实现有两种：

- 固有实现（inherent implementations）
- trait 实现（trait implementations）

r[items.impl.trait.foreign-types]
被实现类型是在类型命名空间中定义的、对 `impl` 条目可见的结构体或枚举。

r[items.impl.inherent]
## 固有实现（Inherent Implementations）

r[items.impl.inherent.implementing-type]
这个具名类型称为*被实现类型*，那些可关联的条目称为该被实现类型的*关联条目*。

r[items.impl.inherent.associated-items]
固有实现把它包含的条目关联到被实现类型上。

r[items.impl.inherent.associated-items.allowed-items]
固有实现可以包含关联函数（包括方法）和关联常量。

r[items.impl.inherent.type-alias]
它们不能包含关联类型别名。

r[items.impl.inherent.coherence]
一个类型也可以有多个固有实现。被实现类型必须与原始类型定义在同一个 crate 中。

``` rust
pub mod color {
    pub struct Color(pub u8, pub u8, pub u8);

    impl Color {
        pub const WHITE: Color = Color(255, 255, 255);
    }
}

mod values {
    use super::color::Color;
    impl Color {
        pub fn red() -> Color {
            Color(255, 0, 0)
        }
    }
}

pub use self::color::Color;
fn main() {
    // 指向被实现类型和 impl 的实际路径在同一模块中。
    color::Color::WHITE;

    // 不同模块中的 impl 块仍通过指向类型的路径访问。
    color::Color::red();

    // 被重新导出的指向被实现类型的路径也可以。
    Color::red();

    // 不行，因为 `values` 中的 use 不是 pub 的。
    // values::Color::red();
}
```

r[items.impl.trait]
## Trait 实现（Trait Implementations）

r[items.impl.trait.implemented-trait]
这个 trait 称为*被实现的 trait*（implemented trait）。被实现类型实现这个被实现的 trait。

r[items.impl.trait.def-requirement]
一个 trait 实现必须定义被实现 trait 声明的所有非默认关联条目，可以重新定义被实现 trait 定义的默认关联条目，且不能定义任何其他条目。

r[items.impl.trait.associated-item-path]
关联条目的路径是：`<` 加上指向被实现类型的路径，加上 `as`，加上指向 trait 的路径，加上 `>` 作为一个路径成分，后面再接关联条目自己的路径成分。

```rust
# #[derive(Copy, Clone)]
# struct Point {x: f64, y: f64};
# type Surface = i32;
# struct BoundingBox {x: f64, y: f64, width: f64, height: f64};
# trait Shape { fn draw(&self, s: Surface); fn bounding_box(&self) -> BoundingBox; }
# fn do_draw_circle(s: Surface, c: Circle) { }
struct Circle {
    radius: f64,
    center: Point,
}

impl Copy for Circle {}

impl Clone for Circle {
    fn clone(&self) -> Circle { *self }
}

impl Shape for Circle {
    fn draw(&self, s: Surface) { do_draw_circle(s, *self); }
    fn bounding_box(&self) -> BoundingBox {
        let r = self.radius;
        BoundingBox {
            x: self.center.x - r,
            y: self.center.y - r,
            width: 2.0 * r,
            height: 2.0 * r,
        }
    }
}
```

r[items.impl.trait.coherence]
### Trait 实现的一致性（Coherence）

r[items.impl.trait.coherence.intro]
一个 trait 实现在以下情况被认为是不一致（incoherent）的：类型是外部的（foreign），或存在重叠的实现实例（即同一个 trait 为同一个类型实现了多次）。你的编译器不应允许这种不一致。
