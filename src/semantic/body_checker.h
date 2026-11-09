#pragma once

// ============================================================
// BodyChecker：函数体检查（8 阶段模型的第 8 阶段）
//
// 逐函数遍历函数体，做三件事：
//   1. 名字解析：每个用到的名字都能在符号表里查到
//   2. 类型检查：每个表达式求出类型（写进 lastType_），不匹配就报错
//   3. 控制流检查：break/continue 在循环内、return 类型匹配、
//      尾表达式类型 == 声明返回类型
//
// 表达式类型的传递方式：visit(XxxExpr&) 把结果类型写进 lastType_，
// 父节点 visit 完子表达式后立刻读走（下一次 visit 会覆盖）。
// 常量求值委托给 ConstEvaluator。
// ============================================================

#include "ast/ast.h"
#include "ast/ast_visitor.h"
#include "semantic/const_evaluator.h"
#include "semantic/symbol_table.h"

#include <unordered_map>

class BodyChecker : public ASTVisitor {
public:
    explicit BodyChecker(SymbolTable& symtab) : symtab_(symtab), constEval_(symtab) {}

    void run(const CratePtr& crate);

    // 表达式类型旁表：每个被检查过的表达式节点 → 它的类型。
    // 检查期间由 evalExpr 填充；检查结束后交给 IR 阶段按节点指针查询
    //（AST 节点由 shared_ptr 持有，整个编译期地址稳定，可放心当键用）
    const std::unordered_map<const Expr*, Type>& exprTypes() const { return exprTypes_; }

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
    ConstEvaluator constEval_;   // 常量求值（数组长度、const 初值、越界下标）

    Type lastType_;            // 最近一个表达式的求值类型（读完立刻存走）
    bool lastDiverges_ = false;  // 最近一个表达式是否发散（return/break/不停 loop）：
                                 // Block 用它判定不可达代码与块的 Never 类型
    Type currentFnRetType_;    // 当前函数的声明返回类型（return 语句比对用）
    int loopDepth_ = 0;        // 循环嵌套深度，break/continue 要求 > 0
    Type loopBreakType_;       // 最近一层 loop 里带值 break 的值类型
    bool hasLoopBreakValue_ = false;
    bool hasAnyBreak_ = false;   // 本层 loop 是否出现任何 break（无 break 的 loop
                                 // 只能靠 return 离开，类型为 Never）
    bool allowI32MinLit_ = false;  // 一元 - 直接作用时允许字面量 2147483648（i32 MIN）
    std::string currentImplType_;  // 当前所在 impl 的目标类型名（visit(ImplBlock)
                                   // 进入时设置、离开时清空；方法里注册 self 用）
    bool rxDialect_ = false;   // Rx 方言（源文件含 use 声明）：放宽旧规范的
                               // 条件括号要求，按标准 Rust 语义检查

    std::unordered_map<const Expr*, Type> exprTypes_;   // 表达式类型旁表
    // 求值子表达式并登记其类型：所有表达式参数的统一入口
    void evalExpr(const ExprPtr& e);
    // Self 类型归一化（impl 外使用报错）与具名类型存在性检查，见 .cpp
    void resolveSelfType(Type& t);
    void checkTypeResolvable(const Type& t);

    // 隐式类型转换（coercion）：src 的值能否隐式当作 dst 用。
    // 用于 let 初值→标注、赋值右值→左值、实参→形参、return 值→返回类型。
    // 规则实现见 .cpp（同学完成）
    bool coercible(const Type& src, const Type& dst);
    // 类型合并（最小上界）：if 两臂、数组元素、loop 多个 break 值的合并。
    // 无法合并返回 false。规则实现见 .cpp（同学完成）
    bool lub(const Type& a, const Type& b, Type& out);

    // Clone/PartialEq 能力查询（derive 标记 + 内建规则），供 .clone()/== 用
    bool isCloneableType(const Type& t);
    bool hasPartialEqType(const Type& t);
    // Copy 能力查询：标量、共享引用、元素可 Copy 的数组、derive(Copy) 的
    // struct；Vec/Box/String 不可 Copy。供数组重复形式 [v; N] 检查用
    bool isCopyType(const Type& t);
    // ---- 位置可写性 / 可变重借（简化借用检查）----
    // placeWritable(e)：位置 e 能否被整体写入（赋值目标、&mut 借用、
    //   Vec::push 的 receiver）。沿 Field/Index/解引用往根走；遇到引用层
    //   由引用自身的可变性决定；根变量必须是可变绑定。
    // reborrowOk(place)：存放在 place 里的 &mut 值能否被可变使用（通过它
    //   写入 / 当右值再借出）。Vec 元素不绕过 Vec 自身的可变性
    //  （vec-index-mutability 系列 rej）；数组/struct/Box 容器保持内部
    //  &mut 的写能力（preserve 系列 acc）。
    bool placeWritable(const ExprPtr& e);
    bool reborrowOk(const ExprPtr& e);
    bool reborrowContainerOk(const ExprPtr& expr, const Type& containerType);
    // 把存放在某个位置里的 &mut 值当右值用时调用（let 初值、实参）：
    // 位置形状的表达式（Field/Index/解引用）且类型是 &mut 时才涉及重借
    void checkReborrowUse(const ExprPtr& e);
};
