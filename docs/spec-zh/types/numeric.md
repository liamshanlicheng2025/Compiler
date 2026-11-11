# 数值类型（Numeric types）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/numeric.md`

r[type.numeric.int]
## 整数类型

r[type.numeric.int.unsigned]
无符号整数类型包括：

| 类型  | 最小值 | 最大值 |
|-------|--------|-------------------|
| `u32` | 0      | 2<sup>32</sup>-1 |

r[type.numeric.int.signed]
有符号二进制补码整数类型包括：

| 类型  | 最小值            | 最大值 |
|-------|-------------------|-------------------|
| `i32` | -(2<sup>31</sup>) | 2<sup>31</sup>-1 |

r[type.numeric.int.size]
## 依赖机器的整数类型

r[type.numeric.int.size.usize]
`usize` 类型是无符号整数类型，位数与平台的指针类型相同。它可以表示进程中的每一个内存地址。

> [!NOTE]
> 虽然 `usize` 能表示每一个*地址*，但把*指针*转换为 `usize` 不一定是可逆的操作。
> 更多信息见类型转换表达式、`std::ptr` 的文档，尤其是其中关于 provenance（指针来源）的部分。

r[type.numeric.int.size.isize]
`isize` 类型是有符号整数类型，位数与平台的指针类型相同。对象和数组大小的理论上界是 `isize` 的最大值。这保证了 `isize` 可以用来计算指向同一对象或数组内部的指针之间的差值，并且可以寻址对象内的每个字节以及结尾之后再过一个字节的位置。

r[type.numeric.int.size.minimum]
`usize` 和 `isize` 至少为 16 位宽。

> [!NOTE]
> 许多 Rust 代码会假定指针、`usize`、`isize` 是 32 位或 64 位。因此 16 位指针的支持很有限，可能需要库显式地小心处理并声明支持。

r[type.numeric.validity]
## 位有效性

对每个数值类型 `T`，`T` 的位有效性与 `[u8; size_of::<T>()]` 的位有效性相同。未初始化的字节不是合法的 `u8`。
