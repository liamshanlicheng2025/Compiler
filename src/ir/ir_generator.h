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
    // 剥引用：v 是 t 的值，t 每包一层 Ref 就 load 一次，直到非引用标量。
    // 用于算术/比较/位运算的操作数（Rx 允许 &a + &b 直接运算）
    std::pair<ll::Value, Type> loadThroughRefs(ll::Value v, const Type& t);
    // 移位量处理：扩宽/收窄到 LHS 位宽（一律按无符号），再 and 上
    // （位宽-1）做计数掩码——Rx 的移位对计数取模，LLVM 里超界移位是 poison
    ll::Value normalizeShiftCount(ll::Value v, const Type& cntT, ll::Type lhsTy);

    // ---- Vec/Box 内建容器 ----
    // 表示法：Box<T> = 堆指针（标量 ptr，值=指向内容的指针）；
    //         Vec<T> = 聚合体 %struct.Vec{ ptr data, i32 len, i32 cap }（值=地址）
    // 聚合体约定（值=地址）：(Named 且非 Box) || Array——Box 是标量，不在其列
    static bool isAggT(const Type& t);
    int sizeOfType(const Type& t);    // rv32 ilp32 布局的字节数（struct 按对齐排）
    int alignOfType(const Type& t);
    // 纯位数据（内部不含 Box/Vec）：clone/拷贝可直接 memcpy，不用递归
    bool isPlainData(const Type& t);
    ll::Value emitAlloc(ll::Value bytes);            // call ptr @rx_alloc(i32)
    void emitMemcpy(ll::Value dst, ll::Value src, ll::Value bytes);
    // clone 语义的拷贝：dst/src 都是地址；Box 字段换新盒子、Vec 字段换新缓冲
    void cloneInto(ll::Value dst, ll::Value src, const Type& t);
    // clone 按"值"约定进出：标量恒等，Box 装新盒，聚合体新槽
    ll::Value emitCloneValue(ll::Value v, const Type& t);
    // 结构相等：a/b 按"值"约定（聚合体=地址、Box=堆指针），返回 i8 0/1
    ll::Value emitEquality(ll::Value a, ll::Value b, const Type& t);
    // 接收者解析：剥 Ref/Box 链到方法宿主类型，返回 (宿主类型, 宿主地址)。
    // 第一层 Ref/Box 的"值"就是目标地址；再往里每剥一层多 load 一次
    std::pair<Type, ll::Value> peelReceiver(const ExprPtr& receiver);
    // Vec 内建操作：fn ∈ {push, len, is_empty, remove}；selfAddr = Vec 结构体
    // 地址；args 为 self 之后的实参表达式。结果留 lastValue_
    void emitVecMethod(const std::string& fn, ll::Value selfAddr, const Type& elemT,
                       std::vector<ExprPtr>& args);
    ll::Value emitVecNew();                                // 新空 Vec（值=地址）
    ll::Value emitVecClone(ll::Value vecAddr, const Type& elemT);
    ll::Value vecField(ll::Value vecAddr, int i);          // data/len/cap 字段指针
    // 结构相等的地址版：pa/pb 是两块存储的地址
    ll::Value emitEqualityAt(ll::Value pa, ll::Value pb, const Type& t);
    // 下标/字段的地址计算（值通道 = 地址 + load；左值通道直接用）：
    // 自动剥基表达式的 Ref/Box 链（boxed[0]、r[0][1]、b.value 等）
    ll::Value indexElementAddr(const ExprPtr& arrayE, const ExprPtr& indexE);
    ll::Value fieldElementAddr(const ExprPtr& objE, const std::string& field);
    // clone/结构相等的函数化：含 Box/Vec 的复合类型可能递归（Vec<Self>），
    // 内联展开会无限递归——改成生成模块级辅助函数 @rx.clone.<m> / @rx.eq.<m>，
    // 用到时排队，visit(Crate) 末尾统一补发定义
    std::string typeMangle(const Type& t);
    void ensureCloneFn(const Type& t);
    void ensureEqFn(const Type& t);
    void emitVecCloneInto(ll::Value dst, ll::Value src, const Type& elemT);
    void emitSyntheticFns();   // 排空两个队列，补发定义
    std::vector<Type> cloneQueue_, eqQueue_;
    std::unordered_set<std::string> cloneDone_, eqDone_;
};
