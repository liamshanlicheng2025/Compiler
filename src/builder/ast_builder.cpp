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
    auto* node = new Crate();
    for (auto* itemCtx : ctx -> item()){
        auto result = visit(itemCtx);
        Item* resultItem = std::any_cast<Item*>(result);
        node -> items.push_back(resultItem);
    }
    return node;
}

std::any AstBuilder::visitFunction_(RustParser::Function_Context* ctx) {
    // 伪代码：
    //   auto* node = new Function();
    //   node->name = 函数名（提示：ctx->identifier()->getText()）
    //   node->body = any_cast<Block*>( visit(ctx->blockExpression()) )
    //   return node;
    // 注意：参数和返回类型先跳过，之后再加
    auto* node = new Function();
    node -> name = ctx -> identifier() -> getText();
    node -> body = std::any_cast<Block*>(visit(ctx -> blockExpression()));
    return static_cast<Item*>(node);
}

std::any AstBuilder::visitBlockExpression(RustParser::BlockExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new Block();
    //   解析树里 block 的语句藏在 statements 规则下面，
    //   找到它之后遍历其中的每个 statement，visit + any_cast<Stmt*>
    //   return node;
    auto* node = new Block();
    if (ctx -> statements()){
        for (auto* stmt : ctx -> statements() -> statement()){
            auto result = visit(stmt);
            Stmt* resultStmt = std::any_cast<Stmt*>(result);
            node -> statements.push_back(resultStmt);
        }
    }
    return node;
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
    auto* node = new LetStmt();
    node -> name = ctx -> patternNoTopAlt() -> patternWithoutRange() -> identifierPattern() -> identifier() -> getText();
    if (ctx ->type_()) node -> typeName = ctx -> type_() -> getText();
    if (ctx -> expression()) node -> init = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Stmt*>(node);
}

std::any AstBuilder::visitLiteralExpression(RustParser::LiteralExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new Literal();
    //   node->text = ctx->getText();
    //   return node;
    auto* node = new Literal();
    node -> text = ctx -> getText();
    return static_cast<Expr*>(node);
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
    auto* node = new BinaryExpr();
    node -> lhs = std::any_cast<Expr*>(visit(ctx -> expression(0)));
    node -> rhs = std::any_cast<Expr*>(visit(ctx -> expression(1)));
    if (ctx -> STAR()) node -> op = "*";
    if (ctx -> PLUS()) node -> op = "+";
    if (ctx -> MINUS()) node -> op = "-";
    if (ctx -> SLASH()) node -> op = "/";
    if (ctx -> PERCENT()) node -> op = "%";
    if (ctx -> shl()) node -> op = "<<";
    if (ctx -> shr()) node -> op = ">>";
    if (ctx -> AND()) node -> op = "&";
    if (ctx -> OR()) node -> op = "|";
    if (ctx -> CARET()) node -> op = "^";
    return static_cast<Expr*>(node);
}

// ============================================================
// 第 1 波：表达式补全
// ============================================================

std::any AstBuilder::visitPathExpression_(RustParser::PathExpression_Context* ctx) {
    // 伪代码：
    //   auto* node = new PathExpr();
    //   穿透链路：pathExpression → pathInExpression → pathExprSegment(0)
    //             → pathIdentSegment → identifier
    //   先只取第一段：pathExprSegment(0) 的文本（多段 :: 路径之后处理）
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitNegationExpression(RustParser::NegationExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new UnaryExpr();
    //   op: 判断 ctx->MINUS() / ctx->NOT() 哪个非空
    //   operand: visit(ctx->expression())
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitComparisonExpression(RustParser::ComparisonExpressionContext* ctx) {
    // 伪代码：复用 BinaryExpr
    //   lhs / rhs 同 Arithmetic 的取法
    //   op: ctx->comparisonOperator()->getText()  （运算符在子规则里，一步拿到）
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitAssignmentExpression(RustParser::AssignmentExpressionContext* ctx) {
    // 伪代码：复用 BinaryExpr，op 存 "="
    //   注意 g4 里这条是 expression EQ expression，左右取法同 Arithmetic
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitExpressionStatement(RustParser::ExpressionStatementContext* ctx) {
    // 伪代码：
    //   auto* node = new ExprStmt();
    //   这个规则有两个分支：expression SEMI ｜ expressionWithBlock SEMI?
    //   取 ctx->expression() 或 ctx->expressionWithBlock()（判空），
    //   visit 之后 any_cast<Expr*> 包进 ExprStmt
    //   return static_cast<Stmt*>(node);
    // 提示：if/while/loop 走的是 expressionWithBlock 分支，
    // 它们 visit 完返回的也是 Expr*，同样用 ExprStmt 包
    return static_cast<Stmt*>(nullptr);  // TODO(你)
}

// ============================================================
// 第 2 波：控制流
// ============================================================

std::any AstBuilder::visitIfExpression(RustParser::IfExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new IfExpr();
    //   cond:      visit(ctx->expression())
    //   thenBlock: visit(ctx->blockExpression(0))
    //   else:      ctx->KW_ELSE() 非空才有 else 分支；
    //              else 后面可能是 blockExpression(1)，也可能是嵌套的
    //              ifExpression（else if 链）——嵌套时 new 一个 Block
    //              把 IfExpr 包成 ExprStmt 塞进去，保持 elseBlock 类型一致
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitPredicateLoopExpression(RustParser::PredicateLoopExpressionContext* ctx) {
    // 伪代码（while）：
    //   auto* node = new WhileExpr();
    //   cond: visit(ctx->expression())，body: visit(ctx->blockExpression())
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitInfiniteLoopExpression(RustParser::InfiniteLoopExpressionContext* ctx) {
    // 伪代码（loop { }）：
    //   auto* node = new LoopExpr();
    //   body: visit(ctx->blockExpression())
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitReturnExpression(RustParser::ReturnExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new ReturnExpr();
    //   value: ctx->expression() 可空（裸 return;），判空后 visit
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitBreakExpression(RustParser::BreakExpressionContext* ctx) {
    // 伪代码：同 ReturnExpr，value 可空
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitContinueExpression(RustParser::ContinueExpressionContext* ctx) {
    // 伪代码：new ContinueExpr()，没有孩子，一行返回
    return static_cast<Expr*>(nullptr);  // TODO(你)
}
