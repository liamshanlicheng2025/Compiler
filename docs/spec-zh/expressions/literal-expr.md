# 字面量表达式（Literal expressions）

> 中文译文，原文：`reference/RCompiler-Spec/src/expressions/literal-expr.md`

r[expr.literal.syntax]
```grammar,expressions
LiteralExpression ->
      CHAR_LITERAL
    | STRING_LITERAL
    | RAW_STRING_LITERAL
    | C_STRING_LITERAL
    | RAW_C_STRING_LITERAL
    | INTEGER_LITERAL
    | `true`
    | `false`
```

r[expr.literal.intro]
*字面量表达式*是由单个 token（而不是一串 token）构成的表达式，它直接表示它求值得到的值，而不是通过名字或其他求值规则来引用那个值。

r[expr.literal.const-expr]
字面量是常量表达式的一种形式，因此（主要）在编译期求值。

r[expr.literal.literal-token]
前面词法部分描述的每种字面量 token 形式都可以构成一个字面量表达式，关键字 `true` 和 `false` 也可以。

```rust
"hello";   // 字符串类型
'5';       // 字符类型
5;         // 整数类型
```

r[expr.literal.string-representation]
在下文的描述中，一个 token 的*字符串表示*（string representation）是输入中匹配该 token 词法规则的那一串字符。

> [!NOTE]
> 这个字符串表示中永远不会出现紧跟 `U+000A`（LF）的 `U+000D`（CR）：这样的字符对之前已经被转换成单个 `U+000A`（LF）。

r[expr.literal.escape]
## 转义（Escapes）

r[expr.literal.escape.intro]
下文对文本类字面量表达式的描述会用到几种*转义*形式。

r[expr.literal.escape.sequence]
每种转义形式由两部分刻画：
 * 一个*转义序列*（escape sequence）：一串字符，总是以 `U+005C`（`\`）开头
 * 一个*转义值*（escaped value）：单个字符，或者空的字符序列

在下文的转义定义中：
 * *八进制数字*指 [`0`-`7`] 范围内的任意字符。
 * *十六进制数字*指 [`0`-`9`]、[`a`-`f`] 或 [`A`-`F`] 范围内的任意字符。

r[expr.literal.escape.simple]
### 简单转义（Simple escapes）

下表第一列中的每一串字符都是一个转义序列。每种情况下，转义值都是第二列对应条目给出的字符。

| 转义序列 | 转义值                    |
|---------|--------------------------|
| `\0`    | U+0000 (NUL)             |
| `\t`    | U+0009 (HT)              |
| `\n`    | U+000A (LF)              |
| `\r`    | U+000D (CR)              |
| `\"`    | U+0022 (QUOTATION MARK)  |
| `\'`    | U+0027 (APOSTROPHE)      |
| `\\`    | U+005C (REVERSE SOLIDUS) |

r[expr.literal.escape.hex-octet]
### 8 位转义（8-bit escapes）

转义序列由 `\x` 加上两个十六进制数字组成。

转义值是这样一个字符：把转义序列的最后两个字符按十六进制整数解释（相当于用基数 16 调用 `u8::from_str_radix`），得到的 Unicode 标量值对应的字符。

> [!NOTE]
> 因此转义值的 Unicode 标量值在 `u8` 的范围内。

r[expr.literal.escape.hex-ascii]
### 7 位转义（7-bit escapes）

转义序列由 `\x` 加上一个八进制数字、再加一个十六进制数字组成。

转义值是这样一个字符：把转义序列的最后两个字符按十六进制整数解释（相当于用基数 16 调用 `u8::from_str_radix`），得到的 Unicode 标量值对应的字符。

r[expr.literal.escape.unicode]
### Unicode 转义

转义序列由 `\u{` 加上一串字符（每个都是十六进制数字或 `_`）、再加上 `}` 组成。

转义值是这样一个字符：把转义序列中包含的十六进制数字按十六进制整数解释（相当于用基数 16 调用 `u32::from_str_radix`），得到的 Unicode 标量值对应的字符。

> [!NOTE]
> [CHAR_LITERAL] 或 [STRING_LITERAL] token 允许的形式保证了一定存在这样的字符。

r[expr.literal.continuation]
### 字符串续行转义（String continuation escapes）

