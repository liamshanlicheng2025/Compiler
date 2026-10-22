#include "builder/ast_builder.h"

#include <iostream>
#include <stdexcept>

// ============================================================
// AstBuilder：把 ANTLR 解析树翻译成自定义 AST。
//
// 约定：
//   - visit 表达式类规则 → 返回 ExprPtr；语句类 → StmtPtr；顶层条目 → ItemPtr
//     （返回前统一转成基类智能指针，std::any 不做继承转换）
//   - g4 里带 ? 的元素生成的访问函数可能返回 nullptr，使用前判空
//   - 每个 Context 类的成员函数即解析树孩子节点的访问入口，
//     定义见 build/generated/RustParser.h
// ============================================================

CratePtr AstBuilder::build(RustParser::CrateContext* ctx) {
    return std::any_cast<CratePtr>(visitCrate(ctx));
}

// ------------------------------------------------------------
// 类型标注解析：把 g4 的 type_ 子树翻译成结构化 Type
// （CR 后新增：类型不再存字符串原文）
// ------------------------------------------------------------

// visit 结果统一成 ExprPtr（块是 Expr 的子类，BlockPtr 可直接转）
ExprPtr AstBuilder::toExpr(std::any r) {
    if (auto* e = std::any_cast<ExprPtr>(&r)) return *e;
    if (auto* b = std::any_cast<BlockPtr>(&r)) return ExprPtr(*b);
    throw std::runtime_error("[ast] 期望表达式，实际节点类型不符");
}

Type AstBuilder::parseType(RustParser::Type_Context* ctx) {
    // type_ : typeNoBounds | implTraitType | traitObjectType
    // 课程范围只出现 typeNoBounds，其余报未知
    if (auto* nb = ctx->typeNoBounds()) return parseTypeNoBounds(nb);
    std::cerr << "[ast] 暂不支持的类型写法: " << ctx->getText() << std::endl;
    return Type{};
}

Type AstBuilder::parseTypeNoBounds(RustParser::TypeNoBoundsContext* ctx) {
    Type t;

    // &T / &mut T
    if (auto* ref = ctx->referenceType()) {
        t.kind = TypeKind::Ref;
        t.isMut = (ref->KW_MUT() != nullptr);
        t.elem = std::make_shared<Type>(parseTypeNoBounds(ref->typeNoBounds()));
        return t;
    }

    // [T; N]：长度在语法上是 expression，课程范围内是整数字面量，取原文转数字
    if (auto* arr = ctx->arrayType()) {
        t.kind = TypeKind::Array;
        t.elem = std::make_shared<Type>(parseType(arr->type_()));
        // 长度原文留给语义阶段解析（可能是 const 名，如 [i32; ARR]）；
        // 字面量这里直接算出，算不出（常量表达式）则 arrayLen=0 待解析
        t.arrayLenExpr = arr->expression()->getText();
        try {
            t.arrayLen = static_cast<std::size_t>(std::stoull(t.arrayLenExpr));
        } catch (...) { /* 语义阶段 resolveArrayLens 处理 */ }
        return t;
    }

    // (T)：括号穿透
    if (auto* par = ctx->parenthesizedType()) return parseType(par->type_());

    // ()：unit 类型（tupleType 的空调用形态）
    if (auto* tup = ctx->tupleType()) {
        if (tup->type_().empty()) { t.kind = TypeKind::Unit; return t; }
        std::cerr << "[ast] 元组类型不在课程范围: " << ctx->getText() << std::endl;
        return t;
    }

    // !：never 类型
    if (ctx->neverType()) { t.kind = TypeKind::Never; return t; }

    // 具名类型：基础类型映射到枚举，其余按 struct/enum 名字存 Named
    // 注意：单个类型名实际会被解析成 traitObjectTypeOneBound（traitBound → typePath），
    // 而不是直接的 typePath 备选，两条路都要走
    RustParser::TypePathContext* tp = ctx->typePath();
    if (auto* tro = ctx->traitObjectTypeOneBound())
        if (auto* tb = tro->traitBound()) tp = tb->typePath();
    if (tp) {
        std::string n = tp->getText();
        if (n == "i32")   t.kind = TypeKind::I32;
        else if (n == "u32")    t.kind = TypeKind::U32;
        else if (n == "isize")  t.kind = TypeKind::Isize;
        else if (n == "usize")  t.kind = TypeKind::Usize;
        else if (n == "f32")    t.kind = TypeKind::F32;
        else if (n == "f64")    t.kind = TypeKind::F64;
        else if (n == "bool")   t.kind = TypeKind::Bool;
        else if (n == "char")   t.kind = TypeKind::Char;
        else if (n == "str")    t.kind = TypeKind::Str;
        else { t.kind = TypeKind::Named; t.name = n; }
        return t;
    }

    std::cerr << "[ast] 暂不支持的类型写法: " << ctx->getText() << std::endl;
    return t;
}

