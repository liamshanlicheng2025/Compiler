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
    // TODO(之后): Rust 里 block 其实是表达式（最后一个不带分号的表达式是块的值），
    // 支持 if/while 的块返回值时再处理
    void accept(ASTVisitor& visitor) override;
};

// 函数定义
class Function : public Item {
public:
    std::string name;
    Block* body = nullptr;
    // TODO(之后): 参数列表（名字+类型）、返回类型
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

// ---- 之后扩展时加的节点（提示，先别实现）----
// IfExpr      : cond, thenBlock, elseBlock(可空)
// WhileExpr   : cond, body
// ReturnStmt  : value(可空)
// CallExpr    : callee, args
// StructDef   : name, fields      —— 属于 Item
// PathExpr    : 变量/函数名引用   —— 很快就需要，x + 1 里的 x 就是它