转义序列由 `\` 紧跟一个 `U+000A`（LF），再加上下一个非空白字符之前的所有空白字符组成。这里的空白字符指 `U+0009`（HT）、`U+000A`（LF）、`U+000D`（CR）和 `U+0020`（空格）。

转义值是空字符序列。

> [!NOTE]
> 这种转义的效果是：字符串续行会跳过其后的空白，包括额外的换行。因此下面 `a`、`b`、`c` 相等：
>
> ```rust
> let a = "foobar";
> let b = "foo\
>          bar";
> let c = "foo\
>
>      bar";
>
> assert_eq!(a, b);
> assert_eq!(b, c);
> ```
>
> 跳过额外的换行（如例子 c）可能引起困惑、出乎预料。这个行为未来可能会调整。在做出决定之前，建议避免依赖用续行跳过多个换行的行为。

r[expr.literal.char]
## 字符字面量表达式

r[expr.literal.char.intro]
字符字面量表达式由单个 [CHAR_LITERAL] token 组成。

r[expr.literal.char.type]
表达式的类型是原生类型 `char`。

r[expr.literal.char.no-suffix]
token 不能带后缀。

r[expr.literal.char.literal-content]
token 的*字面量内容*（literal content）是 token 字符串表示中第一个 `U+0027`（`'`）之后、最后一个 `U+0027`（`'`）之前的那串字符。

r[expr.literal.char.represented]
字面量表达式的*被表示字符*（represented character）由字面量内容按如下方式得出：

r[expr.literal.char.escape]
* 如果字面量内容是以下形式的转义序列之一，被表示字符就是该转义序列的转义值：
    * 简单转义
    * 7 位转义
    * Unicode 转义

r[expr.literal.char.single]
* 否则，被表示字符就是构成字面量内容的那个单个字符。

r[expr.literal.char.result]
表达式的值是被表示字符的 Unicode 标量值对应的 `char`。

> [!NOTE]
> [CHAR_LITERAL] token 允许的形式保证了这些规则总是产生单个字符。

字符字面量表达式的例子：

```rust
'R';                               // R
'\'';                              // '
'\x52';                            // R
'\u{00E6}';                        // 拉丁小写字母 AE (U+00E6)
```

r[expr.literal.string]
## 字符串字面量表达式

r[expr.literal.string.intro]
字符串字面量表达式由单个 [STRING_LITERAL] 或 [RAW_STRING_LITERAL] token 组成。

r[expr.literal.string.type]
表达式的类型是指向原生类型 `str` 的共享引用（生命周期为 `static`），即类型为 `&'static str`。

r[expr.literal.string.no-suffix]
token 不能带后缀。

r[expr.literal.string.literal-content]
token 的*字面量内容*是 token 字符串表示中第一个 `U+0022`（`"`）之后、最后一个 `U+0022`（`"`）之前的那串字符。

r[expr.literal.string.represented]
字面量表达式的*被表示字符串*是由字面量内容按如下方式得出的字符序列：

r[expr.literal.string.escape]
* 如果 token 是 [STRING_LITERAL]，字面量内容中出现的以下形式的每个转义序列都被替换成它的转义值：
    * 简单转义
    * 7 位转义
    * Unicode 转义
    * 字符串续行转义

  这些替换按从左到右的顺序进行。例如，token `"\\x41"` 被转换成字符 `\` `x` `4` `1`。

r[expr.literal.string.raw]
* 如果 token 是 [RAW_STRING_LITERAL]，被表示字符串与字面量内容完全相同。

r[expr.literal.string.result]
表达式的值是一个引用，指向一块静态分配的 `str`，其中存放被表示字符串的 UTF-8 编码。

字符串字面量表达式的例子：

```rust
"foo"; r"foo";                     // foo
"\"foo\""; r#""foo""#;             // "foo"

"foo #\"# bar";
r##"foo #"# bar"##;                // foo #"# bar

"\x52"; "R"; r"R";                 // R
"\\x52"; r"\x52";                  // \x52
```

r[expr.literal.c-string]
## C 字符串字面量表达式

r[expr.literal.c-string.intro]
C 字符串字面量表达式由单个 [C_STRING_LITERAL] 或 [RAW_C_STRING_LITERAL] token 组成。

r[expr.literal.c-string.type]
表达式的类型是指向标准库 [CStr] 类型的共享引用（生命周期为 `static`），即类型为 `&'static core::ffi::CStr`。

r[expr.literal.c-string.no-suffix]
token 不能带后缀。

r[expr.literal.c-string.literal-content]
token 的*字面量内容*是 token 字符串表示中第一个 `"` 之后、最后一个 `"` 之前的那串字符。

r[expr.literal.c-string.represented]
字面量表达式的*被表示字节序列*是由字面量内容按如下方式得出的字节序列：