// 整个源文件：遍历所有顶层条目（函数、struct、impl、const 等）
std::any AstBuilder::visitCrate(RustParser::CrateContext* ctx) {
    auto node = std::make_shared<Crate>();
    for (auto* itemCtx : ctx -> item()){
        auto result = visit(itemCtx);
        // 防御：未覆盖的条目会产生空 any，报警告跳过而不是让 any_cast 崩掉
        if (!result.has_value()) {
            std::cerr << "[ast] 未处理的顶层条目: " << itemCtx->getText() << std::endl;
            continue;
        }
        node -> items.push_back(std::any_cast<ItemPtr>(result));
    }
    return CratePtr(node);
}

// 函数定义：名字、参数列表（含 selfParam 检测）、返回类型、函数体
std::any AstBuilder::visitFunction_(RustParser::Function_Context* ctx) {
    auto node = std::make_shared<Function>();
    node -> name = ctx -> identifier() -> getText();
    node -> hasGenericParams = (ctx -> genericParams() != nullptr);
    if (ctx -> functionParameters()){
        // selfParam（&self / &mut self）不在 functionParam() 列表里，是独立入口；
        // 方法调用时 receiver 是隐式 self 实参，CodeGen 要给它留第一个参数槽位
        if (auto* sp = ctx->functionParameters()->selfParam()) {
            node->isMethod = true;
            // shorthandSelf: (AND lifetime?)? KW_MUT? KW_SELFVALUE
            node->selfMut = (sp->shorthandSelf() && sp->shorthandSelf()->KW_MUT())
                            || sp->getText().find("mut") != std::string::npos;
            // 按值 self：shorthandSelf 无 &（fn better(self, ...)）或 typedSelf
            node->selfByValue = (sp->typedSelf() != nullptr)
                            || (sp->shorthandSelf() && !sp->shorthandSelf()->AND());
        }
        for (auto* fp : ctx -> functionParameters() ->functionParam()){
            auto* pat = fp -> functionParamPattern();
            Param p;
            // 沿 pattern 链钻到 identifierPattern：fn f(mut n: i32) 的
            // 名字是 n，可变性存在 isMut
            auto* idpat = pat -> pattern() -> patternNoTopAlt(0)
                            -> patternWithoutRange() -> identifierPattern();
            p.name = idpat -> identifier() -> getText();
            p.isMut = idpat -> KW_MUT() != nullptr;
            p.type = parseType(pat -> type_());
            node -> params.push_back(p);
        }
    }
    if (ctx -> functionReturnType())
        node -> returnType = std::make_shared<Type>(parseType(ctx -> functionReturnType() -> type_()));
    node -> body = std::any_cast<BlockPtr>(visit(ctx -> blockExpression()));
    return ItemPtr(node);
}

