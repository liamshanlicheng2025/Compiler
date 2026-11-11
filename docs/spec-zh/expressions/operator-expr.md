
# 运算符表达式（Operator expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/operator-expr.md`

r[expr.operator.syntax]
```grammar,expressions
OperatorExpression ->
      BorrowExpression
    | DereferenceExpression
    | NegationExpression
    | ArithmeticOrLogicalExpression
    | ComparisonExpression
    | LazyBooleanExpression
    | TypeCastExpression
    | AssignmentExpression
    | CompoundAssignmentExpression
```

r[expr.operator.intro]
运算符由 Rust 语言为内建类型定义。

r[expr.operator.trait]
以下许多运算符也可以用 `std::ops` 或 `std::cmp` 中的 trait 重载。

r[expr.operator.int-overflow]
## 溢出（Overflow）

r[expr.operator.int-overflow.intro]
整数运算符溢出时，在 debug 模式编译下会 panic。可以用 `-C debug-assertions` 和 `-C overflow-checks` 编译选项更直接地控制这个行为。以下情况被视为溢出：

r[expr.operator.int-overflow.binary-arith]
* `+`、`*` 或二元 `-` 产生的值大于能存储的最大值，或小于能存储的最小值。

r[expr.operator.int-overflow.unary-neg]
* 对任何有符号整数类型的最小值应用一元 `-`，除非操作数是字面量表达式（或者是单独处于一个或多个括号表达式之内的字面量表达式）。

r[expr.operator.int-overflow.div]
* 使用 `/` 或 `%`，且左操作数是有符号整数类型的最小值、右操作数是 `-1`。
  出于历史原因，即使 `-C overflow-checks` 被禁用，这些检查也会进行。

r[expr.operator.int-overflow.shift]
* 使用 `<<` 或 `>>`，且右操作数大于或等于左操作数类型的位数，或为负数。

> [!NOTE]
> 对一元 `-` 后的字面量表达式的豁免意味着：`-128_i8` 或 `let j: i8 = -(128)` 这样的形式永远不会 panic，并且有期望的值 -128。
>
> 在这些情况下，字面量表达式本身就具有其类型的最小值（例如 `128_i8` 的值是 -128），因为整数字面量会按其类型截断（见整数字面量表达式一节的描述）。
>
> 按照二进制补码的溢出约定，对这些最小值取负后值不变。
>
> 在 `rustc` 中，这些最小值表达式也会被 `overflowing_literals` lint 检查忽略。

r[expr.operator.borrow]
## 借用运算符（Borrow operators）

r[expr.operator.borrow.syntax]
```grammar,expressions
BorrowExpression ->
      (`&`|`&&`) Expression
    | (`&`|`&&`) `mut` Expression
```

r[expr.operator.borrow.intro]
`&`（共享借用）和 `&mut`（可变借用）运算符是一元前缀运算符。

r[expr.operator.borrow.result]
当作用于位置表达式时，这个表达式产生一个指向该值所引用内存位置的引用（指针）。

r[expr.operator.borrow.lifetime]
在引用存活期间，该内存位置也处于被借用状态。对共享借用（`&`）来说，这意味着该位置不能被修改，但可以被读取或再次共享。对可变借用（`&mut`）来说，在借用过期之前，该位置不能以任何方式被访问。

r[expr.operator.borrow.mut]
`&mut` 在可变位置表达式上下文中求值它的操作数。

r[expr.operator.borrow.temporary]
如果 `&` 或 `&mut` 运算符作用于值表达式，则会创建一个临时值。

这些运算符不能被重载。

```rust
{
    // 创建一个值为 7 的临时值，存活到这个作用域结束。
    let shared_reference = &7;
}
let mut array = [-2, 3, 9];
{
    // 在这个作用域内可变地借用 `array`。
    // `array` 只能通过 `mutable_reference` 使用。
    let mutable_reference = &mut array;
}
```

r[expr.borrow.and-and-syntax]
尽管 `&&` 是单个 token（惰性"与"运算符），但在借用表达式的上下文中，它等价于两次借用：

```rust
// 含义相同：
let a = &&  10;
let a = & & 10;

// 含义相同：
let a = &&&&  mut 10;
let a = && && mut 10;
let a = & & & & mut 10;
```

r[expr.deref]
## 解引用运算符（The dereference operator）

r[expr.deref.syntax]
```grammar,expressions
DereferenceExpression -> `*` Expression
```

r[expr.deref.intro]
`*`（解引用）运算符也是一元前缀运算符。

