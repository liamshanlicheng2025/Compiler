#pragma once

// ============================================================
// Pass 1：声明收集（DeclCollector）
//
// 只走 AST 顶层，把所有"名字"注册进符号表，不看函数体。
// 这样 Pass 2 检查函数体时，所有函数/类型都已经可见（支持先用后定义、
// 函数互相递归调用）。
//
// 填充内容：
//   - 内建函数（registerBuiltins）
//   - 函数签名、const/static → 全局作用域
//   - struct/enum → 全局作用域（Symbol 里回指定义节点）
//   - impl 方法 → SymbolTable::implMethods（按类型名挂）
// ============================================================

#include "ast/ast.h"
#include "ast/ast_visitor.h"
#include "semantic/symbol_table.h"

class DeclCollector : public ASTVisitor {
public:
    explicit DeclCollector(SymbolTable& symtab) : symtab_(symtab) {}

    // 入口：预注册内建函数 + 遍历顶层条目
    void run(const CratePtr& crate);

    // visit(Crate) 里用 dynamic_pointer_cast 分发到下面的注册函数，
    // 所以大部分 visit 覆写不会被走到（留空即可）
    void visit(Crate& node) override;
    void visit(Block&) override {}
    void visit(Function&) override {}
    void visit(LetStmt&) override {}
    void visit(ExprStmt&) override {}
    void visit(Literal&) override {}
    void visit(BinaryExpr&) override {}
    void visit(PathExpr&) override {}
    void visit(UnaryExpr&) override {}
    void visit(IfExpr&) override {}
    void visit(WhileExpr&) override {}
    void visit(LoopExpr&) override {}
    void visit(ReturnExpr&) override {}
    void visit(BreakExpr&) override {}
    void visit(ContinueExpr&) override {}
    void visit(CallExpr&) override {}
    void visit(StructDef&) override {}
    void visit(ImplBlock&) override {}
    void visit(StructLiteralExpr&) override {}
    void visit(FieldExpr&) override {}
    void visit(MethodCallExpr&) override {}
    void visit(ArrayExpr&) override {}
    void visit(IndexExpr&) override {}
    void visit(ConstDef&) override {}
    void visit(StaticDef&) override {}
    void visit(EnumDef&) override {}
    void visit(MatchExpr&) override {}
    void visit(MacroStmt&) override {}
    void visit(ItemStmt&) override {}
    void visit(CastExpr&) override {}

private:
    SymbolTable& symtab_;

    // 各条目类型的注册逻辑（伪代码见 decl_collector.cpp）
    void registerFunction(const FunctionPtr& fn);
    void registerStruct(const std::shared_ptr<StructDef>& def);
    void registerConst(const std::shared_ptr<ConstDef>& def);
    void registerStatic(const std::shared_ptr<StaticDef>& def);
    void registerImpl(const std::shared_ptr<ImplBlock>& def);
    // enum 课程不要求（助教 CR 确认），不注册 EnumDef
};
