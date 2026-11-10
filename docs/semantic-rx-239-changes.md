# Semantic 阶段补全记录：165 → 239/239

> 时间：2026-11-08 ~ 2026-11-09。对象：rx-compiler-testcases/semantic（239 点）。
> 基线：旧套件 RCompiler-Testcases semantic-1 (221/221)、semantic-2 (50/50) 全程不破。
> 分数路径：165 → 168 → 195 → 205 → 214 → 221 → 223 → **239**。

## 总览

74 个失败点分三类：

| 类别 | 数量 | 性质 |
|---|---|---|
| 方言冲突（use 声明、条件括号） | ~17 | 新旧两套规范矛盾，需方言切换 |
| 子集内检查缺口 | ~26 | 漏做的语义检查，规则本就属于简化子集 |
| 完整特性（Vec/Box/trait/coercion/借用） | ~31 | 需要新建类型系统与检查框架 |

---

## 一、Rx 方言兼容（165 → 168）

**问题**：Rx 测试每个文件以 `use rx::core::*;` 开头、if/while 条件不写括号（标准
Rust）；旧课程规范强制条件带括号（expr12/20/25/30 是对应 rej 用例）。两套规范
互相矛盾。

**改动**：
- `Crate` 加 `rxDialect` 标记；builder 在 `visitCrate` 识别 `KW_USE` 起始的顶层
  条目，静默跳过并标记方言（`src/builder/ast_builder.cpp`）
- `BodyChecker::visit(IfExpr)/visit(WhileExpr)` 的括号检查加方言门控
- 教训：3 个 rej 用例此前靠括号规则"误打误撞"判对，方言放开后暴露，由第二阶段
  的真正检查接住

## 二、子集内检查缺口（168 → 195）

### 控制流（body_checker.cpp）
- while/loop 循环体的块类型必须是 `()` 或 Never
- while 拒绝带值 `break`；while 条件求值时 `loopDepth_` 临时归零
  （拦 `loop { while continue {} }` 的条件跳外层）
- 无 else 的 if 分支必须返回 `()`；块中间的无分号语句必须返回 `()`
- **发散传播**：新增 `lastDiverges_`，return/break/continue/无 break 的 loop
  置真，沿 Block 传播。发散块类型为 Never，可匹配任何上下文
  （修复 `{ { loop {}; 42 }; }`、`return ();` 后不可达代码的误判）

### 名称与类型
- **符号表拆类型/值双命名空间**（symbol_table.h/.cpp）：Scope 两张 map +
  `lookupType`。修复 `struct Same` 与 `fn Same` 共存
- 保护名拦截（`struct i32`）、字段查重、参数查重、顶层函数禁止 receiver、
  impl 方法与关联常量共享命名空间跨块查重（crate_index.cpp）
- `Self` 仅限 impl 内使用；签名/标注的具名类型必须可解析；无泛型 struct
  带 `<...>` 使用报错（`S<i32>`）

### 运算符与引用
- Rx 方言下标必须 usize；一元 `-` 限有符号整型
- bool 的 `&` `|` `^` 与 `&=` `|=` `^=`；补 `<<=` `>>=` 分支
- 引用赋值逐层验证 `&mut`（拦 `*p = 2` 共享引用、`**q = 2` 外层共享）
- **`&&x` 合并词法修复**：g4 把 `&&` 当单 token，展开为两层嵌套借用，
  `mut` 归内层
- 临时值可调 `&mut self` 方法；关联函数（无 self）禁止点调用

## 三、完整特性实现（195 → 239）

### 类型系统（type.h/.cpp、ast_builder.cpp）
- `Vec<T>`/`Box<T>` = `Named` + `elem`；equal/toString 感知元素类型
- builder 解析类型位置泛型实参与表达式 turbofish（`PathExpr.genericArg`）
- `()` 解析为 unit 字面量（LiteralKind::Unit）

### Vec/Box 内建行为（body_checker.cpp）
- `new/clone/push/remove/len/is_empty`：点调用 + 关联函数两种形式
- 下标/字段/receiver 的自动解引用剥 Ref + Box；`*box` 解引用
- clone 优先级：用户自定义方法 > 派生 Clone > 内建容器 > 共享引用本身
- Clone/PartialEq 传导到元素类型（`Vec<不可Clone>` 不可克隆）

### derive/trait 框架（crate_index.cpp）
- builder 沿父链收集 item 上的 `#[derive(...)]`
- 两遍注册后 `validateDerives`：查重、Copy→Clone 前提、Eq→PartialEq 前提、
  逐字段 `typeSupports*` 递归检查、写回 `sym.isCopy/isClone/hasPartialEq`
- 数组重复形式 `[v; N]`（N>1）要求元素 Copy（Rx 方言限定，旧套件无 derive）

### coercion / lub
- 值流动全部改走 `coercible`：引用对引用时外层 mut 兼容 + Box 解引用链，
  `derefChain` 用严格比较（防 equal 的内层 mut 放行误放 rej-inner）
- if 两臂 / 数组元素 / loop 多 break 值改走 `lub`（引用合并取更共享一侧）

### 运算符自动解引用（按 rej 逐条对齐）
- 算术/位/移位：剥一层共享引用（`&&a + &b` 拦）
- `==`/`!=`：全层剥（点值比较），struct/容器要求 PartialEq
- 排序比较：不对称（右侧必须可转换到左侧），禁止引用/值混用
- 一元 `-`/`!`：一层共享引用允许，`&mut` 拒绝

### 可变性体系（简化借用检查）
- `placeWritable(e)`：位置整体可写性（赋值目标、`&mut` 借用、push receiver）
- `reborrowOk(place)`：存放的 `&mut` 值的可变重借——**Vec 元素不绕过 Vec
  自身可变性**（不可变 Vec 里的 `&mut` 元素不能写），**数组/struct/Box 保持**
  内部 `&mut` 写能力
- `&mut` 值当右值用（let/传参）同样过 reborrowOk；共享化（`&mut → &`）不受限

### Self 与杂项
- `Self` 归一化：参数/返回类型、结构体字面量、`Self::CONST`、字段类型
- 关联函数形式调方法：首参当 receiver（`Counter::add(&mut c, 2)`）
- impl 目标必须用户 struct；Vec/Box 必须恰好一个类型参数；turbofish 调
  非泛型函数报错；`Vec::remove` 下标 usize

## 过程中的坑（复盘用）

1. **占位实现要覆盖 happy path**：lub 占位直接 return false，瞬间崩掉旧套件
   100+ 个点
2. **辅助函数内部 evalExpr 会冲掉 `lastType_`**：LetStmt 必须先存类型再调
   checkReborrowUse
3. **Rx 专属规则必须 `rxDialect_` 门控**：数组重复形式的 Copy 检查误伤了没有
   derive 概念的旧套件
