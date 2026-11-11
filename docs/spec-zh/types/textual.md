# 文本类型（Textual types）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/textual.md`

r[type.text.intro]
`char` 和 `str` 类型用于存放文本数据。

r[type.text.char-value]
`char` 类型的值是一个 Unicode 标量值（即不是代理项的码点），以 32 位无符号字表示，取值范围为 0x0000 到 0xD7FF，或 0xE000 到 0x10FFFF。

r[type.text.char-precondition]
创建超出此范围的 `char` 会立即产生未定义行为。`[char]` 实际上等价于长度为 1 的 UCS-4 / UTF-32 字符串。

r[type.text.str-value]
`str` 类型的值的表示方式与 `[u8]`（8 位无符号字节的切片）相同。但是 Rust 标准库对 `str` 有额外的假设：操作 `str` 的方法会假设并保证其中的数据是合法的 UTF-8。用非 UTF-8 的缓冲区调用 `str` 的方法，可能现在或将来导致未定义行为。

r[type.text.str-unsized]
由于 `str` 是动态大小类型（DST），它只能通过指针类型来实例化，例如 `&str`。`&str` 的内存布局与 `&[u8]` 相同。

r[type.text.layout]
## 布局与位有效性

r[type.layout.char-layout]
`char` 在所有平台上都保证与 `u32` 有相同的大小和对齐。

r[type.layout.char-validity]
`char` 的每个字节都保证是已初始化的（换句话说，`transmute::<char, [u8; size_of::<char>()]>(...)` 永远是安全的——但由于某些位模式不是合法的 `char`，反方向的转换并不总是安全的）。
