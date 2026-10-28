#pragma once

// ============================================================
// AST 节点定义
//
// 内存策略：全项目统一使用 std::shared_ptr 持有节点，自动管理生命周期。
// 设计约定：
//   - 节点间传递一律用别名：CratePtr / ItemPtr / StmtPtr / ExprPtr / BlockPtr
//   - visitor 接口仍按引用传递（visit(Xxx&)），accept 不变
//   - 类型标注一律存结构化 Type（ast/type.h），不再存字符串原文
//   - 每个具体节点类都要在 ast.cpp 里实现 accept()
// ============================================================

#include <memory>
#include <string>
#include <vector>

#include "ast/type.h"

class ASTVisitor;

// ---- 前向声明 + 智能指针别名 ----

class Node;
class Item;
class Stmt;
class Expr;
class Crate;
class Block;
class Function;
class ConstDef;

enum class LiteralKind {Int, Float, Bool, Char, Str};

using NodePtr  = std::shared_ptr<Node>;
using ItemPtr  = std::shared_ptr<Item>;
using StmtPtr  = std::shared_ptr<Stmt>;
using ExprPtr  = std::shared_ptr<Expr>;
using CratePtr = std::shared_ptr<Crate>;
using BlockPtr = std::shared_ptr<Block>;
using FunctionPtr = std::shared_ptr<Function>;

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

// ---- 基础节点 ----

// 整个源文件（对应 g4 的 crate 规则）
class Crate : public Node {
public:
    std::vector<ItemPtr> items;
    void accept(ASTVisitor& visitor) override;
};

// 语句块 { ... }——Rust 里块本身是表达式（值 = 尾表达式或 ()），
// 所以继承 Expr：裸块可以直接当 ExprStmt 的内容
class Block : public Expr {
public:
    std::vector<StmtPtr> statements;
    ExprPtr tailExpr;            // 尾部表达式：{ stmt*; expr } 里那个不带分号的 expr，
                                 // 是块的值（如 fn f() -> i32 { 1 + 2 }）；空指针 = 无
    void accept(ASTVisitor& visitor) override;
};

// 函数参数（名字 + 结构化类型标注）
struct Param {
    std::string name;
    Type type;
    bool isMut = false;   // fn f(mut n: i32) 的可变参数（semantic 允许对其赋值）
};

// 函数定义
class Function : public Item {
public:
    std::string name;
    std::vector<Param> params;   // 空 vector = 无参数（不含 self）
    std::shared_ptr<Type> returnType;  // 空指针 = 无返回类型标注（返回 ()）
    BlockPtr body;
    bool isMethod = false;       // impl 块里第一个参数是 self/&self/&mut self 的方法
    bool selfMut = false;        // &mut self 或 mut self 时为 true
    bool selfByValue = false;    // 按值 self（fn better(self, ...)），类型不是引用
    bool hasGenericParams = false;  // 带 <T> 泛型参数（子集不支持泛型，仅入口检查用）
    void accept(ASTVisitor& visitor) override;
};

// let 声明：let x: i32 = 1 + 2; / let mut y = 5;
class LetStmt : public Stmt {
public:
    std::string name;
    std::shared_ptr<Type> type;  // 类型标注；空指针 = 省略，由语义检查阶段推导
    ExprPtr init;                // 初值表达式；let x; 无初值时是空指针
    bool isMut = false;          // let mut（语义阶段查"给不可变变量赋值"要用）
    void accept(ASTVisitor& visitor) override;
};

// 表达式语句：一个表达式后面跟分号，如 foo();
class ExprStmt : public Stmt {
public:
    ExprPtr expr;
    bool hasSemi = true;   // expressionWithBlock 的分号可选；无分号的尾部
                           // if/loop/match/块 是所在块的值（basic22）
    void accept(ASTVisitor& visitor) override;
};

// 语句位置的顶层条目：块内的嵌套 fn、局部 const/static
// （g4 的 statement 规则允许 item；return12 的嵌套函数、loop3 的局部 const）
class ItemStmt : public Stmt {
public:
    ItemPtr item;
    void accept(ASTVisitor& visitor) override;
};

// as 类型转换：expr as T（如 counter < len as i32）
class CastExpr : public Expr {
public:
    ExprPtr expr;
    Type target;                 // as 后面的目标类型
    void accept(ASTVisitor& visitor) override;
};

// 字面量：1、true、"hello"……
class Literal : public Expr {
public:
    std::string text;          // 原文（IR 阶段取数值用）
    LiteralKind kind;          // 种类（builder 填好，语义阶段直接查表）
    void accept(ASTVisitor& visitor) override;
};

// 二元运算：lhs op rhs
// 比较（==、<、>……）和赋值（=、+=……）复用本节点，op 存运算符字符串
class BinaryExpr : public Expr {
public:
    std::string op;            // "+", "-", "*", "/", "%", "==", "=", "+=", ...
    ExprPtr lhs;
    ExprPtr rhs;
    void accept(ASTVisitor& visitor) override;
};

// 变量/路径引用：x、foo、Point::new（多段用 :: 拼进 name）
class PathExpr : public Expr {
public:
    std::string name;
    void accept(ASTVisitor& visitor) override;
};

// 一元运算：-x、!x、*p（解引用）、&x / &mut x（引用）
class UnaryExpr : public Expr {
public:
    std::string op;            // "-", "!", "*", "&", "&mut"
    ExprPtr operand;
    void accept(ASTVisitor& visitor) override;
};

// ---- 控制流 ----
// 注意：Rust 里 if/while/loop/return/break/continue 都是表达式（Expr）。

