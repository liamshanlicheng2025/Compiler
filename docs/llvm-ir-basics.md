# LLVM 与 LLVM IR 速成

> 本文回答：LLVM 是什么、我们用它白嫖什么、LLVM IR 的语法长什么样。
> IR 阶段的整体规划见 `ir-overview-and-plan.md`。

## 1. LLVM 是什么

LLVM 是一个**编译器基础设施**项目，可以理解成"编译器后端的标准件工厂"。
现代编译器普遍分三段：

```
前端（语言相关）          中端（语言无关）           后端（机器相关）
Rust/C/我们自己的语言  →  LLVM IR  + 优化器     →   x86/ARM/RISC-V 机器码
   我们写的部分  ←———  我们白嫖的部分  ———→
```

约定是：任何人发明新语言，只要把源代码翻译成 **LLVM IR**，LLVM 的优化器和
几十个后端就全部免费可用。我们的课程设计正是这样：**我们输出 LLVM IR 文本，
clang（LLVM 的官方驱动）负责优化并生成 RISC-V 汇编**。寄存器分配、指令选择、
窥孔优化这些最难的活，全部外包。

LLVM IR 有三种形态：文本（`.ll`，人可读）、二进制位码（`.bc`）、内存对象。
我们生成的是 `.ll` 文本——可以肉眼检查，调试友好。

## 2. LLVM IR 的世界观：SSA 与虚拟寄存器

看一个完整的最小例子：

```llvm
define i32 @add_one(i32 %x) {       ; 定义函数：返回 i32，名字 add_one，一个 i32 参数
entry:
  %1 = add i32 %x, 1                ; 虚拟寄存器 %1 = %x + 1
  ret i32 %1                        ; 返回
}
```

核心规则：

- **虚拟寄存器无限多**：`%1`、`%2`、`%x`、`%sum` 随便用，不用管物理寄存器只有
  十几个——映射到物理寄存器是后端的事。
- **SSA（静态单赋值）**：每个虚拟寄存器**只能被赋值一次**。`%1 = add ...` 之后
  `%1` 就定死了。变量"可反复赋值"的概念在 IR 指令层面**不存在**。

那 `let mut x = 1; x = x + 1;` 怎么办？两种办法：

- **alloca 绕开（我们采用）**：变量不住寄存器，住内存。`alloca` 在栈上开一块
  空间，读用 `load`、写用 `store`，随便改。这是 clang 在 -O0 时的做法，简单
  可靠；clang 的 mem2reg 优化会再把它们提升回寄存器。
- 手写 phi 指令（进阶，不做）。

## 3. 类型系统

| LLVM 类型 | 含义 | 我们的映射 |
|---|---|---|
| `i32` `i8` `i1` | 任意位宽整数 | 四种整型 → `i32`；bool → `i8` |
| `ptr` | 不透明指针（LLVM 15+，不区分指向什么） | 引用 `&T`、数组/结构体的地址 |
| `[N x T]` | 定长数组 | `[T; N]` → `[N x T]` |
| `%struct.X = type { i32, ptr }` | 结构体（字段按序布局） | struct 定义 |
| `void` | 无返回 | `()` 返回值 |

**重要**：i32 不区分有符号/无符号。有符号性体现在**指令**上（下节的
sdiv/udiv、ashr/lshr、icmp 的谓词），这正是 exprTypes 旁表要保留源类型的原因。

## 4. 算术、比较、布尔

```llvm
%3 = add i32 %1, %2        ; 加减乘：add sub mul（有/无符号通用，溢出行为一致）
%4 = sdiv i32 %1, %2       ; 有符号除法 sdiv；无符号用 udiv
%5 = srem i32 %1, %2       ; 有符号取余 srem；无符号用 urem
%6 = ashr i32 %1, %2       ; 算术右移（补符号位，有符号用）；逻辑右移 lshr；左移 shl
%7 = and i32 %1, %2        ; 位运算 and or xor
%c = icmp sgt i32 %1, %2   ; 比较，结果是 i1。谓词：eq ne | sgt sge slt sle（有符号）
                           ;                          | ugt uge ult ule（无符号）
```

## 5. 内存三件套 + getelementptr

```llvm
%px = alloca i32                    ; 栈上开一个 i32 的槽，%px 是它的地址（ptr）
store i32 42, ptr %px               ; 写：*px = 42
%v = load i32, ptr %px              ; 读：%v = *px
```

**getelementptr（gep）**是地址计算器，字段访问和数组下标全靠它：

```llvm
; struct Point { x: i32, y: i32 } → %struct.Point = type { i32, i32 }
%fy = getelementptr %struct.Point, ptr %p, i32 0, i32 1
;                                    ↑结构体类型   ↑基址  ↑第0个结构体 ↑第1个字段（y）

; arr: [10 x i32]，算 arr[i] 的地址：
%ei = getelementptr [10 x i32], ptr %arr, i32 0, i32 %i
;                                        ↑基址  ↑不步进  ↑第 i 个元素
```