// 语句块 { stmt* tailExpr? }：语句列表 + 可选的尾表达式（块的值）
std::any AstBuilder::visitBlockExpression(RustParser::BlockExpressionContext* ctx) {
    auto node = std::make_shared<Block>();
    if (ctx -> statements()){
        for (auto* stmt : ctx -> statements() -> statement()){
            auto result = visit(stmt);
            // 防御：未覆盖的语句结构会产生空 any，报警告跳过
            if (!result.has_value()) {
                std::cerr << "[ast] 未处理的语句: " << stmt->getText() << std::endl;
                continue;
            }
            // 语句位置可能是 item（g4: statement 含 item——嵌套 fn、局部 const）。
            // item 类 visit 返回 ItemPtr，包成 ItemStmt 再进语句列表
            if (auto* sp = std::any_cast<StmtPtr>(&result)) {
                node -> statements.push_back(*sp);
            } else if (auto* ip = std::any_cast<ItemPtr>(&result)) {
                auto stmt2 = std::make_shared<ItemStmt>();
                stmt2->item = *ip;
                node -> statements.push_back(StmtPtr(stmt2));
            } else {
                std::cerr << "[ast] 语句类型既非 Stmt 也非 Item: " << stmt->getText() << std::endl;
            }
        }
        // 尾表达式：fn f() -> i32 { 1 + 2 } 里的 1 + 2
        if (ctx -> statements() -> expression())
            node -> tailExpr = toExpr(visit(ctx -> statements() -> expression()));
    }
    return BlockPtr(node);
}

// let 声明：变量名往 pattern 规则链里钻，类型标注和初值均可选
std::any AstBuilder::visitLetStatement(RustParser::LetStatementContext* ctx) {
    auto node = std::make_shared<LetStmt>();
    // 链路：letStatement → patternNoTopAlt → patternWithoutRange → identifierPattern → identifier
    auto* idpat = ctx -> patternNoTopAlt() -> patternWithoutRange() -> identifierPattern();
    node -> name = idpat -> identifier() -> getText();
    node -> isMut = (idpat -> KW_MUT() != nullptr);   // let mut 的可变标志
    if (ctx ->type_()) node -> type = std::make_shared<Type>(parseType(ctx -> type_()));
    if (ctx -> expression()) node -> init = toExpr(visit(ctx -> expression()));
    return StmtPtr(node);
}

// 字面量：1、true、"hello" 等，原文存进 text
std::any AstBuilder::visitLiteralExpression(RustParser::LiteralExpressionContext* ctx) {
    auto node = std::make_shared<Literal>();
    node -> text = ctx -> getText();
    if (ctx->INTEGER_LITERAL()) node->kind = LiteralKind::Int;
    else if (ctx->FLOAT_LITERAL()) node->kind = LiteralKind::Float;
    else if (ctx->KW_TRUE() || ctx->KW_FALSE()) node->kind = LiteralKind::Bool;
    else if (ctx->CHAR_LITERAL()) node->kind = LiteralKind::Char;
    else node->kind = LiteralKind::Str;
    return ExprPtr(node);
}

// 算术/位运算二元表达式：* / % + - << >> & | ^，该标签合并多种运算，
// 哪个 token 访问函数非空就是哪个运算符
std::any AstBuilder::visitArithmeticOrLogicalExpression(
    RustParser::ArithmeticOrLogicalExpressionContext* ctx) {
    auto node = std::make_shared<BinaryExpr>();
    node -> lhs = toExpr(visit(ctx -> expression(0)));
    node -> rhs = toExpr(visit(ctx -> expression(1)));
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
    return ExprPtr(node);
}

// 逻辑运算 &&/||：和算术运算一样建 BinaryExpr，op 按命中的 token 区分
std::any AstBuilder::visitLazyBooleanExpression(
    RustParser::LazyBooleanExpressionContext* ctx) {
    auto node = std::make_shared<BinaryExpr>();
    node->lhs = toExpr(visit(ctx->expression(0)));
    node->rhs = toExpr(visit(ctx->expression(1)));
    if (ctx->ANDAND()) node->op = "&&";
    if (ctx->OROR())   node->op = "||";
    return ExprPtr(node);
}

// 路径表达式（变量/函数引用）：遍历所有段用 :: 拼接，支持 Point::new 等多段路径
std::any AstBuilder::visitPathExpression_(RustParser::PathExpression_Context* ctx) {
    auto node = std::make_shared<PathExpr>();
    std::string path;
    for (auto* seg : ctx->pathExpression()->pathInExpression()->pathExprSegment()) {
        if (!path.empty()) path += "::";
        path += seg->getText();
    }
    node->name = path;
    return ExprPtr(node);
}

