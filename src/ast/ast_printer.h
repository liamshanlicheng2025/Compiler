#pragma once

// ============================================================
// AST 打印器：把 AST 以缩进树的形式输出，用于自查
// 目标效果（tests/minimal.rs）：
//
// Crate
//   Function main
//     Block
//       LetStmt x : i32
//         BinaryExpr +
//           Literal 1
//           Literal 2
// ============================================================

#include "ast_visitor.h"
#include <iosfwd>
#include <string>

class ASTPrinter : public ASTVisitor {
public:
    explicit ASTPrinter(std::ostream& out);

    void visit(Crate& node) override;
    void visit(Block& node) override;
    void visit(Function& node) override;
    void visit(LetStmt& node) override;
    void visit(ExprStmt& node) override;
    void visit(Literal& node) override;
    void visit(BinaryExpr& node) override;
    void visit(PathExpr& node) override;
    void visit(UnaryExpr& node) override;
    void visit(IfExpr& node) override;
    void visit(WhileExpr& node) override;
    void visit(LoopExpr& node) override;
    void visit(ReturnExpr& node) override;
    void visit(BreakExpr& node) override;
    void visit(ContinueExpr& node) override;
    void visit(CallExpr& node) override;
    void visit(StructDef& node) override;
    void visit(ImplBlock& node) override;
    void visit(StructLiteralExpr& node) override;
    void visit(FieldExpr& node) override;
    void visit(MethodCallExpr& node) override;
    void visit(ArrayExpr& node) override;
    void visit(IndexExpr& node) override;
    void visit(ConstDef& node) override;
    void visit(StaticDef& node) override;
    void visit(EnumDef& node) override;
    void visit(MatchExpr& node) override;
    void visit(MacroStmt& node) override;

private:
    std::ostream& out_;
    int indent_ = 0;

    // 输出当前缩进 + 一行文字
    void line(const std::string& text);

    // 辅助：进入子节点前 indent_++，出来后 indent_--，
    // 注意保证异常/提前返回时也能恢复（想想怎么写最稳妥）
};
