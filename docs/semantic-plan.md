# 语义分析阶段 Coding Plan

目标：跑通 `reference/RCompiler-Testcases/semantic-1`（222 个用例，含正例和负例），
有余力再对 `semantic-2`（50 个）。判定标准：该报错的报错退出，该通过的安静通过。

## 总体架构

```
Crate (AST)
   │  Pass 1: DeclCollector —— 只走顶层，注册所有"名字"
   │    fn 签名、struct/enum 定义、const/static、impl 方法
   ▼
全局作用域填满
   │  Pass 2: SemanticChecker —— 逐函数遍历函数体
   │    解析名字 + 推导/检查类型 + 控制流检查
   ▼
报错（打印错误信息，非 0 退出）或 通过
```

两个类都实现 `ASTVisitor` 接口（`src/ast/ast_visitor.h`）。
表达式求类型用成员变量传递：`Type lastType;`（visit 完一个表达式节点后，
它的类型写进 lastType，父节点取走——避免改 visitor 接口的返回类型）。

## 里程碑

### M1：类型表示 + 符号表（本阶段地基）
- `src/ast/type.h`：`TypeKind` 枚举 + `Type` 结构 + `equal()`/`toString()`
  （CR 后从 src/semantic/ 移到 src/ast/，AST 与语义阶段共用同一套类型表示）
- `src/semantic/symbol_table.h/.cpp`：`Scope` / `Symbol` / `SymbolTable`
- 验收：符号表能插入、链式查找、拒绝重复定义（写个小的临时 main 测一下即可）

### M2：Pass 1 声明收集
- `DeclCollector::visit(Crate)` 遍历顶层 item
- 注册：Function（参数类型+返回类型）、StructDef（名字+字段表）、EnumDef
  （名字+变体表）、ConstDef、StaticDef
- impl 块：把方法注册到"类型名 → 方法表"（Symbol 里挂 map 或单独一张
  `std::unordered_map<std::string, std::vector<Symbol>> implMethods`）
- **预注册内建函数**（先于一切）：`printlnInt(i32)`、`printInt(i32)`、
  `getInt() -> i32`、`exit(i32)`，宏 `println!`/`print!` 特殊处理
- 重复定义在这里报错（两个同名 fn、同名 struct……）
- 验收：对 syntax-tour.rs 跑完能把所有顶层名字打印出来（先写个 dump 调试函数）

### M3：Pass 2 表达式类型检查（工作量最大）
按节点类型逐个实现，建议顺序：
1. Literal（**先把 ast.h:88 的 TODO 做掉**：给 Literal 加 kind 枚举，
   builder 里填好——整数→i32，true/false→bool，字符串→&str/String，字符→char）
2. PathExpr（查符号表，查不到报"未声明"）
3. BinaryExpr（算术要求两边同整型→结果同型；比较→bool；&& ||→bool）
4. UnaryExpr（- 要整型；! 要 bool；& 造引用类型；* 解引用剥一层）
5. LetStmt（标注和初值匹配；无标注则类型=初值类型；注册进当前作用域）
6. AssignExpr / CompoundAssign（左值检查 + 类型匹配 + mut 检查）
7. IfExpr / WhileExpr / LoopExpr（cond 必须 bool；if 两分支类型一致）
8. CallExpr（查函数签名，参数个数+类型逐个比对）
9. FieldExpr / MethodCallExpr（查 struct 字段表 / impl 方法表）
10. ArrayExpr / IndexExpr（元素类型一致；下标必须整型）
11. StructLiteralExpr（字段存在、类型匹配、不多不少）
- 验收：semantic-1 里挑几个简单正例过，负例能报错

### M4：控制流与其他检查
- ReturnExpr：值类型 == 当前函数返回类型（记录 currentFnRetType）
- BreakExpr/ContinueExpr：必须在循环内（维护 loopDepth 计数器）
- const 不可赋值、不可变引用不可写
- 函数体尾表达式类型 == 声明返回类型（衔接 AST 的 tailExpr！）
- ~~enum/match 相关检查~~：**助教 CR 确认不要求**，遇到直接跳过

### M5：批量跑测试集
- 写个脚本遍历 semantic-1/src 下所有用例，统计通过/失败
- 对负例：只要报错退出就算过；对正例：不报错算过
- 逐个修 failure

## 错误处理约定
- `SemanticError` 用异常或 `std::cerr + exit(1)` 均可，全项目统一一种
- 错误信息带上下文（哪个名字、什么类型不匹配），调试用

## 常见坑（预判）
1. `let x: i32 = 1 + 2;` 里 `1 + 2` 的类型推导依赖 Literal 的 kind——M3 第 1 步先做
2. 作用域进入/退出要和 Block 遍历严格配对（进入 Block 压栈，离开弹栈；
   函数参数属于函数体作用域）
3. 方法调用的 receiver 类型决定查哪个 impl 表
4. `if` 没有 else 时类型是 unit，不能出现在需要值的位置
5. 内建函数别忘了，否则 `printlnInt(1)` 全报"未声明"