// 一元运算：-x、!x
std::any AstBuilder::visitNegationExpression(RustParser::NegationExpressionContext* ctx) {
    auto node = std::make_shared<UnaryExpr>();
    if (ctx -> MINUS()) node -> op = "-";
    if (ctx -> NOT()) node -> op = "!";
    node -> operand = toExpr(visit(ctx -> expression()));
    return ExprPtr(node);
}

// 比较表达式：==、<、> 等，运算符在 comparisonOperator 子规则里
std::any AstBuilder::visitComparisonExpression(RustParser::ComparisonExpressionContext* ctx) {
    auto node = std::make_shared<BinaryExpr>();
    node -> lhs = toExpr(visit(ctx -> expression(0)));
    node -> rhs = toExpr(visit(ctx -> expression(1)));
    node -> op = ctx -> comparisonOperator() -> getText();
    return ExprPtr(node);
}

// 赋值表达式：复用 BinaryExpr，op = "="
std::any AstBuilder::visitAssignmentExpression(RustParser::AssignmentExpressionContext* ctx) {
    auto node = std::make_shared<BinaryExpr>();
    node -> lhs = toExpr(visit(ctx -> expression(0)));
    node -> rhs = toExpr(visit(ctx -> expression(1)));
    node -> op = "=";
    return ExprPtr(node);
}

// as 类型转换：expr as T → CastExpr
std::any AstBuilder::visitTypeCastExpression(RustParser::TypeCastExpressionContext* ctx) {
    auto node = std::make_shared<CastExpr>();
    node->expr = toExpr(visit(ctx->expression()));
    node->target = parseTypeNoBounds(ctx->typeNoBounds());
    return ExprPtr(node);
}

// 括号表达式：(expr)，括号只是分组，直接穿透返回内层表达式的节点
// （官方测试里出现 while (turn < 9) 这种带括号条件，不覆盖会崩）
std::any AstBuilder::visitGroupedExpression(RustParser::GroupedExpressionContext* ctx) {
    return visit(ctx->expression());
}

// 表达式语句：普通表达式（带分号）和 if/while/loop 等带块表达式（分号可选）
// 统一包成 ExprStmt
std::any AstBuilder::visitExpressionStatement(RustParser::ExpressionStatementContext* ctx) {
    auto node = std::make_shared<ExprStmt>();
    node->hasSemi = ctx->SEMI() != nullptr;   // 带块表达式的分号可选
    if (ctx -> expression()) node -> expr = toExpr(visit(ctx -> expression()));
    if (auto* ewb = ctx -> expressionWithBlock()){
        if (ewb -> blockExpression())
            // 裸块语句 { ... }：visit 返回 BlockPtr，Block 继承 Expr，直接转 ExprPtr
            node -> expr = ExprPtr(std::any_cast<BlockPtr>(visit(ewb -> blockExpression())));
        else
            node -> expr = toExpr(visit(ewb));
    }
    return StmtPtr(node);
}

// if 表达式：else 分支可能是块（blockExpression(1)）或嵌套 if（else if 链），
// 嵌套时包一层只含该 IfExpr 的 Block，保持 elseBlock 类型一致
std::any AstBuilder::visitIfExpression(RustParser::IfExpressionContext* ctx) {
    auto node = std::make_shared<IfExpr>();
    node -> cond = toExpr(visit(ctx -> expression()));
    node -> thenBlock = std::any_cast<BlockPtr>(visit(ctx -> blockExpression(0)));
    // 条件括号检测：同 while（见 visitPredicateLoopExpression）
    node -> condGrouped = !ctx->expression()->getText().empty()
                          && ctx->expression()->getText().front() == '(';
    if (ctx -> KW_ELSE()){
        if (ctx -> ifExpression()){
            // else if 链：嵌套的 if 作为包装块的尾表达式（块的值 = if 的值），
            // 否则两分支类型比对会拿到语句位置的 Unit
            auto result = std::make_shared<Block>();
            result->tailExpr = toExpr(visit(ctx -> ifExpression()));
            node -> elseBlock = BlockPtr(result);
        } else {
            node -> elseBlock = std::any_cast<BlockPtr>(visit(ctx -> blockExpression(1)));
        }
    }
    return ExprPtr(node);
}

