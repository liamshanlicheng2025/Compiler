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
        // 开发期防御：未覆盖的 item（struct/impl/enum……）报警告并跳过
        if (!result.has_value()) {
            std::cerr << "[ast] 未处理的顶层条目: " << itemCtx->getText() << std::endl;
            continue;
        }
        Item* resultItem = std::any_cast<Item*>(result);
        node -> items.push_back(resultItem);
    }
    return node;
}

std::any AstBuilder::visitFunction_(RustParser::Function_Context* ctx) {
    auto* node = new Function();
    node -> name = ctx -> identifier() -> getText();
    if (ctx -> functionParameters()){
        for (auto* fp : ctx -> functionParameters() ->functionParam()){
            auto* pat = fp -> functionParamPattern();
            Param p;
            p.name = pat -> pattern() -> getText();
            p.typeName = pat -> type_() -> getText();
            node -> params.push_back(p);
        }
    }
    // 参数列表
    //   ctx->functionParameters() 可空（无参函数）；
    //   里面 ctx->...->functionParam() 是 vector，每个 functionParam 往下钻到
    //   functionParamPattern：pattern()->getText() 是参数名，type_()->getText() 是类型
    //   （pattern 里钻 identifier 的链路和 letStatement 一样）
    //   注意跳过 selfParam（impl 方法的 &self，第 4 波再处理）
    //
    //  返回类型
    //   ctx->functionReturnType() 可空；非空时 ->getText() 存进 returnType
    if (ctx -> functionReturnType()) node -> returnType = ctx -> functionReturnType() -> type_() -> getText();
    node -> body = std::any_cast<Block*>(visit(ctx -> blockExpression()));
    return static_cast<Item*>(node);
}

