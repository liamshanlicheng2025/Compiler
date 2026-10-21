#pragma once

// ============================================================
// CrateIndex：声明收集（8 阶段模型的第 1~3、6 阶段）
//
// 只走 AST 顶层，把所有"名字"注册进符号表，不看函数体。
// 这样 BodyChecker 检查函数体时，所有函数/类型都已经可见
//（支持先用后定义、函数互相递归调用）。
//
// 填充内容：
//   - 内建函数（registerBuiltins）
//   - 函数签名、const/static → 全局作用域
//   - struct/enum → 全局作用域（Symbol 里回指定义节点）
//   - impl 方法 → SymbolTable::implMethods（按类型名挂）
//   - 入口检查：checkEntryPoint（骨架，待实现）
// ============================================================

#include "ast/ast.h"
#include "ast/ast_visitor.h"
#include "semantic/symbol_table.h"

class CrateIndex : public ASTVisitor {
public:
    explicit CrateIndex(SymbolTable& symtab) : symtab_(symtab) {}

    // 入口：预注册内建函数 + 遍历顶层条目 + 入口检查
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

    // 各条目类型的注册逻辑
    void registerFunction(const FunctionPtr& fn);
    void registerStruct(const std::shared_ptr<StructDef>& def);
    void registerConst(const std::shared_ptr<ConstDef>& def);
    void registerStatic(const std::shared_ptr<StaticDef>& def);
    void registerImpl(const std::shared_ptr<ImplBlock>& def);

    // 入口检查（骨架）：源程序必须有合法的 main——
    //   无参数、无返回类型标注（或显式 -> ()）、不能是方法/泛型。
    //   新官方测试集 semantic/entry/ 有 4 个 rej 用例对应这些规则。
    void checkEntryPoint(const CratePtr& crate);
};
