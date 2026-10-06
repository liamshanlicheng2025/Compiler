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
    if (node.tailExpr) node.tailExpr->accept(*this);
    --indent_;
}

void ASTPrinter::visit(Function& node) {
    std::string sig = "Function " + node.name + "(";
    for (size_t i = 0; i < node.params.size(); ++i) {
        if (i) sig += ", ";
        sig += node.params[i].name + ": " + node.params[i].typeName;
    }
    sig += ")";
    if (!node.returnType.empty()) sig += " -> " + node.returnType;
    line(sig);
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

void ASTPrinter::visit(PathExpr& node) {
    line("PathExpr " + node.name);
}

void ASTPrinter::visit(UnaryExpr& node) {
    line("UnaryExpr " + node.op);
    ++indent_;
    if (node.operand) node.operand->accept(*this);
    --indent_;
}

void ASTPrinter::visit(IfExpr& node) {
    line("IfExpr");
    ++indent_;
    if (node.cond) node.cond->accept(*this);
    if (node.thenBlock) node.thenBlock->accept(*this);
    if (node.elseBlock) node.elseBlock->accept(*this);
    --indent_;
}

void ASTPrinter::visit(WhileExpr& node) {
    line("WhileExpr");
    ++indent_;
    if (node.cond) node.cond->accept(*this);
    if (node.body) node.body->accept(*this);
    --indent_;
}

void ASTPrinter::visit(LoopExpr& node) {
    line("LoopExpr");
    ++indent_;
    if (node.body) node.body->accept(*this);
    --indent_;
}

void ASTPrinter::visit(ReturnExpr& node) {
    line("ReturnExpr");
    ++indent_;
    if (node.value) node.value->accept(*this);
    --indent_;
}

void ASTPrinter::visit(BreakExpr& node) {
    line("BreakExpr");
    ++indent_;
    if (node.value) node.value->accept(*this);
    --indent_;
}

void ASTPrinter::visit(ContinueExpr& node) {
    line("ContinueExpr");
}

void ASTPrinter::visit(CallExpr& node) {
    line("CallExpr");
    ++indent_;
    if (node.callee) node.callee->accept(*this);
    for (auto* arg : node.args) {
        if (arg) arg->accept(*this);
    }
    --indent_;
}

void ASTPrinter::visit(MacroStmt& node) {
    line("MacroStmt " + node.name + "(" + node.argsText + ")");
}

void ASTPrinter::visit(StructDef& node) {
    std::string s = "StructDef " + node.name + " {";
    for (size_t i = 0; i < node.fields.size(); ++i) {
        if (i) s += ", ";
        s += node.fields[i].name + ": " + node.fields[i].typeName;
    }
    line(s + "}");
}

void ASTPrinter::visit(ImplBlock& node) {
    line("ImplBlock " + node.typeName);
    ++indent_;
    for (auto* m : node.methods) {
        if (m) m->accept(*this);
    }
    --indent_;
}

void ASTPrinter::visit(StructLiteralExpr& node) {
    line("StructLiteral " + node.name);
    ++indent_;
    for (auto& [fname, value] : node.fields) {
        line(fname + ":");
        ++indent_;
        if (value) value->accept(*this);
        --indent_;
    }
    --indent_;
}

void ASTPrinter::visit(FieldExpr& node) {
    line("FieldExpr ." + node.field);
    ++indent_;
    if (node.object) node.object->accept(*this);
    --indent_;
}

void ASTPrinter::visit(MethodCallExpr& node) {
    line("MethodCall ." + node.method);
    ++indent_;
    if (node.receiver) node.receiver->accept(*this);
    for (auto* arg : node.args) {
        if (arg) arg->accept(*this);
    }
    --indent_;
}

void ASTPrinter::visit(ArrayExpr& node) {
    if (node.repeatValue) {
        line("ArrayExpr [value; count]");
        ++indent_;
        node.repeatValue->accept(*this);
        if (node.repeatCount) node.repeatCount->accept(*this);
        --indent_;
    } else {
        line("ArrayExpr");
        ++indent_;
        for (auto* e : node.elements) {
            if (e) e->accept(*this);
        }
        --indent_;
    }
}

void ASTPrinter::visit(IndexExpr& node) {
    line("IndexExpr");
    ++indent_;
    if (node.array) node.array->accept(*this);
    if (node.index) node.index->accept(*this);
    --indent_;
}

void ASTPrinter::visit(ConstDef& node) {
    line("ConstDef " + node.name + " : " + node.typeName);
    ++indent_;
    if (node.value) node.value->accept(*this);
    --indent_;
}

void ASTPrinter::visit(StaticDef& node) {
    line(std::string("StaticDef ") + (node.isMut ? "mut " : "") + node.name + " : " + node.typeName);
    ++indent_;
    if (node.value) node.value->accept(*this);
    --indent_;
}
