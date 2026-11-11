# 字段访问表达式（Field access expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/field-expr.md`

r[expr.field.syntax]
```grammar,expressions
FieldExpression -> Expression `.` IDENTIFIER
```

r[expr.field.intro]
*字段表达式*是一个位置表达式，求值结果是结构体或联合体某个字段的位置。

r[expr.field.mut]
当操作数是可变的，字段表达式也是可变的。

r[expr.field.form]
字段表达式的语法是：一个表达式（称为*容器操作数*），然后一个 `.`，最后一个标识符。

r[expr.field.not-method-call]
字段表达式后面不能跟括号括起的逗号分隔表达式列表——那会被解析为方法调用表达式。也就是说，字段表达式不能作为调用表达式的函数操作数。

> [!NOTE]
> 要在调用表达式中使用字段，把字段表达式用括号括起来：
>
> ```rust
> # struct HoldsCallable<F: Fn()> { callable: F }
> let holds_callable = HoldsCallable { callable: || () };
>
> // 非法：被解析为调用名为 "callable" 的方法
> // holds_callable.callable();
>
> // 合法
> (holds_callable.callable)();
> ```

例子：

```rust,ignore
mystruct.myfield;
foo().x;
(Struct {a: 10, b: 20}).a;
(mystruct.function_field)() // 包含字段表达式的调用表达式
```

r[expr.field.autoref-deref]
## 自动解引用（Automatic dereferencing）

如果容器操作数的类型实现了 `Deref` 或 `DerefMut`（取决于操作数是否可变），它会被*自动解引用*任意多次，直到字段访问可以进行。这个过程也简称为 *autoderef*。

在 RCompiler 中，只有 `Box` 类型支持自动解引用。

r[expr.field.borrow]
## 借用（Borrowing）

在借用时，结构体的各个字段（或指向结构体的引用的各个字段）被视为互相独立的实体。如果结构体没有实现 `Drop` 且存放在局部变量中，这也适用于分别移出它的每个字段。如果是通过 `Box` 以外的用户自定义类型做自动解引用，则不适用。

```rust
struct A { f1: String, f2: String, f3: String }
let mut x: A;
# x = A {
#     f1: "f1".to_string(),
#     f2: "f2".to_string(),
#     f3: "f3".to_string()
# };
let a: &mut String = &mut x.f1; // x.f1 被可变借用
let b: &String = &x.f2;         // x.f2 被不可变借用
let c: &String = &x.f2;         // 可以再次借用
let d: String = x.f3;           // 移出 x.f3
let e: String = x.f3;           // 错误：使用了已被移走的值
```
