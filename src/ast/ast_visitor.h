#pragma once

// ============================================================
// AST 访问者接口
//
// 之后所有"对 AST 做一遍遍历"的操作都实现这个接口：
// 打印（ASTPrinter）、语义检查、IR 生成……
// 每加一个新的具体节点类，记得在这里加对应的 visit。
// ============================================================

class Crate;
class Block;
class Function;
class LetStmt;
class ExprStmt;
class Literal;
class BinaryExpr;
class PathExpr;
class UnaryExpr;
class IfExpr;
class WhileExpr;
class LoopExpr;
class ReturnExpr;
class BreakExpr;
class ContinueExpr;

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Crate& node) = 0;
    virtual void visit(Block& node) = 0;
    virtual void visit(Function& node) = 0;
    virtual void visit(LetStmt& node) = 0;
    virtual void visit(ExprStmt& node) = 0;
    virtual void visit(Literal& node) = 0;
    virtual void visit(BinaryExpr& node) = 0;
    virtual void visit(PathExpr& node) = 0;
    virtual void visit(UnaryExpr& node) = 0;
    virtual void visit(IfExpr& node) = 0;
    virtual void visit(WhileExpr& node) = 0;
    virtual void visit(LoopExpr& node) = 0;
    virtual void visit(ReturnExpr& node) = 0;
    virtual void visit(BreakExpr& node) = 0;
    virtual void visit(ContinueExpr& node) = 0;
};
