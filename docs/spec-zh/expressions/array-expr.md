# 数组与数组下标表达式（Array and array index expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/array-expr.md`

## 数组表达式

r[expr.array.syntax]
```grammar,expressions
ArrayExpression -> `[` ArrayElements? `]`

ArrayElements ->
      Expression ( `,` Expression )* `,`?
    | Expression `;` Expression
```

r[expr.array.constructor]
*数组表达式*用于构造数组。数组表达式有两种形式。

r[expr.array.array]
第一种形式列出数组中的每一个值。

r[expr.array.array-syntax]
这种形式的语法是：方括号内一个逗号分隔的表达式列表，所有表达式类型一致。

r[expr.array.array-behavior]
它产生一个数组，按书写顺序包含这些值。

r[expr.array.repeat]
第二种形式的语法是：方括号内两个用分号（`;`）分隔的表达式。

r[expr.array.repeat-operand]
`;` 之前的表达式称为*重复操作数*（repeat operand）。

r[expr.array.length-operand]
`;` 之后的表达式称为*长度操作数*（length operand）。

r[expr.array.length-restriction]
长度操作数必须是 `usize` 类型的常量表达式（例如字面量或常量条目）。

```rust
const C: usize = 1;
let _: [u8; C] = [0; 1]; // 字面量。
let _: [u8; C] = [0; C]; // 常量条目。
let _: [u8; C] = [0; _]; // 推断常量。
let _: [u8; C] = [0; (((_)))]; // 推断常量。
```

r[expr.array.repeat-behavior]
这种形式的数组表达式创建一个数组，长度为长度操作数的值，每个元素都是重复操作数的一份拷贝。也就是说，`[a; b]` 创建一个包含 `b` 份 `a` 的值的数组。

r[expr.array.repeat-copy]
如果长度操作数的值大于 1，则要求重复操作数的类型实现 `Copy`，或者重复操作数是常量表达式，或者是指向常量条目的路径。

r[expr.array.repeat-const-item]
当重复操作数是常量表达式或指向常量条目的路径时，它会被求值长度操作数指定的次数。

r[expr.array.repeat-evaluation-zero]
如果该值是 `0`，则常量块或常量条目根本不会被求值。

r[expr.array.repeat-non-const]
对既不是常量块也不是指向常量条目路径的表达式，它恰好被求值一次，然后结果被复制长度操作数的值那么多次。

```rust
[1, 2, 3, 4];
["a", "b", "c", "d"];
[0; 128];              // 含 128 个 0 的数组
[0u8, 0u8, 0u8, 0u8,];
[[1, 0, 0], [0, 1, 0], [0, 0, 1]]; // 二维数组
const EMPTY: Vec<i32> = Vec::new();
[EMPTY; 2];
```

r[expr.array.index]
## 数组与切片下标表达式

r[expr.array.index.syntax]
```grammar,expressions
IndexExpression -> Expression `[` Expression `]`
```

r[expr.array.index.array]
数组和切片类型的值可以通过在其后书写一个方括号括起的 `usize` 类型表达式（即下标）来索引。当数组是可变的，得到的内存位置可以被赋值。

r[expr.array.index.trait]
对其他类型，下标表达式 `a[b]` 等价于 `*std::ops::Index::index(&a, b)`；在可变位置表达式上下文中则等价于 `*std::ops::IndexMut::index_mut(&mut a, b)`。与方法一样，Rust 也会反复对 `a` 插入解引用操作来寻找实现。

r[expr.array.index.zero-index]
数组和切片的下标从零开始。

r[expr.array.index.const]
如果数组访问是常量表达式，可以在编译期用常量下标值检查边界。

r[expr.array.index.non-const]
否则，会在运行期做检查，检查失败会让线程进入 panic 状态。

```rust,should_panic
// lint 默认为 deny。
#![warn(unconditional_panic)]

([1, 2, 3, 4])[2];        // 求值为 3

let b = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
b[1][2];                  // 多维数组下标

let x = (["a", "b"])[10]; // 警告：下标越界

let n = 10;
let y = (["a", "b"])[n];  // 会 panic

let arr = ["a", "b"];
arr[10];                  // 警告：下标越界
```

r[expr.array.index.trait-impl]
通过实现 `Index` 和 `IndexMut` trait，可以为数组和切片以外的类型实现下标表达式。
