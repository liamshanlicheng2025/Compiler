# 表达式（Expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions.md`

r[expr.syntax]
```grammar,expressions
Expression ->
      ExpressionWithoutBlock
    | ExpressionWithBlock

ExpressionWithoutBlock ->
        LiteralExpression
      | PathExpression
      | OperatorExpression
      | GroupedExpression
      | ArrayExpression
      | IndexExpression
      | StructExpression
      | CallExpression
      | MethodCallExpression
      | FieldExpression
      | ContinueExpression
      | BreakExpression
      | ReturnExpression

ExpressionWithBlock ->
        BlockExpression
      | LoopExpression
      | IfExpression
```

r[expr.intro]
表达式可以扮演两个角色：它总是产生一个*值*，并且可能产生*副作用*（effects，也称"side effects"）。

r[expr.evaluation]
表达式*求值得到*（evaluates to）一个值，并在*求值*过程中产生副作用。

r[expr.operands]
许多表达式包含子表达式，称为该表达式的*操作数*（operands）。

r[expr.behavior]
每种表达式的含义规定了以下几件事：

* 求值该表达式时是否要求值它的操作数
* 以什么顺序求值这些操作数
* 如何组合操作数的值来得到表达式的值

r[expr.structure]
就这样，表达式的结构决定了执行的结构。块也不过是另一种表达式，所以块、语句、表达式、块之间可以任意深度地互相递归嵌套。

> [!NOTE]
> 我们给表达式的操作数起了名字以便讨论，但这些名字并不稳定，可能会改变。

r[expr.precedence]
## 表达式优先级

Rust 运算符和表达式的优先级如下，从强到弱排列。同一优先级的二元运算符按它们的结合性分组。

| 运算符/表达式                | 结合性       |
|-----------------------------|-------------|
| 路径（Paths）                 |             |
| 方法调用（Method calls）        |             |
| 字段表达式（Field expressions） | 从左到右     |
| 函数调用（Function calls）、数组下标 |          |
| 一元 `-` `!` `*` 借用（`&`/`&mut`） |          |
| `as`                        | 从左到右     |
| `*` `/` `%`                 | 从左到右     |
| `+` `-`                     | 从左到右     |
| `<<` `>>`                   | 从左到右     |
| `&`                         | 从左到右     |
| `^`                         | 从左到右     |
| <code>&#124;</code>         | 从左到右     |
| `==` `!=` `<` `>` `<=` `>=` | 需要括号     |
| `&&`                        | 从左到右     |
| <code>&#124;&#124;</code>   | 从左到右     |
| `=` `+=` `-=` `*=` `/=` `%=` `&=` <code>&#124;=</code> `^=` `<<=` `>>=` | 从右到左 |
| `return` `break`            |             |

r[expr.operand-order]
## 操作数的求值顺序

r[expr.operand-order.default]
下面列出的表达式都以相同的方式求值它们的操作数，具体方式见列表之后。其他表达式要么没有操作数，要么按各自页面描述的方式有条件地求值操作数。

* 解引用表达式
* 取负表达式
* 算术和逻辑二元运算符
* 比较运算符
* 类型转换表达式
* 带括号的表达式（Grouped expression）
* 数组表达式
* 下标表达式
* 结构体表达式
* 调用表达式
* 方法调用表达式
* 字段表达式
* Break 表达式
* Return 表达式

r[expr.operand-order.operands-before-primary]
这些表达式的操作数在应用表达式本身的效果之前求值。有多个操作数的表达式按源代码中书写的顺序从左到右求值。

> [!NOTE]
> 哪些子表达式是一个表达式的操作数，由上一节的表达式优先级决定。

例如，下面两个 `next` 方法调用总是以相同的顺序执行：

```rust
# // 这里用 vec 而不是数组来避免引用，
# // 因为在写这个例子的时候
# // 还没有稳定的所有权数组迭代器。
let mut one_two = vec![1, 2].into_iter();
assert_eq!(
    (1, 2),
    (one_two.next().unwrap(), one_two.next().unwrap())
);
```

> [!NOTE]
> 由于这个规则是递归应用的，这些表达式也会从最内层到最外层求值，在没有更内层的子表达式之前先忽略同级的兄弟表达式。

r[expr.place-value]
## 位置表达式与值表达式（Place Expressions and Value Expressions）

r[expr.place-value.intro]
表达式分成两大类：位置表达式（place expressions）和值表达式（value expressions）；另外还有第三小类，称为被赋值表达式（assignee expressions）。在每个表达式内部，操作数同样可以出现在位置上下文（place context）或值上下文（value context）中。一个表达式的求值方式既取决于它自己的类别，也取决于它所处的上下文。