r[expr.deref.result]
当作用于指针时，它表示指针指向的位置。

r[expr.deref.mut]
如果表达式的类型是 `&mut T` 或 `*mut T`，且它是局部变量、局部变量的（嵌套）字段，或者是可变位置表达式，那么得到的内存位置可以被赋值。

r[expr.deref.safety]
解引用裸指针需要 `unsafe`。

r[expr.deref.traits]
对非指针类型，`*x` 在不可变位置表达式上下文中等价于 `*std::ops::Deref::deref(&x)`，在可变位置表达式上下文中等价于 `*std::ops::DerefMut::deref_mut(&mut x)`。

```rust
let x = &7;
assert_eq!(*x, 7);
let y = &mut 9;
*y = 11;
assert_eq!(*y, 11);
```

r[expr.negate]
## 取负运算符（Negation operators）

r[expr.negate.syntax]
```grammar,expressions
NegationExpression ->
      `-` Expression
    | `!` Expression
```

r[expr.negate.intro]
这是最后两个一元运算符。

r[expr.negate.results]
下表总结了它们在原生类型上的行为，以及为其他类型重载这些运算符所用的 trait。请记住：有符号整数总是用二进制补码表示。这些运算符的所有操作数都在值表达式上下文中求值，因此会被移动或复制。

| 符号  | 整数        | `bool`       | 浮点          | 重载 trait         |
|-------|------------|--------------|--------------|--------------------|
| `-`   | 取负*       |              | 取负         | `std::ops::Neg`    |
| `!`   | 按位取反    | 逻辑非        |              | `std::ops::Not`    |

\* 仅适用于有符号整数类型。

这些运算符的一些例子：

```rust
let x = 6;
assert_eq!(-x, -6);
assert_eq!(!x, -7);
assert_eq!(true, !false);
```

r[expr.arith-logic]
## 算术与逻辑二元运算符

r[expr.arith-logic.syntax]
```grammar,expressions
ArithmeticOrLogicalExpression ->
      Expression `+` Expression
    | Expression `-` Expression
    | Expression `*` Expression
    | Expression `/` Expression
    | Expression `%` Expression
    | Expression `&` Expression
    | Expression `|` Expression
    | Expression `^` Expression
    | Expression `<<` Expression
    | Expression `>>` Expression
```

r[expr.arith-logic.intro]
二元运算符表达式都以中缀记法书写。

r[expr.arith-logic.behavior]
下表总结了算术和逻辑二元运算符在原生类型上的行为，以及为其他类型重载这些运算符所用的 trait。请记住：有符号整数总是用二进制补码表示。这些运算符的所有操作数都在值表达式上下文中求值，因此会被移动或复制。

| 符号  | 整数                  | `bool`       | 浮点          | 重载 trait         | 重载复合赋值 trait               |
|-------|----------------------|--------------|--------------|--------------------| -------------------------------- |
| `+`   | 加法                  |              | 加法         | `std::ops::Add`    | `std::ops::AddAssign`            |
| `-`   | 减法                  |              | 减法         | `std::ops::Sub`    | `std::ops::SubAssign`            |
| `*`   | 乘法                  |              | 乘法         | `std::ops::Mul`    | `std::ops::MulAssign`            |
| `/`   | 除法*†                |              | 除法         | `std::ops::Div`    | `std::ops::DivAssign`            |
| `%`   | 取余**†               |              | 取余         | `std::ops::Rem`    | `std::ops::RemAssign`            |
| `&`   | 按位与                | 逻辑与        |              | `std::ops::BitAnd` | `std::ops::BitAndAssign`         |
| <code>&#124;</code> | 按位或 | 逻辑或        |              | `std::ops::BitOr`  | `std::ops::BitOrAssign`          |
| `^`   | 按位异或              | 逻辑异或      |              | `std::ops::BitXor` | `std::ops::BitXorAssign`         |
| `<<`  | 左移                  |              |              | `std::ops::Shl`    | `std::ops::ShlAssign`            |
| `>>`  | 右移***               |              |              | `std::ops::Shr`    | `std::ops::ShrAssign`            |

\* 整数除法向零取整。

\*\* Rust 使用以截断除法定义的取余。给定 `remainder = dividend % divisor`，余数与被除数同号。

\*\*\* 有符号整数类型是算术右移，无符号整数类型是逻辑右移。

† 对整数类型，除以零会 panic。

这些运算符的使用例子：