两个常踩的坑：

1. gep **不做边界检查**，它纯算地址（基址 + 偏移），安不安全是语义阶段管的事。
2. 第一个索引是"步过几个完整的结构体/数组"，第二个索引才是字段/元素编号——
   照上面的模板写，别想当然只写一个索引。

## 6. 控制流：基本块 + 跳转

IR 里没有 if/while，只有**基本块**（一段顺序执行的指令）和块尾的**终结指令**
（`br` 跳转或 `ret` 返回）。每个基本块以标签开头：

```llvm
; if (a > 2) { b = 1; } else { b = 2; }
  %c = icmp sgt i32 %a, 2
  br i1 %c, label %if.then, label %if.else   ; 条件跳转
if.then:
  store i32 1, ptr %pb
  br label %if.end                            ; 无条件跳转
if.else:
  store i32 2, ptr %pb
  br label %if.end
if.end:
  ; 后续代码……
```

循环就是跳回头的块：`while.cond` 块判断条件，真则进 `while.body`，body 末尾
跳回 `while.cond`；**break = 跳到循环出口块，continue = 跳回条件块**——lowering
时维护一个标签栈即可。

`&&`/`||` 的短路也是条件跳转：`a && b` 是"算 a，为假直接得 false，为真才算 b"，
绝不能两个都算完再 `and`（否则 `p != null && *p > 0` 这种就崩了）。

## 7. 函数调用与外部声明

```llvm
declare void @printlnInt(i32)       ; 外部函数声明（builtin.c 提供）

define i32 @main() {
entry:
  %y = call i32 @add_one(i32 41)    ; 调用：返回值进 %y
  call void @printlnInt(i32 %y)
  ret i32 0
}
```

方法调用 `p.norm2()` 就是普通调用 `call i32 @Point.norm2(ptr %p)`——
self 作为第一个实参传指针，这是我们在 AST 里存 `isMethod` 时就埋好的设计。

## 8. 字符串与全局常量

```llvm
@.str.0 = private unnamed_addr constant [6 x i8] c"hello\00"
; 使用时取首字符指针：
%ps = getelementptr [6 x i8], ptr @.str.0, i32 0, i32 0
```

全局 `static` 变量对应 `@g = global i32 0`（mutable）或 `constant`（只读）。

## 9. 贯穿示例：一段源程序的完整翻译

源程序（就是赵睿城分享里那个贯穿示例）：

```rust
fn add_one(x: i32) -> i32 { x + 1 }
fn main() {
    let n: i32 = 41;
    let y: i32 = add_one(n);
    println_i32(y);          // 映射到 builtin 的 printlnInt
}
```

对应的完整 `.ll`（alloca 风格，clang -O0 同款）：

```llvm
declare void @printlnInt(i32)

define i32 @add_one(i32 %x) {
entry:
  %px = alloca i32
  store i32 %x, ptr %px              ; 参数也先落槽，统一按变量处理
  %1 = load i32, ptr %px
  %2 = add i32 %1, 1
  ret i32 %2
}

define i32 @main() {
entry:
  %pn = alloca i32
  store i32 41, ptr %pn              ; let n = 41
  %3 = load i32, ptr %pn
  %4 = call i32 @add_one(i32 %3)     ; add_one(n)
  %py = alloca i32
  store i32 %4, ptr %py              ; let y = ...
  %5 = load i32, ptr %py
  call void @printlnInt(i32 %5)
  ret i32 0
}
```

肉眼可见的"啰嗦"（load/store 满天飞）完全不用担心——clang 的 mem2reg
优化会把它们全部消掉，这正是我们敢用 alloca 风格的底气。

## 10. 子集 → LLVM IR 速查表

| 源语言 | LLVM IR |
|---|---|
| i32/u32/isize/usize | `i32`（有符号性看指令） |
| bool | `i8` |
| `()` | `void` |
| `&T` / `&mut T` | `ptr` |
| `[T; N]` | `[N x T]` |
| struct | `%struct.X = type {...}` + gep 取字段 |
| `a + b` | `add i32`（u32 也一样） |
| `a / b` `a % b` | i32→`sdiv`/`srem`，u32/usize→`udiv`/`urem` |
| `a >> b` | i32→`ashr`，u32/usize→`lshr` |
| `a < b` 等比较 | `icmp s*`（无符号用 `u*`），结果 i1 |
| `if` | 条件 br + then/else/end 块 |
| `while`/`loop` | cond/body/end 块，break/continue 跳标签 |
| `let x = v; x = w;` | alloca + store/load |
| 函数调用/方法调用 | `call`（方法 self 作首参） |
| `return v` | `ret` |
| printlnInt 等内建 | `declare` + `call`（builtin.c 提供） |
| 字符串字面量 | 全局 `constant [N x i8]` + gep 取址 |
| `x as T` | 整型间是空操作（都是 i32）；涉 i1/i8 才 zext/trunc |
