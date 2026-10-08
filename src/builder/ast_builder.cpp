#include "builder/ast_builder.h"

// ============================================================
// AstBuilder：把 ANTLR 解析树翻译成自定义 AST。
//
// 约定：
//   - visit 表达式类规则 → 返回 Expr*；语句类 → Stmt*；顶层条目 → Item*
//     （返回前统一 static_cast 成基类指针，std::any 不做继承转换）
//   - g4 里带 ? 的元素生成的访问函数可能返回 nullptr，使用前判空
//   - 每个 Context 类的成员函数即解析树孩子节点的访问入口，
//     定义见 build/generated/RustParser.h
// ============================================================

Crate* AstBuilder::build(RustParser::CrateContext* ctx) {
    return std::any_cast<Crate*>(visitCrate(ctx));
}

// 整个源文件：遍历所有顶层条目（函数、struct、impl、const 等）
std::any AstBuilder::visitCrate(RustParser::CrateContext* ctx) {
    auto* node = new Crate();
    for (auto* itemCtx : ctx -> item()){
        auto result = visit(itemCtx);
        // 防御：未覆盖的条目会产生空 any，报警告跳过而不是让 any_cast 崩掉
        if (!result.has_value()) {
            std::cerr << "[ast] 未处理的顶层条目: " << itemCtx->getText() << std::endl;
            continue;
        }
        Item* resultItem = std::any_cast<Item*>(result);
        node -> items.push_back(resultItem);
    }
    return node;
}

// 函数定义：名字、参数列表（含 selfParam 检测）、返回类型、函数体
std::any AstBuilder::visitFunction_(RustParser::Function_Context* ctx) {
    auto* node = new Function();
    node -> name = ctx -> identifier() -> getText();
    if (ctx -> functionParameters()){
        // selfParam（&self / &mut self）不在 functionParam() 列表里，是独立入口；
        // 方法调用时 receiver 是隐式 self 实参，CodeGen 要给它留第一个参数槽位
        if (auto* sp = ctx->functionParameters()->selfParam()) {
            node->isMethod = true;
            // shorthandSelf: (AND lifetime?)? KW_MUT? KW_SELFVALUE
            node->selfMut = (sp->shorthandSelf() && sp->shorthandSelf()->KW_MUT())
                            || sp->getText().find("mut") != std::string::npos;
        }
        for (auto* fp : ctx -> functionParameters() ->functionParam()){
            auto* pat = fp -> functionParamPattern();
            Param p;
            p.name = pat -> pattern() -> getText();
            p.typeName = pat -> type_() -> getText();
            node -> params.push_back(p);
        }
    }
    if (ctx -> functionReturnType()) node -> returnType = ctx -> functionReturnType() -> type_() -> getText();
    node -> body = std::any_cast<Block*>(visit(ctx -> blockExpression()));
    return static_cast<Item*>(node);
}

// 语句块 { stmt* tailExpr? }：语句列表 + 可选的尾表达式（块的值）
std::any AstBuilder::visitBlockExpression(RustParser::BlockExpressionContext* ctx) {
    auto* node = new Block();
    if (ctx -> statements()){
        for (auto* stmt : ctx -> statements() -> statement()){
            auto result = visit(stmt);
            // 防御：未覆盖的语句结构会产生空 any，报警告跳过
            if (!result.has_value()) {
                std::cerr << "[ast] 未处理的语句: " << stmt->getText() << std::endl;
                continue;
            }
            Stmt* resultStmt = std::any_cast<Stmt*>(result);
            node -> statements.push_back(resultStmt);
        }
        // 尾表达式：fn f() -> i32 { 1 + 2 } 里的 1 + 2
        if (ctx -> statements() -> expression())
            node -> tailExpr = std::any_cast<Expr*>(visit(ctx -> statements() -> expression()));
    }
    return node;
}

