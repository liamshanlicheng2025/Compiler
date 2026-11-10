# Semantic 阶段小测复习：SPEC 语义部分详细整理

> 来源：`reference/RCompiler-Spec/src/`（课程版 Rust Reference）。
> 本文覆盖：类型系统、语句与块、if/循环、运算符、**类型 coercion 与 LUB**。
> 每节末尾标注与我们编译器实现的对应关系（`BodyChecker`/`CrateIndex`）。

---

## 1. 类型系统总览（types.md）

**类型清单**（小测要能默写）：
- 基本类型：`bool`、整型（`i32`/`u32`/`isize`/`usize`）、`char`、`str`
- 序列类型：数组 `[T; N]`、切片（课程不深入）
- 用户类型：struct、enum
- 指针类型：引用 `&T` / `&mut T`
- unit 类型 `()`

**类型表达式语法**（能写在类型标注位置的语法）：
```
TypeNoBounds -> TypePath | ReferenceType | ArrayType | UnitType
```

**递归类型**的两条限制：
1. 递归必须经过 nominal 类型（struct），不能是纯结构类型（数组）自递归
2. 递归类型尺寸必须有限——递归字段必须是指针类型（引用/Box）
（对应我们的 `LayoutChecker` 查环，环必须被引用/Box 打断）

## 2. 语句（statements.md）

四种语句形态：
```
Statement -> `;` | Item | LetStatement | ExpressionStatement
```

- **let 语句**：课程简化版要求**必须显式类型标注、必须有初值**
  （`let PatternNoTopAlt : Type = Expression ;`）
- **表达式语句**的分号规则（**高频考点**）：
  ```
  ExpressionStatement -> ExpressionWithoutBlock `;` | ExpressionWithBlock `;`?
  ```
  - 不带块表达式作语句：**必须**带分号
  - 带块表达式（if/while/loop/裸块）作语句：分号**可省略**
  - **省略分号时，该表达式的类型必须是 `()`**（`{ 1 }` 作非末尾语句要报错）
  - 歧义消解：带块表达式既能当语句也能当表达式时，**按语句解析**

## 3. 块表达式（block-expr.md）

- 结构：`{ Statement* 尾表达式? }`
- 求值：按序执行每条语句，最后求尾表达式（final operand）
- **块的类型 = 尾表达式的类型；无尾表达式 = `()`**
- 块总是**值表达式**（value expression）
- 块作为语句的外层表达式时，除非紧跟分号，期望类型是 `()`

对应实现：`BodyChecker::visit(Block)`（尾表达式→类型、发散传播、无分号语句 unit 检查）。

## 4. if 表达式（if-expr.md）

```
IfExpression -> `if` Conditions BlockExpression (`else` (BlockExpression | IfExpression))?
Conditions -> `(` Expression _except StructExpression_ `)`
```

- **条件必须带括号**（课程 spec 语法如此；括号内不允许结构体字面量）
- 条件必须是 **bool**；支持 `&&` 链；**短路求值必须支持**
- **if 表达式的值 = 被执行的那个块的值**；没有任何块执行时为 `()`
- **"if 表达式在所有情形下类型必须一致"**——then/else if/else 各臂的类型
  通过 **LUB** 合并（见 §8）

对应实现：`visit(IfExpr)`（condGrouped 检查、bool 检查、`lub(thenT, elseT)` 合并、
无 else 时为 `()` 且 then 块必须 `()`/Never）。

## 5. 循环（loop-expr.md）

- 两种：`loop { }` 无限循环、`while (条件) { }` 谓词循环
- **`loop` 不发散的判定**：不带 break 的 `loop` 是发散的，类型 `!`；
  带 break 的 loop 不发散，**类型必须兼容每个 break 表达式的值的类型**
  （多个 break 值 → LUB 合并）
- **`while` 的值恒为 `()`**；**只有 `loop` 支持带值 break**
- `break`/`continue` 绑定**最内层**循环；只允许出现在循环体内
- `break` 不带表达式 ≡ `break ()`
- 条件同 if：带括号、bool、短路

对应实现：`visit(LoopExpr)`（`hasAnyBreak_`/`loopBreakType_` + lub 合并）、
`visit(WhileExpr)`（值为 `()`、拒绝带值 break、条件内 `loopDepth_` 归零）。

## 6. 运算符表达式（operator-expr.md）

### 借用 `&` / `&mut`
- `&&` 虽是单个 token（惰性 and 运算符），但在借用语境下**按两个借用解析**：
  `&&10` ≡ `&&10`、`&&&&mut 10` ≡ `& & & & mut 10`
- `&`/`&mut` 作用于**值表达式**时会创建**临时值**
- `&mut` 在"可变位置语境"求值其操作数

### 解引用 `*`
- 类型为 `&mut T` 且是可变位置时，结果位置**可赋值**
- 非指针类型上 `*x` ≡ Deref 调用（课程里就是 Box 解引用）

### 一元 `-` / `!`
| 符号 | 整型 | bool |
|---|---|---|
| `-` | 取负（**仅有符号**） | — |
| `!` | 按位取反 | 逻辑取反 |