r[expr.place-value.place-memory-location]
*位置表达式*是表示一个内存位置的表达式。

r[expr.place-value.place-expr-kinds]
这类表达式包括：局部变量、静态变量、解引用（`*expr`）、数组下标表达式（`expr[expr]`）、字段引用（`expr.f`），以及带括号的位置表达式。

r[expr.place-value.value-expr-kinds]
其他所有表达式都是值表达式。

r[expr.place-value.value-result]
*值表达式*是表示一个实际值的表达式。

r[expr.place-value.place-context]
以下上下文是*位置表达式*上下文：

* 复合赋值表达式的左操作数。
* 一元借用（`&`/`&mut`）或解引用运算符的操作数。
* 字段表达式的操作数。
* 数组下标表达式中被索引的那个操作数。
* 任何隐式借用的操作数。
* 结构体表达式函数式更新（functional update）语法的基表达式。

> [!NOTE]
> 历史上，位置表达式叫*左值*（lvalues），值表达式叫*右值*（rvalues）。

r[expr.place-value.assignee]
*被赋值表达式*是出现在赋值表达式左操作数位置的表达式。具体地说，被赋值表达式包括：

- 位置表达式。
- 下划线（`_`）。
- 由被赋值表达式组成的结构体（字段可命名）。
- 单元结构体。

r[expr.place-value.parenthesis]
被赋值表达式内部允许任意加括号。

r[expr.move]
### 移动与复制的类型（Moved and copied types）

r[expr.move.intro]
当一个位置表达式在值表达式上下文中求值，或在模式中按值绑定时，它表示该内存位置中*存放的*值。

r[expr.move.copy]
如果该值的类型实现了 `Copy`，那么值会被复制。

r[expr.move.requires-sized]
在其余情况下，如果该类型是 `Sized` 的，那么可能可以移动这个值。

r[expr.move.movable-place]
只有以下位置表达式可以被移出（moved out of）：

* 当前未被借用的变量。
* 临时值（temporaries）。
* 可以被移出的、且不实现 `Drop` 的位置表达式的字段。
* 对类型为 `Box<T>` 且本身可以被移出的表达式做解引用的结果。

r[expr.move.deinitialization]
从一个求值结果为局部变量的位置表达式移出值之后，该位置变为未初始化状态，在被重新初始化之前不能再读取。

r[expr.move.place-invalid]
在所有其他情况下，试图在值表达式上下文中使用位置表达式都是错误。

r[expr.mut]
### 可变性（Mutability）

r[expr.mut.intro]
一个位置表达式要被赋值、被可变地借用、被隐式可变借用，或被绑定到包含 `ref mut` 的模式中，它必须是*可变的*。我们把这类表达式称为*可变位置表达式*。相对地，其他位置表达式称为*不可变位置表达式*。

r[expr.mut.valid-places]
以下表达式可以作为可变位置表达式上下文：

* 当前未被借用的可变变量。
* 可变的 `static` 条目。
* 临时值。
* 字段：这会把子表达式放在可变位置表达式上下文中求值。
* `*mut T` 指针的解引用。
* 对类型为 `&mut T` 的变量（或变量的字段）做解引用。
  注意：这是下一条规则要求的一个例外。
* 对实现了 `DerefMut` 的类型做解引用：这要求被解引用的值在可变位置表达式上下文中求值。
* 对实现了 `IndexMut` 的类型做数组下标索引：这会把被索引的值（但不含下标）放在可变位置表达式上下文中求值。

r[expr.temporary]
### 临时值（Temporaries）

在大多数位置表达式上下文中使用值表达式时，会创建一个临时的匿名内存位置，并用该值初始化。表达式转而求值为那个位置——除非它被常量提升（promoted）为 `static`。临时值的析构作用域（drop scope）通常是包住它的语句的结尾。

r[expr.implicit-borrow]
### 隐式借用（Implicit Borrows）

r[expr.implicit-borrow-intro]
某些表达式会通过隐式借用的方式，把一个表达式当作位置表达式对待。例如，可以直接比较两个非定长切片是否相等，因为 `==` 运算符会隐式借用它的操作数：

```rust
# let c = [1, 2, 3];
# let d = vec![1, 2, 3];
let a: &[i32];
let b: &[i32];
# a = &c;
# b = &d;
// ...
*a == *b;
// 等价形式：
::std::cmp::PartialEq::eq(&*a, &*b);
```

r[expr.implicit-borrow.application]
以下表达式中可能发生隐式借用：

* 方法调用表达式的左操作数。
* 字段表达式的左操作数。
* 调用表达式的左操作数。
* 数组下标表达式的左操作数。
* 解引用运算符（`*`）的操作数。
* 比较运算符的操作数。
* 复合赋值的左操作数。