std::any AstBuilder::visitBlockExpression(RustParser::BlockExpressionContext* ctx) {
    auto* node = new Block();
    if (ctx -> statements()){
        for (auto* stmt : ctx -> statements() -> statement()){
            auto result = visit(stmt);
            // 开发期防御：未覆盖的语法结构会产生空 any，
            // 报警告并跳过，而不是让 any_cast 崩在半路
            if (!result.has_value()) {
                std::cerr << "[ast] 未处理的语句: " << stmt->getText() << std::endl;
                continue;
            }
            Stmt* resultStmt = std::any_cast<Stmt*>(result);
            node -> statements.push_back(resultStmt);
        }
        // 尾部表达式
        //   ctx->statements()->expression() 非空时，visit + any_cast<Expr*>
        //   存进 node->tailExpr（这就是 fn f() -> i32 { 1 + 2 } 里的 1 + 2）
        if (ctx -> statements() -> expression())
            node -> tailExpr = std::any_cast<Expr*>(visit(ctx -> statements() -> expression()));
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
    auto* node = new PathExpr();
    // 修复（多段路径）：原来只取 pathExprSegment(0)，会丢掉 :: 后面的段
    // （Point::new 变成 Point）。现在遍历所有段，用 :: 拼接完整路径。
    std::string path;
    for (auto* seg : ctx->pathExpression()->pathInExpression()->pathExprSegment()) {
        if (!path.empty()) path += "::";
        path += seg->getText();
    }
    node->name = path;
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitNegationExpression(RustParser::NegationExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new UnaryExpr();
    //   op: 判断 ctx->MINUS() / ctx->NOT() 哪个非空
    //   operand: visit(ctx->expression())
    //   return static_cast<Expr*>(node);
    auto* node = new UnaryExpr();
    if (ctx -> MINUS()) node -> op = "-";
    if (ctx -> NOT()) node -> op = "!";
    node -> operand = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitComparisonExpression(RustParser::ComparisonExpressionContext* ctx) {
    // 伪代码：复用 BinaryExpr
    //   lhs / rhs 同 Arithmetic 的取法
    //   op: ctx->comparisonOperator()->getText()  （运算符在子规则里，一步拿到）
    //   return static_cast<Expr*>(node);
    auto* node = new BinaryExpr();
    node -> lhs = std::any_cast<Expr*>(visit(ctx -> expression(0)));
    node -> rhs = std::any_cast<Expr*>(visit(ctx -> expression(1)));
    node -> op = ctx -> comparisonOperator() -> getText();
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitAssignmentExpression(RustParser::AssignmentExpressionContext* ctx) {
    // 伪代码：复用 BinaryExpr，op 存 "="
    //   注意 g4 里这条是 expression EQ expression，左右取法同 Arithmetic
    auto* node = new BinaryExpr();
    node -> lhs = std::any_cast<Expr*>(visit(ctx -> expression(0)));
    node -> rhs = std::any_cast<Expr*>(visit(ctx -> expression(1)));
    node -> op = "=";
    return static_cast<Expr*>(node);
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
    auto* node = new ExprStmt();
    if (ctx -> expression()) node -> expr = std::any_cast<Expr*>(visit(ctx -> expression()));
    if (ctx -> expressionWithBlock()) node -> expr = std::any_cast<Expr*>(visit(ctx -> expressionWithBlock()));
    return static_cast<Stmt*>(node);
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
    auto* node = new IfExpr();
    node -> cond = std::any_cast<Expr*>(visit(ctx -> expression()));
    node -> thenBlock = std::any_cast<Block*>(visit(ctx -> blockExpression(0)));
    if (ctx -> KW_ELSE()){
        if (ctx -> ifExpression()){
            auto* result = new Block();
            auto* stmt = new ExprStmt();
            stmt -> expr = std::any_cast<Expr*>(visit(ctx -> ifExpression()));
            result->statements.push_back(stmt);
            node -> elseBlock = result;
        } else {
            node -> elseBlock = std::any_cast<Block*>(visit(ctx -> blockExpression(1)));
        }
    }
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitPredicateLoopExpression(RustParser::PredicateLoopExpressionContext* ctx) {
    // 伪代码（while）：
    //   auto* node = new WhileExpr();
    //   cond: visit(ctx->expression())，body: visit(ctx->blockExpression())
    //   return static_cast<Expr*>(node);
    auto* node = new WhileExpr();
    node -> cond = std::any_cast<Expr*>(visit(ctx -> expression()));
    node -> body = std::any_cast<Block*>(visit(ctx -> blockExpression()));
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitInfiniteLoopExpression(RustParser::InfiniteLoopExpressionContext* ctx) {
    // 伪代码（loop { }）：
    //   auto* node = new LoopExpr();
    //   body: visit(ctx->blockExpression())
    //   return static_cast<Expr*>(node);
    auto* node = new LoopExpr();
    node -> body = std::any_cast<Block*>(visit(ctx -> blockExpression()));
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitReturnExpression(RustParser::ReturnExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new ReturnExpr();
    //   value: ctx->expression() 可空（裸 return;），判空后 visit
    //   return static_cast<Expr*>(node);
    auto* node = new ReturnExpr();
    if (ctx -> expression()) node -> value = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitBreakExpression(RustParser::BreakExpressionContext* ctx) {
    // 伪代码：同 ReturnExpr，value 可空
    auto* node = new BreakExpr();
    if (ctx -> expression()) node -> value = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitContinueExpression(RustParser::ContinueExpressionContext* ctx) {
    // 伪代码：new ContinueExpr()，没有孩子，一行返回
    return static_cast<Expr*>(new ContinueExpr());
}

std::any AstBuilder::visitMacroInvocationSemi(RustParser::MacroInvocationSemiContext* ctx) {
    // 语句级宏调用：println!("{}", x);
    // 宏名从 simplePath 拿；参数是 tokenTree*（原始 token 流，不是表达式），
    // 第一版把 token 文本拼起来整个存，CodeGen 阶段再解析
    auto* node = new MacroStmt();
    node->name = ctx->simplePath()->getText();
    std::string args;
    for (auto* tt : ctx->tokenTree()) {
        if (!args.empty()) args += " ";
        args += tt->getText();
    }
    node->argsText = args;
    return static_cast<Stmt*>(node);
}
// ============================================================
// 第 3 波：函数完整化
// ============================================================

std::any AstBuilder::visitCallExpression(RustParser::CallExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new CallExpr();
    //   callee: visit(ctx->expression())            ← 左递归：被调者
    //   args:   ctx->callParams() 可空（foo() 无实参）；
    //           非空时遍历 callParams()->expression() 这个 vector，
    //           逐个 visit + any_cast<Expr*> 塞进 args
    //   return static_cast<Expr*>(node);
    auto* node = new CallExpr();
    node -> callee = std::any_cast<Expr*>(visit(ctx -> expression()));
    if (ctx -> callParams()){
        for (auto* expr : ctx -> callParams() -> expression()){
            auto result = std::any_cast<Expr*>(visit(expr));
            node -> args.push_back(result);
        }
    }
    return static_cast<Expr*>(node);
}

// ============================================================
// 第 4 波：复合类型（struct / impl / 字段访问 / 方法调用）
// ============================================================

std::any AstBuilder::visitStructStruct(RustParser::StructStructContext* ctx) {
    // 伪代码：
    //   auto* node = new StructDef();
    //   name: ctx->identifier()->getText()
    //   fields: ctx->structFields() 可空（struct Foo; 无字段）；
    //           非空时遍历 structFields()->structField() vector，
    //           每个 structField 里 identifier() 是字段名、type_() 是类型，
    //           包成 Param 按值 push 进 fields
    //   return static_cast<Item*>(node);
    auto* node = new StructDef();
    node -> name = ctx -> identifier() -> getText();
    if (ctx -> structFields()){
        for (auto* field : ctx -> structFields() -> structField()){
            Param param;
            param.name = field -> identifier() -> getText();
            param.typeName = field -> type_() -> getText();
            node -> fields.push_back(param);
        }
    }
    return static_cast<Item*>(node);
}

std::any AstBuilder::visitInherentImpl(RustParser::InherentImplContext* ctx) {
    // 伪代码：
    //   auto* node = new ImplBlock();
    //   typeName: ctx->type_()->getText()
    //   methods: 遍历 ctx->associatedItem() vector，
    //            每个 associatedItem 里取 ->function_()（可能为空，
    //            比如 const 条目——判空跳过），
    //            visit(function_ ctx) + any_cast<Item*> 再 static_cast 到 Function*
    //   注意：方法的第一个参数可能是 selfParam（&self/&mut self），
    //   visitFunction_ 里遇到 selfParam() 非空时的处理要想清楚——
    //   方法调用时 receiver 就是隐式的 self 实参，这影响 CodeGen
    //   return static_cast<Item*>(node);
    auto* node = new ImplBlock();
    node -> typeName = ctx -> type_() -> getText();
    for (auto* item : ctx -> associatedItem()){
        if (item -> function_()){
            auto result = std::any_cast<Item*>(visit(item -> function_()));
            node -> methods.push_back(static_cast<Function*>(result));
        }
    }
    return static_cast<Item*>(node);
}

std::any AstBuilder::visitStructExpression_(RustParser::StructExpression_Context* ctx) {
    // 伪代码（结构体字面量 Point { x: 1, y: 2 }）：
    //   auto* node = new StructLiteralExpr();
    //   链路：ctx->structExpression()->structExprStruct()
    //         （另外两个备选 structExprTuple / structExprUnit 先不支持）
    //   name: structExprStruct->pathInExpression()->getText()
    //   fields: structExprStruct->structExprFields() 可空（Point{}）；
    //           遍历 ->structExprField() vector，
    //           每个里面 identifier() 是字段名、expression() 是初值
    //   return static_cast<Expr*>(node);
    auto* node = new StructLiteralExpr();
    node -> name = ctx -> structExpression() -> structExprStruct() -> pathInExpression() -> getText();
    if (ctx -> structExpression() -> structExprStruct() -> structExprFields()){
        for (auto* field : ctx -> structExpression() -> structExprStruct() -> structExprFields() -> structExprField()){
            node -> fields.push_back({field -> identifier() -> getText(), std::any_cast<Expr*>(visit(field -> expression()))});
        }
    }
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitFieldExpression(RustParser::FieldExpressionContext* ctx) {
    // 伪代码（p.x）：
    //   auto* node = new FieldExpr();
    //   object: visit(ctx->expression())      ← 左递归：点号左边
    //   field:  ctx->identifier()->getText()
    //   return static_cast<Expr*>(node);
    auto* node = new FieldExpr();
    node -> object = std::any_cast<Expr*>(visit(ctx -> expression()));
    node -> field = ctx -> identifier() -> getText();
    return static_cast<Expr*>(node);
}

std::any AstBuilder::visitMethodCallExpression(RustParser::MethodCallExpressionContext* ctx) {
    // 伪代码（p.shift(3)）：
    //   auto* node = new MethodCallExpr();
    //   receiver: visit(ctx->expression())
    //   method:   ctx->pathExprSegment()->getText()
    //   args:     ctx->callParams() 判空，遍历 ->expression() vector（同 CallExpr）
    //   return static_cast<Expr*>(node);
    auto* node = new MethodCallExpr();
    node -> receiver = std::any_cast<Expr*>(visit(ctx -> expression()));
    node -> method = ctx -> pathExprSegment() -> getText();
    if (ctx -> callParams()){
        for (auto* expr : ctx -> callParams() -> expression()){
            auto result = std::any_cast<Expr*>(visit(expr));
            node -> args.push_back(result);
        }
    }
    return static_cast<Expr*>(node);
}

// 修复（复合赋值）：新增，和 Arithmetic 同构。
// op 用 compoundAssignOperator 子规则一步拿到（+=、-=、*=……）。
std::any AstBuilder::visitCompoundAssignmentExpression(
    RustParser::CompoundAssignmentExpressionContext* ctx) {
    auto* node = new BinaryExpr();
    node->lhs = std::any_cast<Expr*>(visit(ctx->expression(0)));
    node->rhs = std::any_cast<Expr*>(visit(ctx->expression(1)));
    node->op = ctx->compoundAssignOperator()->getText();
    return static_cast<Expr*>(node);
}

// ============================================================
// 第 5 波：数组 / 引用 / 全局条目
// ============================================================

std::any AstBuilder::visitArrayExpression(RustParser::ArrayExpressionContext* ctx) {
    // 伪代码（数组字面量）：
    //   auto* node = new ArrayExpr();
    //   入口：ctx->arrayElements() 可空（[] 空数组）；
    //   规则有两个备选（RustParser.g4:555）：
    //     列举 [1,2,3] : expression (COMMA expression)* —— 遍历 expression() vector
    //     重复 [0; 5]  : expression SEMI expression     —— 恰好两个 expression 且中间是 SEMI
    //   区分方法：看 ctx->arrayElements()->SEMI() 是否非空
    //     非空 → expression(0) 进 repeatValue，expression(1) 进 repeatCount
    //     否则 → 全部进 elements
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitIndexExpression(RustParser::IndexExpressionContext* ctx) {
    // 伪代码（a[i]，左递归后缀）：
    //   array: visit(ctx->expression(0))，index: visit(ctx->expression(1))
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitBorrowExpression(RustParser::BorrowExpressionContext* ctx) {
    // 伪代码（&x / &mut x）：复用 UnaryExpr
    //   op: "&"；如果 ctx->KW_MUT() 非空则 "&mut"
    //   operand: visit(ctx->expression())
    //   return static_cast<Expr*>(node);
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitDereferenceExpression(RustParser::DereferenceExpressionContext* ctx) {
    // 伪代码（*p）：复用 UnaryExpr，op = "*"，operand = visit(ctx->expression())
    return static_cast<Expr*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitConstantItem(RustParser::ConstantItemContext* ctx) {
    // 伪代码（const MAX: i32 = 100;）：
    //   name:     ctx->identifier()->getText()
    //   typeName: ctx->type_()->getText()
    //   value:    ctx->expression() 判空后 visit
    //   return static_cast<Item*>(node);
    return static_cast<Item*>(nullptr);  // TODO(你)
}

std::any AstBuilder::visitStaticItem(RustParser::StaticItemContext* ctx) {
    // 伪代码（static mut COUNT: i32 = 0;）：同 ConstDef，
    //   多一个 isMut：看 ctx->KW_MUT() 是否非空
    //   return static_cast<Item*>(node);
    return static_cast<Item*>(nullptr);  // TODO(你)
}
