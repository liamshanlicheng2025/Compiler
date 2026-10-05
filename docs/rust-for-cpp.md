# Rust ↔ C++ 语法对照表（编译器课程专用）

> 目的：你只需要**读懂** Rust 测试程序、会写简单的测试用例，不需要成为 Rust 程序员。
> 每条的结构：Rust 写法 → 对应的 C++ 概念 → 注意点。
> 打 ⚠️ 的小节是课程裁剪子集**大概率不涉及**的，扫一眼即可，不用深究。

---

## 1. 变量与类型

```rust
let x = 5;            // C++: const int x = 5;   —— 默认不可变！
let mut y = 5;        // C++: int y = 5;         —— mut 才可变
let z: i32 = 10;      // C++: int z = 10;        —— 类型写在冒号后
```

⚠️ 与 C++ 直觉相反：Rust 变量**默认不可变**，`mut` 是可变标记，语义接近"不写 const"。

基本类型对应：

| Rust | C++ | 说明 |
|------|-----|------|
| `i32` / `u32` | `int32_t` / `uint32_t` | 带固定位宽 |
| `isize` / `usize` | `ptrdiff_t` / `size_t` | 位宽随平台 |
| `bool` | `bool` | |
| `char` | 无直接对应 | 4 字节 Unicode 字符，不是 C++ 的 1 字节 `char` |
| `&str` / `String` | `std::string_view` / `std::string` | 字面量是 `&str` |
| `(i32, bool)` | `std::tuple<int, bool>` | 元组 |
| `[i32; 5]` | `int[5]` / `std::array<int,5>` | 定长数组 |
| `()` | `void` | 单元类型，"没有值"的值 |

## 2. 函数

```rust
fn add(a: i32, b: i32) -> i32 {   // C++: int add(int a, int b) {
    a + b                          // 注意：最后一行没有分号 = 返回值
}

fn nothing() { }                   // 返回 ()，相当于 void

fn early(x: i32) -> i32 {
    if x < 0 { return 0; }         // return 也可以显式用
    x
}
```

要点：
- 参数类型、返回类型都写在后面（`x: i32`、`-> i32`）。
- **函数体最后一个不带分号的表达式就是返回值**，这是 Rust 的特色（"一切皆是表达式"）。
- 没有函数重载。

## 3. 控制流

```rust
if x > 0 { ... } else if x < 0 { ... } else { ... }   // 条件不用括号，块必须有大括号

while x < 10 { x += 1; }              // 同 C++

loop {                                 // C++: while(true)
    if done { break; }                 // break / continue 同 C++
}

for i in 0..10 { }        // C++: for (int i = 0; i < 10; i++)
for i in 0..=10 { }       // 闭区间，含 10
```

特色：**if 是表达式**，可以直接赋值：

```rust
let y = if x > 0 { 1 } else { -1 };   // C++: int y = x > 0 ? 1 : -1;
```

## 4. struct 与 impl

```rust
struct Point {                 // 数据定义，类似 C++ struct
    x: i32,
    y: i32,
}

impl Point {                   // 成员函数单独写在 impl 块里！
    fn new(x: i32, y: i32) -> Point {     // 关联函数 ≈ C++ 静态成员函数
        Point { x, y }                     // 字段初始化简写：x 即 x: x
    }
    fn norm2(&self) -> i32 {               // &self ≈ C++ 的 const 成员函数
        self.x * self.x + self.y * self.y
    }
    fn shift(&mut self, dx: i32) {         // &mut self ≈ 非 const 成员函数
        self.x += dx;
    }
}

let mut p = Point::new(1, 2);  // :: 调用关联函数，类似 C++ 的 Point::create(...)
p.shift(3);                    // 方法调用语法同 C++
let n = p.norm2();
```

与 C++ 的关键差别：
- **没有继承**，没有虚函数、没有构造/析构函数概念。
- 数据（struct）和行为（impl）分离书写，但效果等价于 C++ 的 class。