```rust
assert_eq!(3 + 6, 9);
assert_eq!(5.5 - 1.25, 4.25);
assert_eq!(-5 * 14, -70);
assert_eq!(14 / 3, 4);
assert_eq!(100 % 7, 2);
assert_eq!(0b1010 & 0b1100, 0b1000);
assert_eq!(0b1010 | 0b1100, 0b1110);
assert_eq!(0b1010 ^ 0b1100, 0b110);
assert_eq!(13 << 3, 104);
assert_eq!(-10 >> 2, -3);
```

r[expr.arith-logic.string-append]
### 字符串拼接

对 `String` 类型，`+` 运算符通过调用 `append` 方法执行字符串拼接。左操作数必须是可变的 `String`，右操作数必须是 `&str` 类型。

```rust
let mut s: String = String::from("Hello");
s = s + ", world!"; // 等价于 s.append(", world!")
println(s.as_str()); // 打印 "Hello, world!"
```

r[expr.cmp]
## 比较运算符（Comparison Operators）

r[expr.cmp.syntax]
```grammar,expressions
ComparisonExpression ->
      Expression `==` Expression
    | Expression `!=` Expression
    | Expression `>` Expression
    | Expression `<` Expression
    | Expression `>=` Expression
    | Expression `<=` Expression
```

r[expr.cmp.intro]
比较运算符同样为原生类型和标准库中的许多类型定义。

r[expr.cmp.paren-chaining]
链式使用比较运算符时必须加括号。例如，表达式 `a == b == c` 是非法的，可以写成 `(a == b) == c`。

r[expr.cmp.trait]
与算术和逻辑运算符不同，重载这些运算符的 trait 更一般地用于表明一个类型可以如何比较；使用这些 trait 作为约束的函数很可能会假定它们定义了真正的比较。标准库中的许多函数和宏随后可以利用这个假定（尽管不能保证安全性）。

r[expr.cmp.place]
与上面的算术和逻辑运算符不同，这些运算符会隐式地对操作数取共享借用，在位置表达式上下文中求值它们：

```rust
# let a = 1;
# let b = 1;
a == b;
// 等价于
::std::cmp::PartialEq::eq(&a, &b);
```

这意味着操作数不需要被移出。

r[expr.cmp.behavior]

| 符号  | 含义                  | 重载方法                     |
|-------|----------------------|----------------------------|
| `==`  | 相等                  | `std::cmp::PartialEq::eq`  |
| `!=`  | 不等                  | `std::cmp::PartialEq::ne`  |
| `>`   | 大于                  | `std::cmp::PartialOrd::gt` |
| `<`   | 小于                  | `std::cmp::PartialOrd::lt` |
| `>=`  | 大于等于              | `std::cmp::PartialOrd::ge` |
| `<=`  | 小于等于              | `std::cmp::PartialOrd::le` |

比较运算符的使用例子：

```rust
assert!(123 == 123);
assert!(23 != -12);
assert!(12.5 > 12.2);
assert!([1, 2, 3] < [1, 3, 4]);
assert!('A' <= 'B');
assert!("World" >= "Hello");
```

r[expr.bool-logic]
## 惰性布尔运算符（Lazy boolean operators）

r[expr.bool-logic.syntax]
```grammar,expressions
LazyBooleanExpression ->
      Expression `||` Expression
    | Expression `&&` Expression
```

r[expr.bool-logic.intro]
`||` 和 `&&` 运算符可以作用于布尔类型的操作数。`||` 表示逻辑"或"，`&&` 表示逻辑"与"。

r[expr.bool-logic.conditional-evaluation]
它们与 `|` 和 `&` 的区别在于：只有当左操作数还不能确定整个表达式的结果时，才求值右操作数。也就是说，`||` 只在左操作数求值为 `false` 时才求值右操作数，`&&` 只在左操作数求值为 `true` 时才求值右操作数。

```rust
let x = false || true; // true
let y = false && panic!(); // false，不会求值 `panic!()`
```

r[expr.as]
## 类型转换表达式（Type cast expressions）

r[expr.as.syntax]
```grammar,expressions
TypeCastExpression -> Expression `as` TypeNoBounds
```

r[expr.as.intro]
类型转换表达式用二元运算符 `as` 表示。

r[expr.as.result]
执行 `as` 表达式会把左边的值转换为右边的类型。

r[expr.as.coercions]
`as` 可以用来显式执行 coercion（类型强制转换），以及下表中列出的额外转换。任何既不符合 coercion 规则、也不在表中的转换都是编译错误。这里 `*T` 指 `*const T` 或 `*mut T`。`m` 在引用类型中表示可选的 `mut`，在指针类型中表示 `mut` 或 `const`。

