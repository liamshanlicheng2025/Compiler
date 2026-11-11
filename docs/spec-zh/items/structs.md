# 结构体（Structs）

> 中文译文，原文：`reference/RCompiler-Spec/src/items/structs.md`

r[items.struct.syntax]
```grammar,items
Struct ->
      StructStruct

StructStruct ->
    `struct` IDENTIFIER ( `{` StructFields? `}` | `;` )

StructFields -> StructField (`,` StructField)* `,`?

StructField -> IDENTIFIER `:` Type
```

r[items.struct.intro]
*结构体*是用关键字 `struct` 定义的具名结构体类型。

r[items.struct.namespace]
结构体声明把给定的名字定义在其所在模块或块的类型命名空间中。

一个 `struct` 条目及其使用的例子：

```rust
struct Point {x: i32, y: i32}
let p = Point {x: 10, y: 11};
let px: i32 = p.x;
```

> [!NOTE]
> 本规范不支持元组结构体。请改用命名字段的结构体；如果想要位置式字段，可以使用元组式枚举变体。

r[items.struct.unit]
*类单元结构体*是没有任何字段的结构体，定义时完全省略字段列表。这样的结构体会隐式定义一个与其同名的、类型为它自身的常量。例如：

```rust
struct Cookie;
let c = [Cookie, Cookie {}, Cookie, Cookie {}];
```

等价于

```rust
struct Cookie {}
const Cookie: Cookie = Cookie {};
let c = [Cookie, Cookie {}, Cookie, Cookie {}];
```

r[items.struct.layout]
结构体的精确内存布局不作规定，需要你自己设计。