## 5. enum 与 match

⚠️ **这是 Rust 与 C++ 差别最大的地方**，课程子集如果包含 enum 必考。

```rust
enum Shape {                    // 不是 C++ 的 enum！是"带标签的 union"
    Circle(f64),                // 每个变体可以携带数据
    Rect { w: f64, h: f64 },    // 也可以携带具名字段
    Empty,                      // 也可以什么都不带
}
// C++ 最接近的对应物：std::variant<double, RectData, std::monostate>
```

`match` 是增强版 switch：

```rust
match shape {
    Shape::Circle(r) => r * r * 3.14,        // => 分隔分支，不用 break
    Shape::Rect { w, h } => w * h,           // 可以解构出字段
    Shape::Empty => 0.0,                     // 必须覆盖所有变体，编译器强制
}
```

要点：
- 每个分支以 `=>` 引出，逗号分隔，**自动 break**。
- 必须**穷尽**所有情况（可用 `_ =>` 兜底）。
- match 也是表达式，可直接赋值。

## 6. 引用与借用

```rust
let x = 5;
let r = &x;        // C++: const int& r = x;    —— 不可变引用
let m = &mut y;    // C++: int& m = y;          —— 可变引用（y 必须 let mut）
*m = 10;           // 解引用用 *，同 C++ 指针
```

⚠️ 规则（编译器强制，课程子集可能弱化这部分检查）：
- 同一时刻，要么有多个 `&T`，要么只有一个 `&mut T`，不能混。
- 对写编译器来说：知道 `&` 取引用、`*` 解引用、函数参数里 `&self/&mut self` 的含义即可。

## 7. 常量与静态量

```rust
const MAX: i32 = 100;        // C++: constexpr int MAX = 100;  —— 编译期常量
static COUNT: i32 = 0;       // C++: 全局变量（带内部链接的全局静态）
```

## 8. 输出宏（测试程序里最常见）

```rust
println!("hello");              // 带换行输出
println!("x = {}", x);          // {} 是占位符，按顺序填充
println!("{a} + {b}", a=1, b=2);// 具名占位符（少见）
print!("no newline");           // 不换行
```

带 `!` 的是**宏调用**，不是函数。看到认识就行。课程测试通常用 `println!` 输出答案供评测机比对。

## 9. 注释与属性

```rust
// 行注释，同 C++
/* 块注释 */                    // 同 C++
/// 文档注释                     // 当作普通注释看待即可
#[derive(Debug)]                // 属性标注：给编译器的元信息，语义上可以先忽略
```

## 10. ⚠️ 课程子集大概率不涉及的内容

看到以下写法知道"这是什么"即可，你的编译器很可能不用支持：

| 特性 | 一句话解释 |
|------|-----------|
| `trait` / `impl Trait for T` | 接口/抽象约束，类似 C++ concepts |
| 泛型 `fn f<T>(x: T)` | 模板 |
| 生命周期标注 `&'a str` | 引用的有效期标注 |
| `async` / `await` | 异步编程 |
| `macro_rules!` | 自定义宏 |
| `unsafe` | 关闭安全检查的块 |
| 模块系统 `mod` / `use` | 多文件组织（课程测试通常单文件） |

---

## 学习建议（配合本表使用）

1. **打开 `grammar/RustParser.g4`，对照本表逐节找对应规则**——比如看第 4 节时去找 `structStruct`、`implementation`、`associatedItem` 规则，语法文件就是这门课的"考纲"。
2. **每学一节，写一个小测试程序**放进 `tests/`，用 `./build/compiler tests/xxx.rs` 看解析树，确认你写的代码语法合法、结构符合预期。
3. 遇到表里没覆盖的写法，查 `reference/RCompiler-Spec/` 里的对应章节（它是 mdbook 源码，`.md` 文件直接读就很舒服）。
