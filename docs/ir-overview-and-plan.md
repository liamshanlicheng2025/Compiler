# IR 阶段总览与实施计划

> 本文回答四个问题：IR 是什么、有什么用、我们的 IR 阶段要做什么、分几步做。
> LLVM 和 LLVM IR 的语法细节见姊妹篇 `llvm-ir-basics.md`。

## 1. IR 是什么

IR（Intermediate Representation，中间表示）是**介于 AST 和目标机器码之间的一层程序表示**。

为什么需要它？因为两头的"距离"太远了：

- **AST 太贴近语法**：表达式是嵌套的树（`a + b * c` 是三层节点），控制流是结构化
  的（if/while 套着块），还有大量隐式语义（自动解引用、类型转换、临时变量的生命周期）。
- **机器码太底层**：寄存器个数有限、有调用约定（参数放哪个寄存器）、有栈帧管理，
  直接从这里想"表达式树怎么翻译"会顾此失彼。

IR 就是中间的台阶：**线性的、类型化的、显式的指令序列**——表达式树被展平成
"先算 t1 = b*c，再算 t2 = a+t1"；控制流被打碎成"基本块 + 跳转"；隐式语义全部变成
显式指令。从 IR 到机器码的翻译（指令选择、寄存器分配）是成熟问题，**我们把它外包给
LLVM**（见下）。

## 2. 我们的 IR = LLVM IR 文本

课程测试脚本（`reference/RCompiler-Testcases/IR-1/test_llvm.bash`）规定了整条流水线：

```
源代码 test.rx
  │  ./compiler < test.rx > output.ll        ← 我们的编译器：输出 LLVM IR 文本
  ▼
output.ll
  │  clang -S --target=riscv32-unknown-elf   ← LLVM 的优化器 + RISC-V 后端（白嫖）
  ▼
output.s  +  builtin.s（builtin.c 编译来的运行时：printInt/getInt/...）
  │  ravel（RISC-V 模拟器）运行
  ▼
程序输出 + 退出码   ←→   与 test.out / 期望退出码比对
```

所以 IR 阶段我们的任务非常明确：**给每个源程序，生成语义等价的 LLVM IR 文本**。
之后 clang 负责优化（包括用 mem2reg 把我们朴素的 load/store 提升成高效寄存器形式）、
生成 RISC-V 汇编；ravel 模拟器负责跑。

IR-1 测试点就是语义阶段同一批程序（comprehensive、线段树合并这种），但判定标准变了：
不再只看"编译通过"，而是**真的跑起来，输出和退出码要对**。

## 3. IR 阶段的输入（语义阶段留给我们的家产）

| 资产 | 在哪 | IR 阶段的用途 |
|---|---|---|
| AST | builder 产物 | lowering 的遍历对象 |
| 符号表（符号/作用域/implMethods/implConsts） | CrateIndex | 函数签名、方法解析、struct 字段表 |
| **表达式类型旁表 exprTypes** | BodyChecker | 每个表达式的类型——选指令、算大小的依据 |
| 常量值（constValue） | ConstEvaluator | 数组长度、常量折叠 |

为什么当初要做 exprTypes 旁表，到这里揭晓：IR 生成时**每个表达式节点的类型随用随查**，
不用重跑类型推导。赵睿城分享里特别强调的一点也靠它：**i32/u32/isize/usize 在 LLVM
里都编码成 `i32`，但有符号性决定指令选择**——除法用 `sdiv` 还是 `udiv`、右移用
`ashr` 还是 `lshr`、比较用 `icmp sgt` 还是 `ugt`，都得查旁表里的源类型。

## 4. IR 阶段要做的七件事

1. **类型映射**：源类型 → LLVM 类型。四种整型 → `i32`；bool → `i8`（兼容 printf %d，
   避开 i1 的麻烦）；`()` → `void`；引用 → `ptr`；数组 `[T; N]` → `[N x T]`；
   struct → `%struct.X = type { ... }`（字段顺序即内存布局）。