| `e` 的类型           | `U`                   | `e as U` 执行的转换                            |
|-----------------------|-----------------------|-------------------------------------------------------|
| 整数类型 | 整数类型 | 数值转换（Numeric cast）                       |
| `bool` 或 `char`      | 整数类型          | 原生类型到整数的转换 |

### 语义

r[expr.as.numeric]
#### 数值转换（Numeric cast）

r[expr.as.numeric.int-same-size]
* 两个相同大小的整数之间转换（如 i32 -> u32）是无操作（no-op）
  （Rust 对定长有符号整数的负值使用二进制补码）

  ```rust
  assert_eq!(42i32 as u32, 42u32);
  assert_eq!(-1i32 as u32, 4294967295u32);
  assert_eq!(4294967295u32 as i32, -1i32);
  assert_eq!(-1isize as usize, 4294967295usize); // 32 位机器上
  ```

r[expr.as.numeric.int-truncation]
* 从较大的整数转换到较小的整数（如 u32 -> u8）会截断

  ```rust
  assert_eq!(42u16 as u8, 42u8);
  assert_eq!(1234u16 as u8, 210u8);
  assert_eq!(0xabcdu16 as u8, 0xcdu8);

  assert_eq!(-42i16 as i8, -42i8);
  assert_eq!(1234u16 as i8, -46i8);
  assert_eq!(0xabcdi32 as i8, -51i8);
  ```

r[expr.as.numeric.int-extension]
* 从较小的整数转换到较大的整数（如 u8 -> u32）会：
    * 如果源类型是无符号的，做零扩展（zero-extend）
    * 如果源类型是有符号的，做符号扩展（sign-extend）

  ```rust
  assert_eq!(42i8 as i16, 42i16);
  assert_eq!(-17i8 as i16, -17i16);
  assert_eq!(0b1000_1010u8 as u16, 0b0000_0000_1000_1010u16, "零扩展");
  assert_eq!(0b0000_1010i8 as i16, 0b0000_0000_0000_1010i16, "符号扩展 0");
  assert_eq!(0b1000_1010u8 as i8 as i16, 0b1111_1111_1000_1010u16 as i16, "符号扩展 1");
  ```

r[expr.as.bool-char-as-int]
#### 原生类型到整数的转换

* `false` 转换为 `0`，`true` 转换为 `1`
* `char` 转换为其码点的值，然后必要时再做数值转换。

```rust
assert_eq!(false as i32, 0);
assert_eq!(true as i32, 1);
assert_eq!('A' as i32, 65);
// assert_eq!('Ö' as i32, 214);
```

r[expr.assign]
## 赋值表达式（Assignment expressions）

r[expr.assign.syntax]
```grammar,expressions
AssignmentExpression -> Expression `=` Expression
```

r[expr.assign.intro]
*赋值表达式*把一个值移动到指定的位置。

r[expr.assign.assignee]
赋值表达式由一个可变的被赋值表达式（*被赋值操作数*），后跟等号（`=`）和一个值表达式（*被赋的值操作数*）组成。

r[expr.assign.behavior-basic]
在最基本的形式中，被赋值表达式是一个位置表达式，我们先讨论这种情况。

r[expr.assign.behavior-destructuring]
更一般的解构赋值（destructuring assignment）情况在下文讨论，但这种情况总是可以分解为对位置表达式的一系列顺序赋值，可以认为基本情况更为根本。

r[expr.assign.basic]
### 基本赋值

r[expr.assign.evaluation-order]
求值赋值表达式从求值它的操作数开始。**先求值被赋的值操作数（右边），然后求值被赋值表达式（左边）。**

r[expr.assign.destructuring-order]
对**解构**赋值，被赋值表达式的子表达式按从左到右的顺序求值。

> [!NOTE]
> 这与其他表达式不同：右操作数先于左操作数求值。

r[expr.assign.drop-target]
然后，它的效果是先把被赋值位置上原有的值析构（drop），除非该位置是未初始化的局部变量或局部变量的未初始化字段。

r[expr.assign.behavior]
接着，它把被赋的值复制或移动到被赋值的位置。

r[expr.assign.result]
赋值表达式总是产生**单元值** `()`，这与 C 或 C++ 不同。

例子：

```rust
let mut x = 0;
let y = 0;
x = y;
let mut z;
z = x = y; // 先求值 (x = y)，z 的类型是 ()
```

r[expr.assign.destructure]
### 解构赋值（Destructuring assignments）

