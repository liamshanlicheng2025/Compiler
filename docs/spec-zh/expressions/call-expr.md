# 调用表达式（Call expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/call-expr.md`

r[expr.call.syntax]
```grammar,expressions
CallExpression -> Expression `(` CallParams? `)`

CallParams -> Expression ( `,` Expression )* `,`?
```

r[expr.call.intro]
*调用表达式*调用一个函数。调用表达式的语法是：一个表达式（称为*函数操作数*），后跟一个括号括起的、逗号分隔的表达式列表（称为*参数操作数*）。

r[expr.call.convergence]
如果函数最终返回，那么该表达式完成。

r[expr.call.autoref-deref]
如有需要，会自动取一次借用。函数操作数也会按需被自动解引用。

调用表达式的例子：

```rust
# fn add(x: i32, y: i32) -> i32 { 0 }
let three: i32 = add(1i32, 2i32);
let name: &'static str = (|| "Rust")();
```

r[expr.call.desugar]
## 函数调用的消歧（Disambiguating Function Calls）

r[expr.call.desugar.fully-qualified]
所有函数调用都是更显式的完全限定语法（fully-qualified syntax）的语法糖。

r[expr.call.desugar.ambiguity]
函数调用是否需要写成完全限定形式，取决于在作用域内条目的背景下调用是否有歧义。

> [!NOTE]
> 过去，"Unambiguous Function Call Syntax"、"Universal Function Call Syntax" 或 "UFCS" 这些术语曾出现在文档、issue、RFC 和其他社区文字中。但这些术语缺乏描述力，还可能把问题搞混。我们在这里提到它们只是为了便于搜索。

r[expr.call.desugar.limits]
有几种常见情形会导致方法或关联函数调用的接收者或所指对象出现歧义，包括：

* 作用域内多个 trait 为同一类型定义了同名方法
* 不希望发生自动解引用（auto-deref）；例如要区分智能指针本身的方法和指针所指对象的方法
* 像 `default()` 这样没有参数的方法，以及像 `size_of()` 这样返回类型属性的函数

r[expr.call.desugar.explicit-path]
要消除歧义，程序员可以用更具体的路径、类型或 trait 来指代他们想要的方法或函数。

例如：

```rust
trait Pretty {
    fn print(&self);
}

trait Ugly {
    fn print(&self);
}

struct Foo;
impl Pretty for Foo {
    fn print(&self) {}
}

struct Bar;
impl Pretty for Bar {
    fn print(&self) {}
}
impl Ugly for Bar {
    fn print(&self) {}
}

fn main() {
    let f = Foo;
    let b = Bar;

    // 可以这样写，因为 `Foo` 只有一个叫 `print` 的条目
    f.print();
    // 更显式，对 `Foo` 来说不是必需的
    Foo::print(&f);
    // 如果你不嫌啰嗦
    <Foo as Pretty>::print(&f);

    // b.print(); // 错误：找到多个 'print'
    // Bar::print(&b); // 仍然是错误：找到多个 `print`

    // 因为作用域内有多个条目定义了 `print`，所以必须这样写
    <Bar as Pretty>::print(&b);
}
```

更多细节和动机见 RFC 132。