r[expr.literal.c-string.escape]
* 如果 token 是 [C_STRING_LITERAL]，字面量内容被看作一串条目，每个条目要么是一个非 `\` 的单个 Unicode 字符，要么是一个转义。这串条目按如下方式转换成字节序列：
  * 每个单个 Unicode 字符贡献它的 UTF-8 表示。
  * 每个简单转义贡献它转义值的 Unicode 标量值。
  * 每个 8 位转义贡献一个字节，内容是它转义值的 Unicode 标量值。
  * 每个 Unicode 转义贡献它转义值的 UTF-8 表示。
  * 每个字符串续行转义不贡献任何字节。

r[expr.literal.c-string.raw]
* 如果 token 是 [RAW_C_STRING_LITERAL]，被表示字节是字面量内容的 UTF-8 编码。

> [!NOTE]
> [C_STRING_LITERAL] 和 [RAW_C_STRING_LITERAL] token 允许的形式保证了被表示字节中永远不会包含空字节。

r[expr.literal.c-string.result]
表达式的值是一个引用，指向一块静态分配的 [CStr]，其字节数组包含被表示字节，后跟一个空字节。

C 字符串字面量表达式的例子：

```rust
c"foo"; cr"foo";                     // foo
c"\"foo\""; cr#""foo""#;             // "foo"

c"foo #\"# bar";
cr##"foo #"# bar"##;                 // foo #"# bar

c"\x52"; c"R"; cr"R";                // R
c"\\x52"; cr"\x52";                  // \x52

c"æ";                                // 拉丁小写字母 AE (U+00E6)
c"\u{00E6}";                         // 拉丁小写字母 AE (U+00E6)
c"\xC3\xA6";                         // 拉丁小写字母 AE (U+00E6)

c"\xE6".to_bytes();                  // [230]
c"\u{00E6}".to_bytes();              // [195, 166]
```

r[expr.literal.int]
## 整数字面量表达式

r[expr.literal.int.intro]
整数字面量表达式由单个 [INTEGER_LITERAL] token 组成。

r[expr.literal.int.suffix]
如果 token 带后缀，后缀必须是原生整数类型之一的名字：`u32`、`i32`、`usize` 或 `isize`，表达式即具有该类型。

r[expr.literal.int.infer]
如果 token 没有后缀，表达式的类型由类型推断决定：

r[expr.literal.int.inference-unique-type]
* 如果能从周围的程序上下文中*唯一地*确定一个整数类型，表达式就具有该类型。

r[expr.literal.int.inference-default]
* 如果程序上下文对类型的约束不足，默认取有符号 32 位整数 `i32`。

r[expr.literal.int.inference-error]
* 如果程序上下文对类型的约束过度（互相矛盾），则视为静态类型错误。

整数字面量表达式的例子：

```rust
123;                               // 类型 i32
123i32;                            // 类型 i32
123u32;                            // 类型 u32
123_u32;                           // 类型 u32
let a: u64 = 123;                  // 类型 u64

0xff;                              // 类型 i32
0xff_u8;                           // 类型 u8

0o70;                              // 类型 i32
0o70_i16;                          // 类型 i16

0b1111_1111_1001_0000;             // 类型 i32
0b1111_1111_1001_0000i64;          // 类型 i64

0usize;                            // 类型 usize
```

r[expr.literal.int.representation]
表达式的值由 token 的字符串表示按如下方式确定：

r[expr.literal.int.radix]
* 检查字符串的前两个字符来选择整数进制：

    * `0b` 表示 2 进制
    * `0o` 表示 8 进制
    * `0x` 表示 16 进制
    * 否则为 10 进制。

r[expr.literal.int.radix-prefix-stripped]
* 如果进制不是 10，从字符串中去掉前两个字符。

r[expr.literal.int.type-suffix-stripped]
* 从字符串中去掉所有后缀。

r[expr.literal.int.separators-stripped]
* 从字符串中去掉所有下划线。

r[expr.literal.int.u128-value]
* 按选定的进制把字符串转换为 `u128` 值（相当于调用 `u128::from_str_radix`）。如果值放不进 `u128`，是编译错误。

r[expr.literal.int.cast]
* 通过数值转换（numeric cast）把 `u128` 值转换为表达式的类型。

> [!NOTE]
> 如果字面量的值放不进表达式的类型，最后的转换会截断它。`rustc` 有一个名为 `overflowing_literals` 的 lint 检查（默认为 deny），会拒绝出现这种情况的表达式。

> [!NOTE]
> 例如 `-1i8` 是取负运算符作用于字面量表达式 `1i8`，而不是一个单独的整数字面量表达式。关于如何表示有符号类型的最小值，见运算符表达式一章中关于溢出（Overflow）的说明。

r[expr.literal.bool]
## 布尔字面量表达式

r[expr.literal.bool.intro]
布尔字面量表达式由关键字 `true` 或 `false` 之一组成。

r[expr.literal.bool.result]
表达式的类型是原生布尔类型，其值为：
 * 关键字是 `true` 时为 true
 * 关键字是 `false` 时为 false