2. **表达式 lowering**：递归遍历表达式树，每个节点产出一个"值"（虚拟寄存器 `%3`
   或常量），嵌套表达式自然展平成指令序列。二元运算、一元运算、as 转换（i32↔i32
   是空操作，i1/i8 要 zext/trunc）。
3. **变量与内存**：局部变量一律 `alloca` 在栈上，读写走 `load`/`store`——刻意不
   手写 SSA，提升寄存器的工作交给 clang 的 mem2reg 优化。let 遮蔽 = 新的 alloca。
4. **控制流**：if/while/loop → 基本块 + `br`；break/continue → 维护"循环出口/入口
   标签"栈；`&&`/`||` 要短路（翻译成条件跳转，不是两个都算完再 and）。
5. **函数**：`define`/`declare`、参数传递、`ret`；递归调用不用特殊处理（调用约定
   天然支持）；方法调用 = 普通函数调用，self 作为第一个实参。
6. **复合类型访问**：`getelementptr`（gep）算字段/元素地址，再 load/store。
   结构体字面量：alloca + 逐字段写入。数组重复 `[0; N]`：循环或 memset。
7. **内建与字符串**：`printlnInt/printInt/getInt/exit` 映射到 builtin.c 里的函数
   （`declare` 后直接 `call`）；字符串字面量 → 全局只读常量。

## 5. 设计决策

- **不建 IR 对象图，轻量 IRBuilder 直接拼文本**：一个字符串流 + 寄存器编号器 +
  标签编号器就够（Mx/RCompiler 的传统做法）。我们的 IR 只活一次——写出来交给 clang，
  不需要在自己手里做分析变换，建对象图是过度设计。
- **结构化输出**：每个函数先算好需要哪些局部槽，指令按基本块分段，标签命名可读
  （`if.then`/`while.cond`），方便肉眼 debug。
- **bool 用 i8 不用 i1**：LLVM 的 i1 在比较/存储/传参时处处要转换，i8 和 C 的
  char 一致，printf 兼容，省一堆 zext/trunc。

## 6. 里程碑计划

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0 工具链** | 安装 clang（含 RISC-V 后端）和 ravel 模拟器；手写一个 hello world `.ll` 走通 test_llvm.bash 全流程 | 手写 IR 跑出正确输出 |
| **M1 骨架** | `src/ir/` 目录：IRBuilder（拼文本）、类型映射表、main 接 `--emit-llvm` 开关 | 空 main 能生成合法 .ll |
| **M2 第一行输出** | 整型算术 + let + printlnInt/printInt/getInt | 简单算术程序跑对 |
| **M3 控制流** | if/else、while、loop、break/continue、return、短路逻辑 | 循环/分支程序跑对 |
| **M4 函数** | 函数定义/调用、参数、递归 | 递归（阶乘/斐波那契）跑对 |
| **M5 复合类型** | struct（字段/gep/字面量）、数组（下标/重复初始化）、引用（&/*）、方法调用 | struct/数组程序跑对 |
| **M6 全量回归** | IR-1 全套 + 对照 semantic 阶段结果 | IR-1 通过率达标 |

建议顺序死原则：**每个里程碑跑通再进下一个**，IR 调试比语义难得多（错误表现为
"运行结果不对"而不是"编译报错"），步子大了没法定位。

## 7. 风险与注意

- **环境缺口**：当前机器没有 clang 和 ravel，M0 是第一优先级；装好后把
  test_llvm.bash 的调用方式固化成我们自己的回归脚本。
- **调试方法**：IR 写错了 clang 会报解析错误（好事，能定位到行）；语义写错了表现
  为运行结果错（坏事）。所以每个里程碑要留几个"已知正确"的小程序和它们的预期输出。
- **builtin 函数名**：IR-1 的 builtin.c 用的是 `printInt/printlnInt/getInt` 这些旧名
  （semantic 阶段我们注册过）。 lowering 时源语言的内建调用统一映射到这组名字。