- 溢出规则：debug 下 panic；对最负值取负是溢出，**除非操作数是字面量**
  （`-2147483648i32` 合法——对应我们的 `allowI32MinLit_`）

### 算术/位运算（背表）
`+ - * / % & | ^ << >>`；整数除法**向零取整**；`%` 是被除数符号；有符号右移是
**算术**右移；bool 的 `&` `|` `^` 是（非短路）逻辑运算；整数除零 panic。

### 比较运算
- 操作数在 **place expression 语境**求值（`a == b` ≡ `PartialEq::eq(&a, &b)`，
  不发生移动）
- **不允许链式比较**：`a == b == c` 非法，必须加括号

### 惰性布尔 `&&` `||`
- 两边必须 bool；**右侧仅在左侧不能决定结果时求值**（短路）

### 类型转换 `as`
- 能显式执行所有 coercion，另有数值互转、bool/char → 整型等表项；
  不在表内的转换是编译错误

### 赋值
- **先求值右操作数，再求值左操作数**（与其他表达式相反！）
- **赋值表达式的值恒为 `()`**（与 C/C++ 不同）
- 复合赋值同理

对应实现：`visit(BinaryExpr)`（五类 op 分流、checkAssignable）、
`visit(UnaryExpr)`（符号整型限定、`-` 下字面量特例）。

## 7. 类型 coercion（type-coercions.md，LUB 的前置）

**定义**：coercion 是隐式的类型转换，**只能发生在特定的 coercion site**，
且只有特定类型对之间允许。

### Coercion site（哪里会发生隐式转换）
1. 带显式类型标注的 **let**（`let _: &i8 = &mut 42;`）
2. **static/const** 声明
3. **函数实参**（实参 → 形参类型）
4. **结构体/枚举字段的实例化**（`Foo { x: &mut 42 }`）
5. **函数返回值**（块尾表达式或 `return` 的值）

### 传播位置（propagating expressions）
上述 site 里的表达式若是"传播型"，其子表达式也是 coercion site：
- 数组字面量 `[a, b, c]`：每个元素向 `U` 转换
- 数组重复形式 `[v; n]`：重复子表达式向 `U` 转换
- 元组、括号 `(e)`、**块**（块有已知类型 `U` 时，其尾表达式是 site）

### 允许的转换（coercion types）
- 子类型（自反）、传递（T1→T2→T3）
- **`&mut T` → `&T`**（最常用！）
- 指针退化：`&T → *const T`、`&mut T → *mut T`、`*mut T → *const T`
- **Deref coercion**：`&T → &U`（当 `T: Deref<Target = U>`）——
  课程里就是 `&Box<X>` 链可以逐级解到内层
- **`!` → 任意 T**（Never 可以向任何类型转换——发散臂不影响合并）

对应实现：`BodyChecker::coercible`（所有值流动位置都走它）。

## 8. LUB（Least Upper Bound coercion）★ 重点

### 是什么
编译器需要把**多个类型"合并"成一个最一般的公共类型**时使用的机制。
目标类型 `T_t` 事先未知，从候选类型集合 `T0..Tn` 迭代求出。

### 使用场景（**恰好五个，背下来**）
1. **if / else if / else 各分支**的公共类型
2. **match 各臂**的公共类型
3. **数组字面量各元素**的公共类型
4. **闭包**体内多个 return 语句的返回类型
5. **函数**体内多个 return 语句的返回类型

> 注意：loop 的多个带值 break 本质上也是这一类（我们实现里就复用了 lub）。

### 计算算法（迭代式，三步）
目标 `T_t` 从 `T0` 开始，逐个吞并 `Ti`：

1. **若 `Ti` 能 coer 到当前 `T_t`** → 目标不变
2. **否则若 `T_t` 能 coer 到 `Ti`** → 目标换成 `Ti`
3. **否则** → 求 `T_t` 与 `Ti` 的**共同超类型**作为新目标；
   求不出 → 类型错误

特点：**与顺序有关**地迭代，但每步都保证目标是"目前所有类型的公共上界"。

### 例子（spec 原例）
```rust
let bar = if (true) { a } else if (false) { b } else { c };  // 三分支 LUB
let baw = match 42 { 0 => a, 1 => b, _ => c };               // match 臂 LUB
let bax = [a, b, c];                                         // 数组元素 LUB
fn foo() -> i32 {                                            // 函数多 return：
    match 42 { 0 => a, 1 => b, _ => c }                      //   LUB(a,b,c) 必须
}                                                            //   能 coer 到 i32
```

### 与我们实现的对应
```cpp
// BodyChecker::lub(a, b, out) —— 迭代算法里"吞并一步"的实现
1. a 是 Never → out = b     // Never 可向任何类型 coer（coercion 规则）
2. b 是 Never → out = a
3. a.equal(b) → out = a     // 已相等，目标不变（算法第 1 步）
4. 两边都是引用且 elem 相等 → out = 更共享的一侧（&mut & & → &）
   // 对应算法第 3 步的"共同超类型"：引用可变性取交
```
调用点（对应五个场景在子集里的部分）：
- `visit(IfExpr)`：`lub(thenT, elseT)`，失败即"两分支类型不一致"
- `visit(ArrayExpr)`：元素类型逐个 `lub` 合并
- `visit(BreakExpr)`：同一 loop 多个 break 值 `lub` 合并
- `visit(ReturnExpr)`：返回值向声明返回类型 `coercible`（函数返回场景的
  简化版——目标类型已知）

