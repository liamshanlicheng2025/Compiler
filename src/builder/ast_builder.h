#pragma once

// ============================================================
// AstBuilder：把 ANTLR 解析树翻译成你自己的 AST（本阶段的核心）
//
// 工作方式：继承 ANTLR 生成的 RustParserBaseVisitor，
// 只覆盖你关心的 visit 方法；没覆盖的走基类默认行为。
//
// 约定（全项目统一，否则 std::any 会炸得很难查）：
//   - visit 表达式类规则  → 返回 Expr*
//   - visit 语句类规则    → 返回 Stmt*
//   - visit 顶层条目规则  → 返回 Item*
//   （std::any 会自动包裹指针，return 节点指针即可）
// ============================================================

#include "RustParserBaseVisitor.h"
#include "ast/ast.h"

class AstBuilder : public RustParserBaseVisitor {
public:
    // 入口：解析树的 crate 根节点进，你的 Crate 节点出
    Crate* build(RustParser::CrateContext* ctx);

    // ---- 第一个里程碑要实现的 6 个 visit ----
    std::any visitCrate(RustParser::CrateContext* ctx) override;
    std::any visitFunction_(RustParser::Function_Context* ctx) override;
    std::any visitBlockExpression(RustParser::BlockExpressionContext* ctx) override;
    std::any visitLetStatement(RustParser::LetStatementContext* ctx) override;
    std::any visitLiteralExpression(RustParser::LiteralExpressionContext* ctx) override;
    std::any visitArithmeticOrLogicalExpression(RustParser::ArithmeticOrLogicalExpressionContext* ctx) override;

    // ---- 之后扩展时在这里加（对照解析树里出现的规则名）----
    // std::any visitIfExpression(...) override;
    // std::any visitWhileExpression(...) override;
    // std::any visitPathExpression_(...) override;   // 变量引用，很快就需要
    // ...
};
