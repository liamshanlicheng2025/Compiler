#pragma once

// ============================================================
// Pass 2：语义检查（SemanticChecker）
//
// 逐函数遍历函数体，做三件事：
//   1. 名字解析：每个用到的名字都能在符号表里查到
//   2. 类型检查：每个表达式求出类型（写进 lastType_），不匹配就报错
//   3. 控制流检查：break/continue 在循环内、return 类型匹配、
//      尾表达式类型 == 声明返回类型
//
// 表达式类型的传递方式：visit(XxxExpr&) 把结果类型写进 lastType_，
// 父节点 visit 完子表达式后立刻读走（注意保存，下一次 visit 会覆盖）。
// ============================================================

#include "ast/ast.h"
#include "ast/ast_visitor.h"
#include "semantic/symbol_table.h"

class SemanticChecker : public ASTVisitor {
public:
    explicit SemanticChecker(SymbolTable& symtab) : symtab_(symtab) {}

    void run(const CratePtr& crate);

    void visit(Crate& node) override;
    void visit(Block& node) override;
    void visit(Function& node) override;
    void visit(LetStmt& node) override;
    void visit(ExprStmt& node) override;
    void visit(Literal& node) override;
    void visit(BinaryExpr& node) override;
    void visit(PathExpr& node) override;
    void visit(UnaryExpr& node) override;
    void visit(IfExpr& node) override;
    void visit(WhileExpr& node) override;
    void visit(LoopExpr& node) override;
    void visit(ReturnExpr& node) override;
    void visit(BreakExpr& node) override;
    void visit(ContinueExpr& node) override;
    void visit(CallExpr& node) override;
    void visit(StructDef& node) override;
    void visit(ImplBlock& node) override;
    void visit(StructLiteralExpr& node) override;
    void visit(FieldExpr& node) override;
    void visit(MethodCallExpr& node) override;
    void visit(ArrayExpr& node) override;
    void visit(IndexExpr& node) override;
    void visit(ConstDef& node) override;
    void visit(StaticDef& node) override;
    void visit(EnumDef& node) override;
    void visit(MatchExpr& node) override;
    void visit(MacroStmt& node) override;
    void visit(ItemStmt& node) override;
    void visit(CastExpr& node) override;

private:
    SymbolTable& symtab_;

    Type lastType_;            // 最近一个表达式的求值类型（读完立刻存走）
    Type currentFnRetType_;    // 当前函数的声明返回类型（return 语句比对用）
    int loopDepth_ = 0;        // 循环嵌套深度，break/continue 要求 > 0
    Type loopBreakType_;       // 最近一层 loop 里带值 break 的值类型（expr17）
    bool hasLoopBreakValue_ = false;
    bool hasAnyBreak_ = false;   // 本层 loop 是否出现任何 break（无 break 的 loop
                                 // 类型是 Never——只能靠 return 离开，misc15）
    bool allowI32MinLit_ = false;  // 一元 - 直接作用时允许字面量 2147483648（i32 MIN）

    // 解析类型里的数组长度：const 名查符号表（必须是 Const 且有编译期值，
    // 否则报"数组长度必须是常量"，见 basic8/basic35）；复杂表达式留 0 作通配
    void resolveArrayLens(Type& t);
    // 编译期常量表达式求值（数组长度用）：+ - * / % 和括号 + const 名
    bool constEval(const std::string& expr, long long& out);
    std::string currentImplType_;  // 当前所在 impl 的目标类型名（visit(ImplBlock)
                                   // 进入时设置、离开时清空；方法里注册 self 用）
};