// while 循环
std::any AstBuilder::visitPredicateLoopExpression(RustParser::PredicateLoopExpressionContext* ctx) {
    auto node = std::make_shared<WhileExpr>();
    node -> cond = toExpr(visit(ctx -> expression()));
    node -> body = std::any_cast<BlockPtr>(visit(ctx -> blockExpression()));
    // 条件括号检测：getText 会剥掉空白，带括号条件的文本以 ( 开头
    node -> condGrouped = !ctx->expression()->getText().empty()
                          && ctx->expression()->getText().front() == '(';
    return ExprPtr(node);
}

// loop 无限循环
std::any AstBuilder::visitInfiniteLoopExpression(RustParser::InfiniteLoopExpressionContext* ctx) {
    auto node = std::make_shared<LoopExpr>();
    node -> body = std::any_cast<BlockPtr>(visit(ctx -> blockExpression()));
    return ExprPtr(node);
}

// return，值可空（裸 return;）
std::any AstBuilder::visitReturnExpression(RustParser::ReturnExpressionContext* ctx) {
    auto node = std::make_shared<ReturnExpr>();
    if (ctx -> expression()) node -> value = toExpr(visit(ctx -> expression()));
    return ExprPtr(node);
}

// break，值可空
std::any AstBuilder::visitBreakExpression(RustParser::BreakExpressionContext* ctx) {
    auto node = std::make_shared<BreakExpr>();
    if (ctx -> expression()) node -> value = toExpr(visit(ctx -> expression()));
    return ExprPtr(node);
}

// continue，无孩子
std::any AstBuilder::visitContinueExpression(RustParser::ContinueExpressionContext* ctx) {
    return ExprPtr(std::make_shared<ContinueExpr>());
}

// 语句级宏调用（println! 等）：参数是 tokenTree 原始 token 流，
// 第一版拼接原文存储，CodeGen 阶段再解析
std::any AstBuilder::visitMacroInvocationSemi(RustParser::MacroInvocationSemiContext* ctx) {
    auto node = std::make_shared<MacroStmt>();
    node->name = ctx->simplePath()->getText();
    std::string args;
    for (auto* tt : ctx->tokenTree()) {
        if (!args.empty()) args += " ";
        args += tt->getText();
    }
    node->argsText = args;
    return StmtPtr(node);
}

// 函数调用：左递归后缀，expression() 是被调者，callParams 可空（foo() 无实参）
std::any AstBuilder::visitCallExpression(RustParser::CallExpressionContext* ctx) {
    auto node = std::make_shared<CallExpr>();
    node -> callee = toExpr(visit(ctx -> expression()));
    if (ctx -> callParams()){
        for (auto* expr : ctx -> callParams() -> expression()){
            node -> args.push_back(toExpr(visit(expr)));
        }
    }
    return ExprPtr(node);
}

// struct 定义：名字 + 字段列表（字段复用 Param 结构）
std::any AstBuilder::visitStructStruct(RustParser::StructStructContext* ctx) {
    auto node = std::make_shared<StructDef>();
    node -> name = ctx -> identifier() -> getText();
    if (ctx -> structFields()){
        for (auto* field : ctx -> structFields() -> structField()){
            Param param;
            param.name = field -> identifier() -> getText();
            param.type = parseType(field -> type_());
            node -> fields.push_back(param);
        }
    }
    return ItemPtr(node);
}

// impl 块：目标类型 + 方法列表（跳过非函数的 associatedItem，如 const）
std::any AstBuilder::visitInherentImpl(RustParser::InherentImplContext* ctx) {
    auto node = std::make_shared<ImplBlock>();
    node -> typeName = ctx -> type_() -> getText();
    for (auto* item : ctx -> associatedItem()){
        if (item -> function_()){
            auto result = std::any_cast<ItemPtr>(visit(item -> function_()));
            node -> methods.push_back(std::static_pointer_cast<Function>(result));
        }
    }
    return ItemPtr(node);
}

