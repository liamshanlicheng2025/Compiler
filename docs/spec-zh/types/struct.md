# 结构体类型（Struct types）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/struct.md`

r[type.struct.intro]
`struct` *类型*是其他类型的异构积（heterogeneous product），这些组成类型称为该类型的*字段*。[^structtype]

r[type.struct.constructor]
`struct` 的新实例可以用结构体表达式构造。

r[type.struct.layout]
`struct` 的内存布局默认是未定义的，以便编译器进行字段重排等优化，但可以用 `repr` 属性固定布局。无论哪种情况，在对应的结构体*表达式*中字段可以按任意顺序给出；得到的 `struct` 值总是具有相同的内存布局。

r[type.struct.field-visibility]
`struct` 的字段可以带可见性修饰符，以允许模块外部访问结构体内的数据。

在 RCompiler 中，结构体总是可见的，不需要任何可见性修饰符。

> [!NOTE]
> 本规范不支持元组结构体（tuple struct）类型，只有命名字段的结构体类型可用。

r[type.struct.unit]
*类单元结构体*（unit-like struct）类型类似于结构体类型，只是它没有字段。由对应的结构体表达式构造出的那个值，是该类型唯一的值。

[^structtype]: `struct` 类型类似于 C 语言的 `struct` 类型、ML 语言家族的*记录*（record）类型，或 Lisp 语言家族的 *struct* 类型。
