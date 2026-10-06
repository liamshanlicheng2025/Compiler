#pragma once

// ============================================================
// AST 节点定义
//
// 内存策略：全项目统一使用裸指针，节点创建后活到进程结束，不 delete。
// 设计约定：
//   - 每个具体节点类都要在 ast.cpp 里实现 accept()
//   - 之后做语义分析时，可能要给节点挂类型信息/源码位置，
//     到时在 Node 基类里加成员即可
// ============================================================

#include <string>
#include <vector>

class ASTVisitor;

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
};

// ---- 三个抽象层：顶层条目 / 语句 / 表达式 ----
// 它们自己不实现 accept()，保持抽象；具体节点各自实现。

class Item : public Node {};   // crate 顶层：函数、struct、enum……
class Stmt : public Node {};   // 语句：let、表达式语句……
class Expr : public Node {};   // 表达式：字面量、二元运算……

// ---- 第一个里程碑需要的 6 个具体节点 ----

// 整个源文件（对应 g4 的 crate 规则）
class Crate : public Node {
public:
    std::vector<Item*> items;
    void accept(ASTVisitor& visitor) override;
};

// 语句块 { ... }
class Block : public Node {
public:
    std::vector<Stmt*> statements;
    Expr* tailExpr = nullptr;    // 尾部表达式：{ stmt*; expr } 里那个不带分号的 expr，
                                 // 是块的值（如 fn f() -> i32 { 1 + 2 }）
    void accept(ASTVisitor& visitor) override;
};

// 函数参数（名字 + 类型标注）
struct Param {
    std::string name;
    std::string typeName;
};

// 函数定义
class Function : public Item {
public:
    std::string name;
    std::vector<Param> params;   // 空 vector = 无参数
    std::string returnType;      // 空字符串 = 无返回类型标注（返回 ()）
    Block* body = nullptr;
    // TODO(第 4 波): impl 里的方法有 selfParam（&self / &mut self），到时加字段
    void accept(ASTVisitor& visitor) override;
};

// let 声明：let x: i32 = 1 + 2;
class LetStmt : public Stmt {
public:
    std::string name;
    std::string typeName;      // 类型标注，没写就留空字符串（之后再支持省略）
    Expr* init = nullptr;      // 初值表达式；let x; 无初值时是 nullptr
    void accept(ASTVisitor& visitor) override;
};

// 表达式语句：一个表达式后面跟分号，如 foo();
class ExprStmt : public Stmt {
public:
    Expr* expr = nullptr;
    void accept(ASTVisitor& visitor) override;
};

// 字面量：1、true、"hello"……
class Literal : public Expr {
public:
    std::string text;          // 先存原文
    // TODO: 用一个枚举区分种类（整数/布尔/字符/字符串），IR 阶段会需要
    void accept(ASTVisitor& visitor) override;
};

// 二元运算：lhs op rhs
class BinaryExpr : public Expr {
public:
    std::string op;            // "+", "-", "*", "/", "%", "==", ...
    Expr* lhs = nullptr;
    Expr* rhs = nullptr;
    void accept(ASTVisitor& visitor) override;
};

// ---- 第 1 波：表达式补全 ----
// 比较（==、<、>……）和赋值（=、+=……）不需要新节点：
// 复用 BinaryExpr，op 存运算符字符串即可

// 变量/路径引用：x、foo、Point::new
class PathExpr : public Expr {
public:
    std::string name;          // 先只支持单段（变量名）；
                               // 多段路径（Point::new）做 impl 时再拆段
    void accept(ASTVisitor& visitor) override;
};

// 一元运算：-x、!x、*p（解引用）
class UnaryExpr : public Expr {
public:
    std::string op;            // "-", "!", "*"
    Expr* operand = nullptr;
    void accept(ASTVisitor& visitor) override;
};

// ---- 第 2 波：控制流 ----
// 注意：Rust 里 if/while/loop/return/break/continue 都是表达式（Expr），
// 不是语句。它们出现在"语句位置"时由外层规则包装，
// 你在 visitExpressionStatement 里决定怎么包（提示见 ast_builder.cpp）。

// if 表达式
class IfExpr : public Expr {
public:
    Expr* cond = nullptr;
    Block* thenBlock = nullptr;
    Block* elseBlock = nullptr;  // 可空；else if 链 = elseBlock 里包一个
                                 // 只含一条 ExprStmt(IfExpr) 的 Block
    void accept(ASTVisitor& visitor) override;
};

// while 循环
class WhileExpr : public Expr {
public:
    Expr* cond = nullptr;
    Block* body = nullptr;
    void accept(ASTVisitor& visitor) override;
};

// loop 无限循环
class LoopExpr : public Expr {
public:
    Block* body = nullptr;
    void accept(ASTVisitor& visitor) override;
};

// return / break / continue
class ReturnExpr : public Expr {
public:
    Expr* value = nullptr;       // 可空：裸 `return;`
    void accept(ASTVisitor& visitor) override;
};

class BreakExpr : public Expr {
public:
    Expr* value = nullptr;       // 可空：`break;`；带值 break 之后再说
    void accept(ASTVisitor& visitor) override;
};

class ContinueExpr : public Expr {
public:
    void accept(ASTVisitor& visitor) override;
};

// ---- 第 3 波：函数完整化 ----

// 函数调用：callee(arg1, arg2, ...)
class CallExpr : public Expr {
public:
    Expr* callee = nullptr;              // 通常是 PathExpr（函数名）
    std::vector<Expr*> args;
    void accept(ASTVisitor& visitor) override;
};

class MacroStmt : public Stmt {
public:
    std::string name;
    std::string argsText;
    void accept(ASTVisitor& visitor) override;
};
// ---- 第 3 波及之后（提示，先别实现）----
// Function 节点扩展：参数列表（名字+类型）、返回类型
// CallExpr    : callee(Expr*), args(vector<Expr*>)   —— 函数调用
// Block 扩展  : 尾部表达式 tailExpr（statements 规则里那个落单的 expression?）
// StructDef   : name, fields                          —— 属于 Item
// ImplBlock   : typeName, methods(vector<Function*>)  —— 属于 Item
// FieldExpr / MethodCallExpr / ArrayExpr / IndexExpr
