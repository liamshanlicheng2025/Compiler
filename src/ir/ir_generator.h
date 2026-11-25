#pragma once

// ============================================================
// IRGenerator：AST → LLVM IR 文本（编译器第 4 阶段）
//
// 遍历方式与 BodyChecker 同构：
//   visit(XxxExpr&) 把产物写进 lastValue_（"%5" 这样的寄存器名，
//   或 "42" 这样的常量文本），父节点 visit 完子表达式后立刻读走；
//   表达式的源类型随时查 exprTypes_ 旁表（选指令、算布局的依据）。
//
// 里程碑分工（docs/ir-overview-and-plan.md §6）：
//   M1 骨架：Crate / StructDef / Function 外壳（已实现）
//   M2：Literal / PathExpr / BinaryExpr / LetStmt / CallExpr(内建)
//   M3：IfExpr / WhileExpr / LoopExpr / Break / Continue / Return / 短路
//   M4：函数调用与参数 / MethodCall / ImplBlock 方法
//   M5：StructLiteral / FieldExpr / ArrayExpr / IndexExpr / UnaryExpr(&,*)
//   enum/match 课程不要求：visit(EnumDef/MatchExpr) 是死桩
// ============================================================

#include "ast/ast.h"
#include "ast/ast_visitor.h"
#include "ir/ll.h"
#include "ir/type_map.h"

#include <string>
#include <unordered_map>
#include <vector>

class SymbolTable;

class IRGenerator : public ASTVisitor {
public:
    IRGenerator(SymbolTable& symtab,
                const std::unordered_map<const Expr*, Type>& exprTypes);

    std::string run(const CratePtr& crate);        // 生成完整 .ll 文本（CLI 用）
    const ll::Module& module() const;              // codegen 的入口：完整对象树

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
    const std::unordered_map<const Expr*, Type>& exprTypes_;
    ll::LLBuilder ir_;

    // 最近一个表达式 lower 出的值：寄存器（%5）/ 立即数（42）/ 全局名
    // 传递模式与 BodyChecker::lastType_ 相同：用完立刻取走
    ll::Value lastValue_ = ll::Value::imm("");
    std::string currentImplType_;
    // 局部变量地址表：变量名 → alloca 栈槽指针（%x.addr）
    // let 遮蔽 = 同名新槽覆盖旧槽（与语义阶段的遮蔽语义一致）
    std::unordered_map<std::string, ll::Value> locals_;

    // 循环标签栈：{continue 目标, break 目标}，进循环压栈、出循环弹栈
    // break/continue 查栈顶决定 br 去哪（嵌套循环天然正确）
    std::vector<std::pair<std::string, std::string>> loopLabels_;

    // 带值 break 的结果槽栈：与 loopLabels_ 平行，进循环时压入
    // （不产值的循环压 reg("") 占位，保持两栈同步）
    std::vector<ll::Value> loopValueSlots_;

    // 当前基本块已被终结指令（ret/br）封死：其后语句不可达，visit(Block)
    // 遇此标志跳过剩余语句；ir_.label() 开新块时解封
    bool blockSealed_ = false;

    // 同名变量遮蔽时栈槽去重：x 第二次 let 时槽名 %x.addr.1、第三次 .2
    std::unordered_map<std::string, int> slotVer_;

    // 模块用到 i64 除/取余时置位：rv32 没有 64 位除法指令，clang 会生成
    // __udivdi3 等 libcall，ravel 的运行时没有——visit(Crate) 末尾补发
    // 位移减法的 IR 实现
    bool needsI64Div_ = false;

    bool inMain_ = false;   // 当前在翻译 main（裸 return; 要 ret i32 0）

    Type currentFnRetType_;   // 当前函数返回类型（ReturnExpr 发 ret 时用）

    // 函数名 → 定义（含嵌套函数）：IR 阶段符号表的 current_ 停在全局，
    // 块内嵌套 fn 查不到，用这张表兜底（查返回类型用）
    std::unordered_map<std::string, std::shared_ptr<Function>> fnByName_;
    // 嵌套函数（块内 ItemStmt 包的 fn）：Rust 不捕获环境，提升到模块级发射
    std::unordered_map<std::string, std::shared_ptr<Function>> nestedFns_;

    // 结构体名 → 定义（含嵌套 struct）：字段表/类型定义发射都查这张表，
    // 不依赖符号表的作用域状态（IR 阶段 current_ 停在全局）
    std::unordered_map<std::string, std::shared_ptr<StructDef>> structByName_;

    // 嵌套 const（块内 ItemStmt 包的 const）的编译期值：收集 pass 时求好
    std::unordered_map<std::string, long long> constVals_;
    const StructDef* findStruct(const std::string& name) const {
        auto it = structByName_.find(name);
        return it == structByName_.end() ? nullptr : it->second.get();
    }

    // sret：函数返回聚合体（struct/数组）时，改用隐藏指针参数——调用方
    // 先 alloca 好槽位，把地址作第一个实参传入，被调方往里面拷贝、ret void。
    // 非聚合体返回时为空占位（reg("")）
    ll::Value sretSlot_ = ll::Value::reg("");

    Type typeOf(const Expr& e) const;            // 查 exprTypes 旁表
    ll::Value evalExpr(const ExprPtr& e);        // visit 子表达式并取走 lastValue_
    ll::Value evalAddr(const ExprPtr& e);        // 求左值表达式的地址（不 load）
    // 聚合体逐成员拷贝：dst/src 都是地址，按语义类型递归
    void emitCopyAggregate(ll::Value dst, ll::Value src, const Type& ty);
    // 实参按形参类型收窄/扩宽（字面量→i64 形参、bool→i32 内建函数等）
    ll::Value coerceArg(ll::Value v, const Type& srcT, const Type& dstT);
};