r[expr.assign.destructure.intro]
解构赋值是变量声明中解构模式匹配的对应物，允许对元组或结构体等复杂值赋值。例如，我们可以交换两个可变变量：

```rust
let (mut a, mut b) = (0, 1);
// 用解构赋值交换 `a` 和 `b`。
(b, a) = (a, b);
```

r[expr.assign.destructure.assignee]
与用 `let` 做解构声明不同，由于语法歧义，模式不能出现在赋值的左边。作为替代，一组与模式对应的表达式被指定为被赋值表达式，允许出现在赋值的左边。被赋值表达式随后被脱糖（desugar）为模式匹配加顺序赋值。

r[expr.assign.destructure.irrefutable]
脱糖后的模式必须是可反驳的（irrefutable，即必然匹配的）：特别地，这意味着解构赋值只允许编译期长度已知的切片模式和平凡切片 `[..]`。

脱糖方法很直接，用例子说明最清楚：

```rust
# struct Struct { x: u32, y: u32 }
# let (mut a, mut b) = (0, 0);
(a, b) = (3, 4);

[a, b] = [3, 4];

Struct { x: a, y: b } = Struct { x: 3, y: 4};

// 脱糖为：

{
    let (_a, _b) = (3, 4);
    a = _a;
    b = _b;
}

{
    let [_a, _b] = [3, 4];
    a = _a;
    b = _b;
}

{
    let Struct { x: _a, y: _b } = Struct { x: 3, y: 4};
    a = _a;
    b = _b;
}
```

r[expr.assign.destructure.repeat-ident]
同一个被赋值表达式中不禁止标识符被多次使用。

r[expr.assign.destructure.discard-value]
下划线表达式和空的范围表达式可以用来忽略某些值而不绑定它们。

r[expr.assign.destructure.default-binding]
注意：默认绑定模式（default binding modes）不适用于脱糖后的表达式。

r[expr.compound-assign]
## 复合赋值表达式（Compound assignment expressions）

r[expr.compound-assign.syntax]
```grammar,expressions
CompoundAssignmentExpression ->
      Expression `+=` Expression
    | Expression `-=` Expression
    | Expression `*=` Expression
    | Expression `/=` Expression
    | Expression `%=` Expression
    | Expression `&=` Expression
    | Expression `|=` Expression
    | Expression `^=` Expression
    | Expression `<<=` Expression
    | Expression `>>=` Expression
```

r[expr.compound-assign.intro]
*复合赋值表达式*把算术和逻辑二元运算符与赋值表达式组合在一起。

例如：

```rust
let mut x = 5;
x += 1;
assert!(x == 6);
```

复合赋值的语法是：一个可变的位置表达式（*被赋值操作数*），然后是运算符与 `=` 组成的单个 token（中间没有空白），然后是一个值表达式（*修改操作数*）。

r[expr.compound-assign.place]
与其他位置操作数不同，被赋值的位置操作数必须是位置表达式。

r[expr.compound-assign.no-value]
试图使用值表达式是编译错误，而不会把它提升为临时值。

r[expr.compound-assign.operand-order]
复合赋值表达式的求值取决于运算符两边操作数的类型。

r[expr.compound-assign.primitive-order]
如果两个类型都是原生类型，那么先求值修改操作数（右边），再求值被赋值操作数（左边）。然后用被赋值操作数和修改操作数的值执行运算符对应的操作，把结果写入被赋值操作数的位置。

> [!NOTE]
> 这与其他表达式不同：右操作数先于左操作数求值。

r[expr.compound-assign.trait]
否则，该表达式是调用该运算符的重载复合赋值 trait 函数的语法糖（见本章前面的表格）。会自动对被赋值操作数取一次可变借用。

例如，`example` 中下面的表达式语句是等价的：

```rust
# struct Addable;
# use std::ops::AddAssign;

impl AddAssign<Addable> for Addable {
    /* */
# fn add_assign(&mut self, other: Addable) {}
}

fn example() {
# let (mut a1, a2) = (Addable, Addable);
  a1 += a2;

# let (mut a1, a2) = (Addable, Addable);
  AddAssign::add_assign(&mut a1, a2);
}
```

r[expr.compound-assign.result]
与赋值表达式一样，复合赋值表达式总是产生单元值 `()`。

> [!WARNING]
> 操作数的求值顺序会根据操作数的类型交换：
> 原生类型时先求值右边，非原生类型时先求值左边。
> 尽量不要写依赖复合赋值表达式操作数求值顺序的代码。
