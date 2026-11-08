# A 类特性骨架：Rx 完整特性扩展路线

> 现状：rx-compiler-testcases/semantic **195/239**。剩余 44 个失败点全部属于本文档
> 列出的特性。是否实现取决于课程计分范围——动手前先和助教确认。
>
> 每条特性给出：要动的文件、数据结构骨架、检查点位置、对应测试点。

---

## 1. `Vec<T>`（约 15 个点：vec-index-mutability、vec-operations、comprehensive ×4、
## nested-containers、runtime-value-merges、lifetimes-and-use、expected-types 等）

Rx 内建动态数组，测试用到的 API：`Vec::<T>::new()`、`push`、`remove`、`len`、
下标读写、`is_empty`。

**类型表示**（`src/ast/type.h`）：不加新 TypeKind，复用 Named：
```cpp
Type vecOf(const Type& elem) {
    Type t; t.kind = TypeKind::Named; t.name = "Vec";
    t.vecElem = std::make_shared<Type>(elem);   // 新增字段：Vec 的元素类型
    return t;
}
// Type::equal：两个 Named("Vec") 相等 ⇔ vecElem 相等（含 Unknown 通配规则）
```

**符号表**（`registerBuiltins`）：注册 `Vec` 为内建 StructType（structDef 为空，
同 String），方法不走 implMethods，在 BodyChecker 里按名字特判（同现有数组 len()）。

**BodyChecker 检查点**：
- `visit(CallExpr)`：`Vec::<T>::new()` —— callee 路径含 `Vec`，元素类型从
  泛型实参文本解析（builder 需在 PathExpr 上保留 `::<T>` 段，见下）。
- `visit(MethodCallExpr)`：recvT 是 Vec 时：
  - `push(elem)` → 参数类型必须等于 vecElem，返回 ()
  - `remove(usize)` → 返回 vecElem
  - `len()` → usize；`is_empty()` → bool
- `visit(IndexExpr)`：array 操作数是 Vec 时同数组规则（下标 usize，值 = vecElem）。

**builder**（`visitPathExpression_`）：`Vec::<i32>::new` 这种带泛型实参的路径段
目前会被 `getText()` 原样拼接，需要把 `::<T>` 从段文本里解析出来，存进
PathExpr 的新字段 `genericArg`（Type），供 Vec::new 取元素类型。

**IR 影响（大）**：Vec 需要堆内存 + 运行时支持（runtime.s 里加 vec_new/
vec_push/vec_remove 的汇编织函数，或全部 inline 成 IR 调 malloc/realloc）。
这就是为什么它可能属于 Codegen 阶段之后才要求。

---

## 2. `Box<T>`（box-and-moves、methods-and-self、expected-types、arrays/acc-repeat 等）

`Box::<T>::new(v)` 堆分配；`*b` 解引用；move 语义（见 §6）。

**类型表示**：同 Vec 思路——`Named("Box")` + `boxElem` 字段；
`visit(UnaryExpr)` 的 `*` 分支增加：Box → 解引用得 boxElem。
`visit(CallExpr)`：`Box::<T>::new(v)` → 参数类型 = T，返回 Box<T>。
FieldExpr/MethodCallExpr 的自动解引用循环（`while (recvT.kind == Ref)`）
要同时剥 Box。

---

## 3. derive / trait（Copy、Clone、PartialEq）（copy-clone-and-equality 11 个点、
## recursive-traits 2 个、trait-dispatch 1 个）

Rx 规则（从测试反推）：
- `#[derive(Copy, Clone)]` 写在 struct 上；**Copy/Clone/PartialEq 不会自动获得，
  必须显式 derive**（rej-copy-must-explicitly-request-clone 等）
- Copy 要求所有字段都 Copy；Box 字段不能 derive Copy；&mut 不能 derive Clone
- 同一 struct 重复 derive 同一 trait 报错（跨多个 attribute 也算）
- `a == b`（struct 比较）要求 PartialEq；`let b = a;` 后 a 仍可用要求 Copy
  （否则是 move，见 §6）

**AST**（`src/ast/ast.h`）：
```cpp
class StructDef : public Item {
    // ...
    std::vector<std::string> derives;   // builder 从 outerAttribute 收集
};
```
**builder**：visitStructStruct 解析 `#[derive(...)]`（outerAttribute →
attr → deriveMetaItem 路径，g4 里有对应规则）。

**CrateIndex::registerStruct**：校验 derive 列表：
```cpp
// 伪代码
for (d in derives) {
    if (d 重复出现) error("重复 derive：" + d);
    if (d == "Copy")   每个字段类型必须 Copy（标量/&/数组 of Copy；Box → 拒）
    if (d == "Clone")  每个字段类型不能含 &mut
}
// 把结果记进 Symbol：sym.isCopy / isClone / hasPartialEq
```

