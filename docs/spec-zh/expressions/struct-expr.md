# 结构体表达式（Struct expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/struct-expr.md`

r[expr.struct.syntax]
```grammar,expressions
StructExpression ->
    PathInExpression `{` StructExprFields? `}`

StructExprFields ->
    StructExprField (`,` StructExprField)* `,`?

StructExprField ->
      IDENTIFIER `:` Expression
```

r[expr.struct.intro]
*结构体表达式*创建一个结构体、枚举或联合体的值。它由指向结构体或枚举变体条目的路径，后跟该条目各字段的值组成。

结构体表达式的例子：

```rust
struct Point { x: f64, y: f64 }
struct NothingInMe { }
struct User { name: String, age: u32, score: usize }
enum Enum { Variant {} }
Point { x: 10.0, y: 20.0 };
NothingInMe {};
let u = User { name: "Joe".to_string(), age: 35, score: 100_000 };
Enum::Variant {};
```

> [!NOTE]
> 本规范不支持元组结构体。对元组式枚举变体，使用调用表达式来引用值命名空间中的构造器；这不同于用花括号的结构体表达式——后者引用的是类型命名空间中的构造器。单元结构体和单元枚举变体通常用路径表达式实例化，该路径引用值命名空间中的常量。
>
> ```rust
> struct Gamma;
> // Gamma 的单元值，引用值命名空间中的常量。
> let a = Gamma;
> // 与 `a` 完全相同的值，但用结构体表达式构造，
> // 引用的是类型命名空间。
> let b = Gamma {};
>
> enum ColorSpace { Oklch }
> let c = ColorSpace::Oklch;
> let d = ColorSpace::Oklch {};
> ```

r[expr.struct.field]
## 带字段的结构体表达式

r[expr.struct.field.intro]
字段写在花括号中的结构体表达式，允许你按任意顺序为每个字段指定值。字段名和它的值之间用冒号分隔。
