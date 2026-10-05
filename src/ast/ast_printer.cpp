#include "ast_printer.h"
#include "ast.h"
#include <ostream>

// ============================================================
// 这是调试工具，不是编译器逻辑。每个 visit 的套路都一样：
//   1. line() 打印本节点信息
//   2. indent_++，对所有非空子节点调 accept(*this)，indent_--
// ============================================================

ASTPrinter::ASTPrinter(std::ostream& out) : out_(out) {}

void ASTPrinter::line(const std::string& text) {
    for (int i = 0; i < indent_; ++i) out_ << "  ";
    out_ << text << '\n';
}

void ASTPrinter::visit(Crate& node) {
    line("Crate");
    ++indent_;
    for (auto* item : node.items) {
        if (item) item->accept(*this);
    }
    --indent_;
}

void ASTPrinter::visit(Block& node) {
    line("Block");
    ++indent_;
    for (auto* stmt : node.statements) {
        if (stmt) stmt->accept(*this);
    }
    --indent_;
}

void ASTPrinter::visit(Function& node) {
    line("Function " + node.name);
    ++indent_;
    if (node.body) node.body->accept(*this);
    --indent_;
}

void ASTPrinter::visit(LetStmt& node) {
    // TODO(你): 打印成 "LetStmt x : i32" 的形式，
    // 注意 typeName 可能为空（let x = 1; 没写类型标注）
    line("LetStmt " + node.name + (node.typeName.empty() ? "" : " : " + node.typeName));
    ++indent_;
    if (node.init) node.init->accept(*this);
    --indent_;
}

void ASTPrinter::visit(ExprStmt& node) {
    line("ExprStmt");
    ++indent_;
    if (node.expr) node.expr->accept(*this);
    --indent_;
}

void ASTPrinter::visit(Literal& node) {
    line("Literal " + node.text);
}

void ASTPrinter::visit(BinaryExpr& node) {
    line("BinaryExpr " + node.op);
    ++indent_;
    if (node.lhs) node.lhs->accept(*this);
    if (node.rhs) node.rhs->accept(*this);
    --indent_;
}
