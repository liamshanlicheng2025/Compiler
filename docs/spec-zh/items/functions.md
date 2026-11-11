# 函数（Functions）

> 中文译文，原文：`reference/RCompiler-Spec/src/items/functions.md`

r[items.fn.syntax]
```grammar,items
Function ->
    FunctionQualifiers `fn` IDENTIFIER
        `(` FunctionParameters? `)`
        FunctionReturnType?
        ( BlockExpression | `;` )

FunctionQualifiers -> `const`? 

FunctionParameters ->
      SelfParam `,`?
    | (SelfParam `,`)? FunctionParam (`,` FunctionParam)* `,`?

SelfParam -> ShorthandSelf

ShorthandSelf -> `&`? `mut`? `self`

FunctionParam -> PatternNoTopAlt `:` Type

FunctionReturnType -> `->` Type
```

r[items.fn.intro]
*函数*由一个块（即函数的*函数体*）、一个名字、一组参数和一个输出类型组成。除了名字之外，这些都是可选的。

r[items.fn.namespace]
函数用关键字 `fn` 声明，它把给定的名字定义在其所在模块或块的值命名空间中。

r[items.fn.signature]
函数可以声明一组*输入*变量作为参数——调用者通过它们把实参传进函数；以及函数完成时返回给调用者的值的*输出*类型。

r[items.fn.implicit-return]
如果输出类型没有显式写出，则它是单元类型 `()`。

r[items.fn.fn-item-type]
当函数被引用时，会得到对应零大小的*函数条目类型*（function item type）的一个一等值；调用这个值就等价于直接调用该函数。

例如，这是一个简单的函数：

```rust
fn answer_to_life_the_universe_and_everything() -> i32 {
    return 42;
}
```

r[items.fn.safety-qualifiers]
`safe` 函数在语义上只允许用在 `extern` 块中。

r[items.fn.params]
## 函数参数

r[items.fn.params.intro]
函数参数是不可反驳模式（irrefutable patterns），所以任何在无 `else` 的 `let` 绑定中合法的模式，也都可以用作参数：

```rust
fn first((value, _): (i32, i32)) -> i32 { value }
```

r[items.fn.params.self-pat]
如果第一个参数是 SelfParam（`self` 参数），则表明该函数是一个方法。

r[items.fn.params.self-restriction]
带 self 参数的函数只能作为 trait 或 impl 中的关联函数出现。

r[items.fn.params.varargs]
带 `...` token 的参数表示变参函数（variadic function），只能用作外部块（external block）函数的最后一个参数。变参参数可以带一个可选的标识符，如 `args: ...`。

r[items.fn.body]
## 函数体

r[items.fn.body.intro]
从概念上说，函数的函数体块外面还包着另一个块：它先绑定参数模式，然后 `return` 函数体的值。这意味着：块的尾表达式如果被求值，最终会被返回给调用者。与通常一样，函数体内的显式 return 表达式会在执行到时短路掉这个隐式返回。

例如，上面的函数的行为等价于写成：

```rust,ignore
// argument_0 是调用者实际传入的第一个实参
let (value, _) = argument_0;
return {
    value
};
```

r[items.fn.body.bodyless]
没有函数体块的函数以分号结尾。这种形式只能出现在 trait 或外部块中。

r[items.fn.const]
## 常量函数（Const functions）

r[items.fn.const.intro]
带 `const` 关键字限定的函数是常量函数。此外，元组式枚举变体的构造器也被视为常量函数，可以在常量上下文中调用。

r[items.fn.const.extern]
常量函数可以使用 `extern` 函数限定符。

r[items.fn.const.exclusivity]
常量函数不允许是 async 的。