// 结构体字面量 Point { x: 1, y: 2 }，暂不支持 tuple/unit 结构体形式
// structExprField 允许简写 Point { x }（g4:585），此时 expression() 为空；
// 简写等价于 x: x，补一个同名 PathExpr
std::any AstBuilder::visitStructExpression_(RustParser::StructExpression_Context* ctx) {
    auto node = std::make_shared<StructLiteralExpr>();
    node -> name = ctx -> structExpression() -> structExprStruct() -> pathInExpression() -> getText();
    if (ctx -> structExpression() -> structExprStruct() -> structExprFields()){
        for (auto* field : ctx -> structExpression() -> structExprStruct() -> structExprFields() -> structExprField()){
            ExprPtr value;
            if (field -> expression())
                value = toExpr(visit(field -> expression()));
            else {
                auto shorthand = std::make_shared<PathExpr>();
                shorthand -> name = field -> identifier() -> getText();
                value = ExprPtr(shorthand);
            }
            node -> fields.push_back({field -> identifier() -> getText(), value});
        }
    }
    return ExprPtr(node);
}

// 字段访问：p.x
std::any AstBuilder::visitFieldExpression(RustParser::FieldExpressionContext* ctx) {
    auto node = std::make_shared<FieldExpr>();
    node -> object = toExpr(visit(ctx -> expression()));
    node -> field = ctx -> identifier() -> getText();
    return ExprPtr(node);
}

// 方法调用：p.shift(3)，receiver 不含进 args
std::any AstBuilder::visitMethodCallExpression(RustParser::MethodCallExpressionContext* ctx) {
    auto node = std::make_shared<MethodCallExpr>();
    node -> receiver = toExpr(visit(ctx -> expression()));
    node -> method = ctx -> pathExprSegment() -> getText();
    if (ctx -> callParams()){
        for (auto* expr : ctx -> callParams() -> expression()){
            node -> args.push_back(toExpr(visit(expr)));
        }
    }
    return ExprPtr(node);
}

// 复合赋值：a += b，运算符在 compoundAssignOperator 子规则里
//（不覆盖的话基类默认穿透会把整个表达式静默替换成右操作数）
std::any AstBuilder::visitCompoundAssignmentExpression(
    RustParser::CompoundAssignmentExpressionContext* ctx) {
    auto node = std::make_shared<BinaryExpr>();
    node->lhs = toExpr(visit(ctx->expression(0)));
    node->rhs = toExpr(visit(ctx->expression(1)));
    node->op = ctx->compoundAssignOperator()->getText();
    return ExprPtr(node);
}

// 数组字面量：列举 [1,2,3] 与重复 [0; 5] 两种形式，
// 用 arrayElements 里 SEMI 是否非空区分（重复形式恰好两个 expression，中间是分号）
std::any AstBuilder::visitArrayExpression(RustParser::ArrayExpressionContext* ctx) {
    auto node = std::make_shared<ArrayExpr>();
    if (ctx -> arrayElements()){
        if (ctx -> arrayElements() -> SEMI()){
            node -> repeatValue = toExpr(visit(ctx -> arrayElements() -> expression(0)));
            node -> repeatCount = toExpr(visit(ctx -> arrayElements() -> expression(1)));
        } else {
            for (auto* expr : ctx -> arrayElements() -> expression()){
                node -> elements.push_back(toExpr(visit(expr)));
            }
        }
    }
    return ExprPtr(node);
}

// 下标访问：a[i]
std::any AstBuilder::visitIndexExpression(RustParser::IndexExpressionContext* ctx) {
    auto node = std::make_shared<IndexExpr>();
    node -> array = toExpr(visit(ctx -> expression(0)));
    node -> index = toExpr(visit(ctx -> expression(1)));
    return ExprPtr(node);
}

// 引用：&x / &mut x，复用 UnaryExpr
std::any AstBuilder::visitBorrowExpression(RustParser::BorrowExpressionContext* ctx) {
    auto node = std::make_shared<UnaryExpr>();
    node -> op = "&";
    if (ctx -> KW_MUT()) node -> op = "&mut";
    node -> operand = toExpr(visit(ctx -> expression()));
    return ExprPtr(node);
}

// 解引用：*p，复用 UnaryExpr
std::any AstBuilder::visitDereferenceExpression(RustParser::DereferenceExpressionContext* ctx) {
    auto node = std::make_shared<UnaryExpr>();
    node -> op = "*";
    node -> operand = toExpr(visit(ctx -> expression()));
    return ExprPtr(node);
}

