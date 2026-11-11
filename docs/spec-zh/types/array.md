# 数组类型（Array types）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/array.md`

r[type.array.syntax]
```grammar,types
ArrayType -> `[` Type `;` Expression `]`
```

r[type.array.intro]
数组是固定大小的序列，包含 `N` 个类型为 `T` 的元素。数组类型写作 `[T; N]`。

r[type.array.constraint]
大小必须是一个常量表达式，求值结果为 `usize`。

例子：

```rust
// 栈上分配的数组
let array: [i32; 3] = [1, 2, 3];

// 堆上分配的数组，coerce（强制转换）为切片
let boxed_array: Box<[i32]> = Box::new([1, 2, 3]);
```

r[type.array.index]
数组的所有元素总是已初始化的，并且在安全的方法和运算符中，对数组的访问总是做边界检查。

> [!NOTE]
> 标准库的 `Vec<T>` 类型提供了堆上分配的可变长数组类型。
