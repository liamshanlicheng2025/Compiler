#include "builder/ast_builder.h"

// ============================================================
// 每个 visit 的通用套路：
//   1. new 出对应的 AST 节点
//   2. 从 ctx 里挖信息（提示见各函数注释）
//   3. 对子节点调用 visit()，把 std::any 结果 any_cast 成约定类型
//   4. 返回节点指针（std::any 自动包裹）
//
// 挖信息的通用手法：先跑一遍 ./build/compiler tests/xxx.rs 看解析树，
// 然后打开 build/generated/RustParser.h 找到对应 Context 类，
// 它的成员函数（如 ctx->identifier()、ctx->expression(i)）
// 就是解析树里那些孩子节点的访问入口。
// ============================================================

Crate* AstBuilder::build(RustParser::CrateContext* ctx) {
    return std::any_cast<Crate*>(visitCrate(ctx));
}

std::any AstBuilder::visitCrate(RustParser::CrateContext* ctx) {
    // 伪代码：
    //   auto* node = new Crate();
    //   遍历 ctx->item()，对每个 item 调 visit()，
    //     结果 any_cast<Item*> 后 push 进 node->items
    //   return node;
    return static_cast<Crate*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitFunction_(RustParser::Function_Context* ctx) {
    // 伪代码：
    //   auto* node = new Function();
    //   node->name = 函数名（提示：ctx->identifier()->getText()）
    //   node->body = any_cast<Block*>( visit(ctx->blockExpression()) )
    //   return node;
    // 注意：参数和返回类型先跳过，之后再加
    return static_cast<Function*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitBlockExpression(RustParser::BlockExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new Block();
    //   解析树里 block 的语句藏在 statements 规则下面，
    //   找到它之后遍历其中的每个 statement，visit + any_cast<Stmt*>
    //   return node;
    return static_cast<Block*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitLetStatement(RustParser::LetStatementContext* ctx) {
    // 伪代码：
    //   auto* node = new LetStmt();
    //   变量名：要往 pattern 规则里钻——对照解析树：
    //     letStatement → patternNoTopAlt → patternWithoutRange
    //                  → identifierPattern → identifier
    //     中间几层没覆盖 visit 的话，想想怎么穿透它们拿到 identifier
    //     （提示：可以直接对某层 ctx 调 ->getText()，也可以研究基类的
    //      aggregateResult 默认行为让结果自动透传）
    //   类型：ctx->type_() 可能为空；非空时先 ->getText() 存字符串
    //   初值：ctx->expression() 可能为空；非空时 visit + any_cast<Expr*>
    //   return node;
    return static_cast<LetStmt*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitLiteralExpression(RustParser::LiteralExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new Literal();
    //   node->text = ctx->getText();
    //   return node;
    return static_cast<Literal*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitArithmeticOrLogicalExpression(
    RustParser::ArithmeticOrLogicalExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new BinaryExpr();
    //   node->lhs = any_cast<Expr*>( visit(ctx->expression(0)) );
    //   node->rhs = any_cast<Expr*>( visit(ctx->expression(1)) );
    //   运算符：这个标签合并了 * / % + - << >> & | ^ 多种运算，
    //   逐个判断 ctx->STAR()、ctx->PLUS() …… 哪个非空，
    //   非空的那个 ->getText() 就是运算符
    //   return node;
    return static_cast<BinaryExpr*>(nullptr);  // TODO(你)
}