// if 表达式
class IfExpr : public Expr {
public:
    ExprPtr cond;
    BlockPtr thenBlock;
    BlockPtr elseBlock;          // 空指针 = 无 else；else if 链 = elseBlock 里包一个
                                 // 只含尾表达式 IfExpr 的 Block
    bool condGrouped = true;     // 条件是否带括号（课程规范要求带，expr12/20/25/30）
    void accept(ASTVisitor& visitor) override;
};

// while 循环
class WhileExpr : public Expr {
public:
    ExprPtr cond;
    BlockPtr body;
    bool condGrouped = true;     // 同 IfExpr
    void accept(ASTVisitor& visitor) override;
};

// loop 无限循环
class LoopExpr : public Expr {
public:
    BlockPtr body;
    void accept(ASTVisitor& visitor) override;
};

// return / break / continue
class ReturnExpr : public Expr {
public:
    ExprPtr value;               // 空指针：裸 `return;`
    void accept(ASTVisitor& visitor) override;
};

class BreakExpr : public Expr {
public:
    ExprPtr value;               // 空指针：`break;`；带值 break 之后再说
    void accept(ASTVisitor& visitor) override;
};

class ContinueExpr : public Expr {
public:
    void accept(ASTVisitor& visitor) override;
};

// 函数调用：callee(arg1, arg2, ...)
class CallExpr : public Expr {
public:
    ExprPtr callee;                      // 通常是 PathExpr（函数名）
    std::vector<ExprPtr> args;
    void accept(ASTVisitor& visitor) override;
};

// ---- 复合类型 ----

// struct 定义：struct Point { x: i32, y: i32 }
class StructDef : public Item {
public:
    std::string name;
    std::vector<Param> fields;   // 字段复用 Param（也是"名字+类型"）
    void accept(ASTVisitor& visitor) override;
};

// impl 块：impl Point { fn new(...) ... fn norm2(&self) ... }
class ImplBlock : public Item {
public:
    std::string typeName;                // impl 目标类型名（名字引用，语义阶段解析）
    std::vector<FunctionPtr> methods;
    std::vector<std::shared_ptr<ConstDef>> consts;   // 关联常量：impl Config { const N: usize = 3; }
    void accept(ASTVisitor& visitor) override;
};

// 结构体字面量：Point { x: 1, y: 2 }
class StructLiteralExpr : public Expr {
public:
    std::string name;                                    // 结构体名
    std::vector<std::pair<std::string, ExprPtr>> fields; // (字段名, 初值) 保持声明顺序
    void accept(ASTVisitor& visitor) override;
};

// 字段访问：p.x
class FieldExpr : public Expr {
public:
    ExprPtr object;
    std::string field;
    void accept(ASTVisitor& visitor) override;
};

// 方法调用：p.shift(3)
class MethodCallExpr : public Expr {
public:
    ExprPtr receiver;                    // 点号左边的对象
    std::string method;
    std::vector<ExprPtr> args;           // 不含 self
    void accept(ASTVisitor& visitor) override;
};

// ---- 数组 / 全局条目 ----

// 数组字面量：[1, 2, 3] 或 [0; 5]（重复填充）
class ArrayExpr : public Expr {
public:
    std::vector<ExprPtr> elements;   // 列举形式 [1, 2, 3]
    ExprPtr repeatValue;             // 重复形式 [0; 5]：值
    ExprPtr repeatCount;             //                和次数；两者为空则是列举形式
    void accept(ASTVisitor& visitor) override;
};

// 下标访问：a[i]
class IndexExpr : public Expr {
public:
    ExprPtr array;
    ExprPtr index;
    void accept(ASTVisitor& visitor) override;
};

// 全局常量：const MAX: i32 = 100;
class ConstDef : public Item {
public:
    std::string name;
    Type type;                     // 类型标注语法上必填，按值存
    ExprPtr value;
    // 编译期求值结果（ConstEvaluator::evaluateAll 填写）：
    // 关联常量不走符号表，值就存在节点上，BodyChecker 经 implConsts 读到
    bool hasConstValue = false;
    long long constValue = 0;
    void accept(ASTVisitor& visitor) override;
};

// 全局静态量：static mut COUNT: i32 = 0;
class StaticDef : public Item {
public:
    std::string name;
    Type type;
    bool isMut = false;            // static mut
    ExprPtr value;
    void accept(ASTVisitor& visitor) override;
};

// ---- enum / match ----

// enum 的一个变体：Cell::X（无负载）、Circle(f64)（tuple 式负载）
struct EnumVariant {
    std::string name;
    std::vector<Type> payloadTypes;  // 空 = 单元变体（无负载）
    // struct 式变体（Rect { w: f64 }）在官方测试中基本不出现，暂不支持
};

// enum 定义
class EnumDef : public Item {
public:
    std::string name;
    std::vector<EnumVariant> variants;
    void accept(ASTVisitor& visitor) override;
};

// match 的一个分支：pattern => body
struct MatchArmNode {
    std::string patternText;   // 模式原文（Cell::X、1、_ 等）；
                               // 语义检查若需要结构化模式再升级为节点
    ExprPtr body;              // 分支体是表达式时用这个
    BlockPtr bodyBlock;        // 分支体是块 { ... } 时用这个（二选一）
};

// match 表达式
class MatchExpr : public Expr {
public:
    ExprPtr scrutinee;                  // match 后面被匹配的值
    std::vector<MatchArmNode> arms;
    void accept(ASTVisitor& visitor) override;
};

// 语句级宏调用：println!(...)
class MacroStmt : public Stmt {
public:
    std::string name;
    std::string argsText;
    void accept(ASTVisitor& visitor) override;
};
