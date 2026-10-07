#include "ast.h"
#include "ast_visitor.h"

// 每个节点的 accept 就是把控制权转交给 visitor 的对应方法。
// 这是固定的机械代码，新加节点时照着写一行即可。

void Crate::accept(ASTVisitor& visitor)      { visitor.visit(*this); }
void Block::accept(ASTVisitor& visitor)      { visitor.visit(*this); }
void Function::accept(ASTVisitor& visitor)   { visitor.visit(*this); }
void LetStmt::accept(ASTVisitor& visitor)    { visitor.visit(*this); }
void ExprStmt::accept(ASTVisitor& visitor)   { visitor.visit(*this); }
void Literal::accept(ASTVisitor& visitor)    { visitor.visit(*this); }
void BinaryExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void PathExpr::accept(ASTVisitor& visitor)   { visitor.visit(*this); }
void UnaryExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void IfExpr::accept(ASTVisitor& visitor)     { visitor.visit(*this); }
void WhileExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void LoopExpr::accept(ASTVisitor& visitor)   { visitor.visit(*this); }
void ReturnExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void BreakExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void ContinueExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void CallExpr::accept(ASTVisitor& visitor)   { visitor.visit(*this); }
void StructDef::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void ImplBlock::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void StructLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void FieldExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void MethodCallExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void ArrayExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void IndexExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void ConstDef::accept(ASTVisitor& visitor)   { visitor.visit(*this); }
void StaticDef::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void EnumDef::accept(ASTVisitor& visitor)    { visitor.visit(*this); }
void MatchExpr::accept(ASTVisitor& visitor)  { visitor.visit(*this); }
void MacroStmt::accept(ASTVisitor& visitor)    { visitor.visit(*this); }