// 全局常量：const MAX: i32 = 100;
std::any AstBuilder::visitConstantItem(RustParser::ConstantItemContext* ctx) {
    auto node = std::make_shared<ConstDef>();
    node -> name = ctx -> identifier() -> getText();
    node -> type = parseType(ctx -> type_());
    if (ctx -> expression()) node -> value = toExpr(visit(ctx -> expression()));
    return ItemPtr(node);
}

// 全局静态量：static mut COUNT: i32 = 0;
std::any AstBuilder::visitStaticItem(RustParser::StaticItemContext* ctx) {
    auto node = std::make_shared<StaticDef>();
    node -> name = ctx -> identifier() -> getText();
    node -> type = parseType(ctx -> type_());
    if (ctx -> expression()) node -> value = toExpr(visit(ctx -> expression()));
    node -> isMut = (ctx -> KW_MUT())? true : false;
    return ItemPtr(node);
}

// ============================================================
// enum / match
// ============================================================

// enum 定义：遍历 enumItems()->enumItem() 列表；tuple 式负载的类型逐个
// parseType 收进 payloadTypes；struct 式变体/显式判别值课程测试基本不出现，跳过
std::any AstBuilder::visitEnumeration(RustParser::EnumerationContext* ctx) {
    auto node = std::make_shared<EnumDef>();
    node -> name = ctx -> identifier() -> getText();
    if (ctx -> enumItems()){
        // enumItems() 返回单个上下文（不是 vector），列表在它下面的 enumItem() 方法里
        for (auto* item : ctx -> enumItems() -> enumItem()){
            EnumVariant variant;   // variants 按值存
            variant.name = item -> identifier() -> getText();
            if (auto* tup = item -> enumItemTuple()){
                // tupleFields/tupleField 是方法（要带 ()）；空括号 () 时 tupleFields() 为空
                if (auto* fields = tup -> tupleFields()){
                    for (auto* field : fields -> tupleField()){
                        variant.payloadTypes.push_back(parseType(field -> type_()));
                    }
                }
            }
            node -> variants.push_back(variant);
        }
    }
    return ItemPtr(node);
}

// match 表达式：scrutinee + 分支表。
// 注意 1：matchArm 上下文里只有 pattern(+guard)，分支体在 matchArms 层——
//   前 n-1 个在 matchArmExpression(i)，最后一个在 matchArms->expression()
// 注意 2：=> { ... } 实际被 ANTLR 解析进 matchArmExpression 的 expression COMMA
//   备选（expression 规则自身包含 expressionWithBlock 备选），所以判断块式体
//   要对 expression 上下文 dynamic_cast 成 ExpressionWithBlock_Context 再往里看
std::any AstBuilder::visitMatchExpression(RustParser::MatchExpressionContext* ctx) {
    auto node = std::make_shared<MatchExpr>();
    node -> scrutinee = toExpr(visit(ctx -> expression()));
    if (auto* arms = ctx -> matchArms()){
        auto armCtxs = arms -> matchArm();
        for (size_t i = 0; i < armCtxs.size(); ++i){
            MatchArmNode arm;
            arm.patternText = armCtxs[i] -> pattern() -> getText();
            auto fillBody = [&](RustParser::ExpressionContext* exprCtx){
                if (auto* ewbAlt = dynamic_cast<RustParser::ExpressionWithBlock_Context*>(exprCtx)){
                    auto* ewb = ewbAlt -> expressionWithBlock();
                    if (ewb -> blockExpression())
                        // 字面 { } 块存 bodyBlock（visitBlockExpression 返回 BlockPtr）
                        arm.bodyBlock = std::any_cast<BlockPtr>(visit(ewb -> blockExpression()));
                    else
                        // if/while/loop/嵌套 match：visit 返回 ExprPtr，存 body
                        arm.body = toExpr(visit(ewb));
                } else {
                    arm.body = toExpr(visit(exprCtx));
                }
            };
            if (i + 1 < armCtxs.size()){
                fillBody(arms -> matchArmExpression(i) -> expression());
            } else {
                // 最后一个分支体：单独在 matchArms->expression() 里
                fillBody(arms -> expression());
            }
            node -> arms.push_back(arm);
        }
    }
    return ExprPtr(node);
}