### 典型考题形式
- 给 `[&mut a, &b]` 求元素类型 → `&i32`（`&mut` 与 `&` 合并为 `&`）
- 给 `if c { &mut x } else { &y }` 求类型 → `&i32`
- 给 `if c { return; } else { 5 }` 求类型 → `i32`（Never 与 i32 合并为 i32）
- 判断：函数声明 `-> i32`，体内 `return &mut x;` 是否合法 → 看
  `&mut i32` 能否 coer 到 `i32`（不能，报错）

---

## 附：方言差异提醒

课程 SPEC 语法上 `Conditions -> ( Expression )` **条件带括号**；
新官方 Rx 测试集按标准 Rust 语义**不要求括号**。我们编译器用
`Crate::rxDialect` 区分（文件含 `use` 声明即 Rx 方言）。小测若考 SPEC
原文，以"带括号"为准。

---

## 附录：Semantic 阶段 ↔ SPEC 章节原文对照表

> 所有路径相对 `reference/RCompiler-Spec/src/`。左边是复习主题，中间是 spec 原文，
> 右边是我们编译器里的实现位置。

### 核心必读（语义规则主要来源）

| 主题 | SPEC 原文 | 我们的实现 |
|---|---|---|
| 类型总览/递归类型 | `types.md`、`types/*.md` | `ast/type.h` |
| **coercion + LUB** | `type-coercions.md`（§ Coercion sites / § Coercion types / § Least upper bound coercions） | `BodyChecker::coercible/lub` |
| 子类型（coercion 的自反情形） | `subtyping.md` | `Type::equal`（ast/type.cpp） |
| 语句/let/分号规则 | `statements.md` | `visit(LetStmt)`、`visit(Block)` |
| 块表达式 | `expressions/block-expr.md` | `visit(Block)` |
| if 表达式 | `expressions/if-expr.md` | `visit(IfExpr)` |
| 循环/break/continue | `expressions/loop-expr.md` | `visit(WhileExpr)`、`visit(LoopExpr)`、`visit(BreakExpr)` |
| 运算符全表/溢出/短路/赋值右先 | `expressions/operator-expr.md` | `visit(BinaryExpr)`、`visit(UnaryExpr)` |
| 表达式求值顺序 | `expressions.md` § Evaluation order of operands | —（IR 阶段） |
| **place/value 语境、可变性、隐式借用** | `expressions.md` § Place Expressions and Value Expressions / § Mutability / § Implicit Borrows | `placeWritable/reborrowOk/checkReborrowUse` |
| 字面量/路径/调用/方法/字段/结构体/数组 | `expressions/literal-expr.md`、`path-expr.md`、`call-expr.md`、`method-call-expr.md`、`field-expr.md`、`struct-expr.md`、`array-expr.md`、`grouped-expr.md`、`return-expr.md` | 对应同名 visit 函数 |

### 名字与条目（CrateIndex/符号表的依据）

| 主题 | SPEC 原文 | 我们的实现 |
|---|---|---|
| **命名空间**（类型/值/宏分开的依据） | `names/namespaces.md` | `Scope` 双 map + `lookupType` |
| 名字解析 | `names/name-resolution.md` | `SymbolTable::lookup` |
| 作用域 | `names/scopes.md` | `enterScope/exitScope/insertOrShadow` |
| 函数/main 入口 | `items/functions.md` | `CrateIndex::registerFunction/checkEntryPoint` |
| struct 定义 | `items/structs.md` | `registerStruct` |
| impl / Self | `items/implementations.md` | `registerImpl`、`resolveSelfType` |
| 方法/关联常量/receiver | `items/associated-items.md` | `visit(MethodCallExpr)`、`implConsts` |
| const/static | `items/constant-items.md`、`items/static-items.md` | `registerConst/registerStatic` |
| **常量求值** | `const_eval.md` | `ConstEvaluator` |
| **derive** | `attributes/derive.md` | `validateDerives` + `typeSupports*` |
| 变量与可变性 | `variables.md` | `Symbol::isMut`、`placeWritable` |

### 明确不在 semantic 阶段范围（看到不用背）

- `items/traits.md`（超出 derive 的 trait 系统）、`items/generics.md`、泛型
- `patterns.md`、`expressions/match-expr.md`、`items/enumerations.md`（助教确认 enum/match 不要求）
- 宏：`macros.md`、`macros-by-example.md`
- 生命周期语义：`lifetime-elision.md`（标注只需穿透解析）
- 闭包：`expressions/closure-expr.md`、`types/closure.md`
- FFI/链接：`linkage.md`、`abi.md`、`items/external-blocks.md`
- 词法细节（小测归 lexer 阶段）：`lexical-structure.md`、`tokens.md` 等