// let 声明：变量名往 pattern 规则链里钻，类型标注和初值均可选
std::any AstBuilder::visitLetStatement(RustParser::LetStatementContext* ctx) {
    auto* node = new LetStmt();
    // 链路：letStatement → patternNoTopAlt → patternWithoutRange → identifierPattern → identifier
    node -> name = ctx -> patternNoTopAlt() -> patternWithoutRange() -> identifierPattern() -> identifier() -> getText();
    if (ctx ->type_()) node -> typeName = ctx -> type_() -> getText();
    if (ctx -> expression()) node -> init = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Stmt*>(node);
}

// 字面量：1、true、"hello" 等，原文存进 text
std::any AstBuilder::visitLiteralExpression(RustParser::LiteralExpressionContext* ctx) {
    auto* node = new Literal();
    node -> text = ctx -> getText();
    return static_cast<Expr*>(node);
}

// 算术/位运算二元表达式：* / % + - << >> & | ^，该标签合并多种运算，
// 哪个 token 访问函数非空就是哪个运算符
std::any AstBuilder::visitArithmeticOrLogicalExpression(
    RustParser::ArithmeticOrLogicalExpressionContext* ctx) {
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

// 路径表达式（变量/函数引用）：遍历所有段用 :: 拼接，支持 Point::new 等多段路径
std::any AstBuilder::visitPathExpression_(RustParser::PathExpression_Context* ctx) {
    auto* node = new PathExpr();
    std::string path;
    for (auto* seg : ctx->pathExpression()->pathInExpression()->pathExprSegment()) {
        if (!path.empty()) path += "::";
        path += seg->getText();
    }
    node->name = path;
    return static_cast<Expr*>(node);
}

// 一元运算：-x、!x
std::any AstBuilder::visitNegationExpression(RustParser::NegationExpressionContext* ctx) {
    auto* node = new UnaryExpr();
    if (ctx -> MINUS()) node -> op = "-";
    if (ctx -> NOT()) node -> op = "!";
    node -> operand = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

// 比较表达式：==、<、> 等，运算符在 comparisonOperator 子规则里
std::any AstBuilder::visitComparisonExpression(RustParser::ComparisonExpressionContext* ctx) {
    auto* node = new BinaryExpr();
    node -> lhs = std::any_cast<Expr*>(visit(ctx -> expression(0)));
    node -> rhs = std::any_cast<Expr*>(visit(ctx -> expression(1)));
    node -> op = ctx -> comparisonOperator() -> getText();
    return static_cast<Expr*>(node);
}

// 赋值表达式：复用 BinaryExpr，op = "="
std::any AstBuilder::visitAssignmentExpression(RustParser::AssignmentExpressionContext* ctx) {
    auto* node = new BinaryExpr();
    node -> lhs = std::any_cast<Expr*>(visit(ctx -> expression(0)));
    node -> rhs = std::any_cast<Expr*>(visit(ctx -> expression(1)));
    node -> op = "=";
    return static_cast<Expr*>(node);
}

// 括号表达式：(expr)，括号只是分组，直接穿透返回内层表达式的节点
// （官方测试里出现 while (turn < 9) 这种带括号条件，不覆盖会崩）
std::any AstBuilder::visitGroupedExpression(RustParser::GroupedExpressionContext* ctx) {
    return visit(ctx->expression());
}

// 表达式语句：普通表达式（带分号）和 if/while/loop 等带块表达式（分号可选）
// 统一包成 ExprStmt
std::any AstBuilder::visitExpressionStatement(RustParser::ExpressionStatementContext* ctx) {
    auto* node = new ExprStmt();
    if (ctx -> expression()) node -> expr = std::any_cast<Expr*>(visit(ctx -> expression()));
    if (ctx -> expressionWithBlock()) node -> expr = std::any_cast<Expr*>(visit(ctx -> expressionWithBlock()));
    return static_cast<Stmt*>(node);
}

// if 表达式：else 分支可能是块（blockExpression(1)）或嵌套 if（else if 链），
// 嵌套时包一层只含该 IfExpr 的 Block，保持 elseBlock 类型一致
std::any AstBuilder::visitIfExpression(RustParser::IfExpressionContext* ctx) {
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

// while 循环
std::any AstBuilder::visitPredicateLoopExpression(RustParser::PredicateLoopExpressionContext* ctx) {
    auto* node = new WhileExpr();
    node -> cond = std::any_cast<Expr*>(visit(ctx -> expression()));
    node -> body = std::any_cast<Block*>(visit(ctx -> blockExpression()));
    return static_cast<Expr*>(node);
}

// loop 无限循环
std::any AstBuilder::visitInfiniteLoopExpression(RustParser::InfiniteLoopExpressionContext* ctx) {
    auto* node = new LoopExpr();
    node -> body = std::any_cast<Block*>(visit(ctx -> blockExpression()));
    return static_cast<Expr*>(node);
}

// return，值可空（裸 return;）
std::any AstBuilder::visitReturnExpression(RustParser::ReturnExpressionContext* ctx) {
    auto* node = new ReturnExpr();
    if (ctx -> expression()) node -> value = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

// break，值可空
std::any AstBuilder::visitBreakExpression(RustParser::BreakExpressionContext* ctx) {
    auto* node = new BreakExpr();
    if (ctx -> expression()) node -> value = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

// continue，无孩子
std::any AstBuilder::visitContinueExpression(RustParser::ContinueExpressionContext* ctx) {
    return static_cast<Expr*>(new ContinueExpr());
}

// 语句级宏调用（println! 等）：参数是 tokenTree 原始 token 流，
// 第一版拼接原文存储，CodeGen 阶段再解析
std::any AstBuilder::visitMacroInvocationSemi(RustParser::MacroInvocationSemiContext* ctx) {
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

// 函数调用：左递归后缀，expression() 是被调者，callParams 可空（foo() 无实参）
std::any AstBuilder::visitCallExpression(RustParser::CallExpressionContext* ctx) {
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

// struct 定义：名字 + 字段列表（字段复用 Param 结构）
std::any AstBuilder::visitStructStruct(RustParser::StructStructContext* ctx) {
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

// impl 块：目标类型 + 方法列表（跳过非函数的 associatedItem，如 const）
std::any AstBuilder::visitInherentImpl(RustParser::InherentImplContext* ctx) {
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

// 结构体字面量 Point { x: 1, y: 2 }，暂不支持 tuple/unit 结构体形式
std::any AstBuilder::visitStructExpression_(RustParser::StructExpression_Context* ctx) {
    auto* node = new StructLiteralExpr();
    node -> name = ctx -> structExpression() -> structExprStruct() -> pathInExpression() -> getText();
    if (ctx -> structExpression() -> structExprStruct() -> structExprFields()){
        for (auto* field : ctx -> structExpression() -> structExprStruct() -> structExprFields() -> structExprField()){
            // 修正：structExprField 允许简写 Point { x }（g4:585），此时 expression() 为空，
            // 直接 visit(nullptr) 会段错误；简写等价于 x: x，补一个同名 PathExpr
            Expr* value;
            if (field -> expression())
                value = std::any_cast<Expr*>(visit(field -> expression()));
            else {
                auto* shorthand = new PathExpr();
                shorthand -> name = field -> identifier() -> getText();
                value = static_cast<Expr*>(shorthand);
            }
            node -> fields.push_back({field -> identifier() -> getText(), value});
        }
    }
    return static_cast<Expr*>(node);
}

// 字段访问：p.x
std::any AstBuilder::visitFieldExpression(RustParser::FieldExpressionContext* ctx) {
    auto* node = new FieldExpr();
    node -> object = std::any_cast<Expr*>(visit(ctx -> expression()));
    node -> field = ctx -> identifier() -> getText();
    return static_cast<Expr*>(node);
}

// 方法调用：p.shift(3)，receiver 不含进 args
std::any AstBuilder::visitMethodCallExpression(RustParser::MethodCallExpressionContext* ctx) {
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

// 复合赋值：a += b，运算符在 compoundAssignOperator 子规则里
// （修复记录：此前未覆盖，默认穿透导致整个表达式被静默替换成右操作数）
std::any AstBuilder::visitCompoundAssignmentExpression(
    RustParser::CompoundAssignmentExpressionContext* ctx) {
    auto* node = new BinaryExpr();
    node->lhs = std::any_cast<Expr*>(visit(ctx->expression(0)));
    node->rhs = std::any_cast<Expr*>(visit(ctx->expression(1)));
    node->op = ctx->compoundAssignOperator()->getText();
    return static_cast<Expr*>(node);
}

// 数组字面量：列举 [1,2,3] 与重复 [0; 5] 两种形式，
// 用 arrayElements 里 SEMI 是否非空区分（重复形式恰好两个 expression，中间是分号）
std::any AstBuilder::visitArrayExpression(RustParser::ArrayExpressionContext* ctx) {
    auto* node = new ArrayExpr();
    if (ctx -> arrayElements()){
        if (ctx -> arrayElements() -> SEMI()){
            node -> repeatValue = std::any_cast<Expr*>(visit(ctx -> arrayElements() -> expression(0)));
            node -> repeatCount = std::any_cast<Expr*>(visit(ctx -> arrayElements() -> expression(1)));
        } else {
            for (auto* expr : ctx -> arrayElements() -> expression()){
                node -> elements.push_back(std::any_cast<Expr*>(visit(expr)));
            }
        }
    }
    return static_cast<Expr*>(node);
}

// 下标访问：a[i]
std::any AstBuilder::visitIndexExpression(RustParser::IndexExpressionContext* ctx) {
    auto* node = new IndexExpr();
    node -> array = std::any_cast<Expr*>(visit(ctx -> expression(0)));
    node -> index = std::any_cast<Expr*>(visit(ctx -> expression(1)));
    return static_cast<Expr*>(node);
}

// 引用：&x / &mut x，复用 UnaryExpr
std::any AstBuilder::visitBorrowExpression(RustParser::BorrowExpressionContext* ctx) {
    auto* node = new UnaryExpr();
    node -> op = "&";
    if (ctx -> KW_MUT()) node -> op = "&mut";
    node -> operand = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

// 解引用：*p，复用 UnaryExpr
std::any AstBuilder::visitDereferenceExpression(RustParser::DereferenceExpressionContext* ctx) {
    auto* node = new UnaryExpr();
    node -> op = "*";
    node -> operand = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Expr*>(node);
}

// 全局常量：const MAX: i32 = 100;
std::any AstBuilder::visitConstantItem(RustParser::ConstantItemContext* ctx) {
    auto* node = new ConstDef();
    node -> name = ctx -> identifier() -> getText();
    node -> typeName = ctx -> type_() -> getText();
    if (ctx -> expression()) node -> value = std::any_cast<Expr*>(visit(ctx -> expression()));
    return static_cast<Item*>(node);
}

// 全局静态量：static mut COUNT: i32 = 0;
std::any AstBuilder::visitStaticItem(RustParser::StaticItemContext* ctx) {
    auto* node = new StaticDef();
    node -> name = ctx -> identifier() -> getText();
    node -> typeName = ctx -> type_() -> getText();
    if (ctx -> expression()) node -> value = std::any_cast<Expr*>(visit(ctx -> expression()));
    node -> isMut = (ctx -> KW_MUT())? true : false;
    return static_cast<Item*>(node);
}

// ============================================================
// enum / match
// ============================================================

std::any AstBuilder::visitEnumeration(RustParser::EnumerationContext* ctx) {
    // 伪代码（enum Cell { X, O } / enum Shape { Circle(f64), Empty }）：
    //   auto* node = new EnumDef();
    //   name: ctx->identifier()->getText()
    //   variants: ctx->enumItems() 可空（空 enum）；
    //     遍历 ->enumItem() vector，每个 enumItem：
    //       名字 = identifier()->getText()
    //       若 enumItemTuple() 非空 → tuple 式负载：
    //         链路 enumItemTuple → tupleFields → tupleField（vector）→ type_()->getText()
    //       enumItemStruct / enumItemDiscriminant（= 显式值）课程测试基本不出现，可跳过
    //   return static_cast<Item*>(node);
    auto node = new EnumDef();
    node -> name = ctx -> identifier() -> getText();
    if (ctx -> enumItems()){
        // 修正：enumItems() 返回单个上下文（不是 vector），列表在它下面的 enumItem() 方法里
        for (auto* item : ctx -> enumItems() -> enumItem()){
            // 修正：variants 是 vector<EnumVariant>（按值存），不用 new
            EnumVariant variant;
            variant.name = item -> identifier() -> getText();
            if (auto* tup = item -> enumItemTuple()){
                // 修正：tupleFields/tupleField 是方法（要带 ()）；空括号 () 时 tupleFields() 为空
                if (auto* fields = tup -> tupleFields()){
                    for (auto* field : fields -> tupleField()){
                        variant.payloadTypes.push_back(field -> type_() -> getText());
                    }
                }
            }
            node -> variants.push_back(variant);
        }
    }
    return static_cast<Item*>(node);
}

std::any AstBuilder::visitMatchExpression(RustParser::MatchExpressionContext* ctx) {
    // 伪代码：
    //   auto* node = new MatchExpr();
    //   scrutinee: visit(ctx->expression())
    //   arms: ctx->matchArms() 可空；注意 matchArms 规则结构（RustParser.g4:692）：
    //     (matchArm FATARROW matchArmExpression)* matchArm FATARROW expression COMMA?
    //     即前 n-1 个分支的体在 matchArmExpression(i) 里，
    //     最后一个分支的体单独在 matchArms->expression() 里
    //   每个 matchArm：patternText = matchArm(i)->pattern()->getText()
    //   matchArmExpression 有两个备选：expression（普通）或
    //     expressionWithBlock（块）——块的情况 visit 返回 Block*，
    //     存进 MatchArmNode::bodyBlock（与 body 二选一）
    //   return static_cast<Expr*>(node);
    auto* node = new MatchExpr();
    node -> scrutinee = std::any_cast<Expr*>(visit(ctx -> expression()));
    if (auto* arms = ctx -> matchArms()){
        // 修正：matchArm 上下文里只有 pattern(+guard)，没有 expression；
        // 分支体在 matchArms 层——前 n-1 个在 matchArmExpression(i)，最后一个在 matchArms->expression()
        auto armCtxs = arms -> matchArm();
        for (size_t i = 0; i < armCtxs.size(); ++i){
            MatchArmNode arm;   // arms 是 vector<MatchArmNode>（按值存），不用 new
            arm.patternText = armCtxs[i] -> pattern() -> getText();
            // 修正：ANTLR 实际会把 => { ... } 解析进 matchArmExpression 的第一个备选
            // expression COMMA（因为 expression 规则自身包含 expressionWithBlock 备选），
            // 所以判断块式体不能看 matchArmExpression::expressionWithBlock()，
            // 要对拿到的 expression 上下文 dynamic_cast 成 ExpressionWithBlock_Context 再往里看
            auto fillBody = [&](RustParser::ExpressionContext* exprCtx){
                if (auto* ewbAlt = dynamic_cast<RustParser::ExpressionWithBlock_Context*>(exprCtx)){
                    auto* ewb = ewbAlt -> expressionWithBlock();
                    if (ewb -> blockExpression())
                        // 字面 { } 块存 bodyBlock（visitBlockExpression 返回 Block*）
                        arm.bodyBlock = std::any_cast<Block*>(visit(ewb -> blockExpression()));
                    else
                        // if/while/loop/嵌套 match：visit 返回 Expr*，存 body
                        arm.body = std::any_cast<Expr*>(visit(ewb));
                } else {
                    arm.body = std::any_cast<Expr*>(visit(exprCtx));
                }
            };
            if (i + 1 < armCtxs.size()){
                fillBody(arms -> matchArmExpression(i) -> expression());
            } else {
                // 最后一个分支体：单独在 matchArms->expression() 里（修正：原来漏了 visit()）
                fillBody(arms -> expression());
            }
            node -> arms.push_back(arm);
        }
    }
    return static_cast<Expr*>(node);
}