**BodyChecker**：
- `visit(BinaryExpr)` 比较分支：两边 Named 时要求 hasPartialEq。
- `visit(LetStmt)` / 赋值：非 Copy 类型的值转移 → 标记 move（§6）。

---

## 4. 引用 coercion 与运算符自动解引用（scalar-reference-operators 3 个、
## reference-coercions 2 个、reference-lub 1 个、compound-assignment 剩余点）

四小块，互相独立，可单独做：

a) **&mut → & 隐式转换**：`Type::equal` 或专门的 `coercible(from, to)`：
   `Ref{isMut=true, e}` 可当 `Ref{isMut=false, e}` 用（let 标注、函数实参、
   赋值右值）。注意 rej-inner-reference-mutability：只剥最外层的 mut，
   `&mut &mut T` → `& &mut T`，内层不改。

b) **算术/比较的操作数自动解引用**：`x -= &2`、`r1 < r2`（引用比的是指向的值）。
   在 visit(BinaryExpr) 求完两侧类型后：
   ```cpp
   // 伪代码
   while (lhsT.kind == Ref && rhsT.kind == Ref) { lhsT = *lhsT.elem; rhsT = *rhsT.elem; }
   // 或单侧：标量 op 引用 → 剥引用那侧（ asymmetric coercion 规则见
   // rej-ordering-cannot-coerce：&mut 左不能迁就 & 右）
   ```
   再按现有规则检查。IR 阶段要对应插入 load。

c) **reference-lub**：if/match 两臂类型分别是 &T 和 &mut T 时，结果取 &T
   （earlier/later 两侧都要调整）。改 visit(IfExpr) 的分支合并：
   不等时尝试 coercion 合并而不是直接报错。

d) **receiver 自动借用**：`s.method()` 而 method 是 &self/&mut self 且 s 是
   值类型——现在 visit(MethodCallExpr) 已按剥引用后的类型查方法，欠的是
   `&self` 方法在 `&&&S` 这类多层引用 receiver 上的逐层匹配（Box 也要剥）。

---

## 5. 生命周期标注（lifetimes-and-use 等）

纯语法层：g4 已能解析 `'a`、`where 'long: 'short`。
builder 已全部穿透（类型里的 lifetime 被忽略、泛型参数只留
hasGenericParams/hasGenericArgs 标记）。
**剩余工作只是验证**：所有带生命周期标注的测试点在语义上应与无标注等价。
目前 lifetimes-and-use 卡在 Vec（§1），生命周期本身没有额外工作。

---

## 6. move 语义与借用检查（vec-index-mutability 的 rej、
## references-and-mutability 深层、box-and-moves 的 partial move）

最重的一块，本质是简化版 borrow checker：

- **place（位置）表达式追踪**：给 BodyChecker 加一个
  `placeOf(expr) → {根符号, 路径}` 的求值器（checkAssignable 里的 walk 就是雏形）。
- **move 表**：`std::unordered_set<const Symbol*> movedOut`（按作用域栈保存）。
  非 Copy 类型的值被 let/传参/返回 转移后，根符号标记 moved；
  之后任何读用报错（"use of moved value"）。重新赋值可复活
  （reinitialization after move）。
- **借用表**：`&` 产生共享借用、`&mut` 产生独占借用；检查规则：
  有活着的共享借用时禁止 &mut 和写；有活着的独占借用时禁止任何其他访问。
  "活着"按词法作用域近似（NLL 的 reborrow-ends-before-parent-reuse 语义
  需要按语句序号模拟，测试点 reborrows-end-before-parent-reuse 查的就是这个）。
- **容器透传**：`&arr[i]`、`&s.f` 的借用挂在根符号 + 投影路径上；
  `&mut arr[i]` 与 `&arr[j]` 的冲突判断按投影路径前缀比较。

建议顺序：place 求值器 → move 表（box-and-moves 大部分点）→ 借用表
（vec-index-mutability 的 rej 点）。

---

## 建议的实施顺序（按性价比）

1. **§4a + §4b**（~6 个点，改动集中在 Type::equal/coercible 和两个 visit）——最快
2. **§3 derive 检查**（~8 个 rej 点几乎全是纯注册期检查，不涉及表达式语义）
3. **§4c + §4d**（~3 个点）
4. **§2 Box**（~4 个点；若课程不做堆分配 IR，语义可以先过）
5. **§6 move/借用**（~6 个点，工作量最大）
6. **§1 Vec**（~15 个点，但牵涉 IR/运行时，必须确认属于哪个阶段再动）
