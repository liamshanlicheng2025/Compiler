#pragma once

// ============================================================
// ll：迷你 LLVM IR 对象模型
//
// 助教指出的问题：IR 按字符串存储的话，codegen 阶段还要再 parse 一遍。
// 这个库把翻译出的 LLVM 指令直接存成结构化形式：
//
//   Module
//   ├── header   模块区顶格行（target、declare、%struct 定义）
//   ├── globals  字符串常量（@.str.N）
//   └── Function
//       ├── 签名（返回类型 / 名字 / 参数表）
//       └── BasicBlock（标签名 + 指令列表）
//           └── Instr（枚举 OpCode + 结构化操作数）
//
// 叶子也是结构化的，codegen 全程不需要解析任何文本：
//   Value：Reg（%5）/ Imm（42）/ Global（@.str.0）三态，按 kind 分派
//   Type：枚举 kind + size()（字节数），数组递归、struct 查布局表
// 字符串只出现在两处：叶子的 text 载荷（名字/数字原文），
// 以及 emit() 打印 .ll 文本——后者只是给 clang 和肉眼看的输出格式。
//
// IR 阶段与 codegen 的接口是 const ll::Module&（完整对象树）；
// emit() 输出与旧的字符串拼装器逐字节一致（已对 8 个用例 diff 验证）。
// ============================================================

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ll {

// 值：三态叶子。text 保留原文（"%5" / "42" / "@.str.0"），
// 但 codegen 只按 kind 分派，不解析 text
struct Value {
    enum class Kind { Reg, Imm, Global } kind = Kind::Reg;
    std::string text;

    static Value reg(std::string name)    { return {Kind::Reg, std::move(name)}; }
    static Value imm(std::string num)     { return {Kind::Imm, std::move(num)}; }
    static Value global(std::string name) { return {Kind::Global, std::move(name)}; }

    bool isReg() const { return kind == Kind::Reg; }
    bool isImm() const { return kind == Kind::Imm; }
    bool isGlobal() const { return kind == Kind::Global; }
    int immValue() const { return std::stoi(text); }   // 仅 Imm 用
    const std::string& str() const { return text; }    // emit 用
};

// 类型：枚举 kind + 负载。codegen 关心的是 kind 和 size()（字节数）
struct Type {
    enum class Kind { Void, I1, I8, I32, F32, F64, Ptr, Array, Struct } kind = Kind::Void;
    int arrayLen = 0;                  // Array：元素个数
    std::shared_ptr<Type> elem;        // Array：元素类型
    std::string structName;            // Struct：名字（布局表的键）
    int structBytes = -1;              // Struct：字节数；-1 = 未定，codegen 查布局表

    static Type voidT() { return {}; }
    static Type i1()  { Type t; t.kind = Kind::I1;  return t; }
    static Type i8()  { Type t; t.kind = Kind::I8;  return t; }
    static Type i32() { Type t; t.kind = Kind::I32; return t; }
    static Type f32() { Type t; t.kind = Kind::F32; return t; }
    static Type f64() { Type t; t.kind = Kind::F64; return t; }
    static Type ptr() { Type t; t.kind = Kind::Ptr; return t; }
    static Type array(Type elem, int n);
    static Type struct_(std::string name, int bytes = -1);

    int size() const;                  // 字节数；Struct 未定返回 -1
    std::string str() const;           // emit 用文本（i32/ptr/[N x T]/%struct.X）
};

enum class OpCode {
    Comment,                                // 伪指令：注释行（; ...）
    Alloca, Load, Store,
    Add, Sub, Mul, SDiv, UDiv, SRem, URem,
    And, Or, Xor, Shl, AShr, LShr,
    ICmp,                                   // auxStr = 谓词（eq/ne/slt/...）
    ZExt,                                   // type = 源类型，auxType = 目标类型
    Call,                                   // args 用 (类型, 值) 对
    Ret, RetVoid,
    Br, BrCond,
    GEP                                     // auxType = 容器类型（Array/Struct）
};

struct Instr {
    OpCode op;
    Value result;                               // 目标寄存器；无结果指令 text 为空
    Type type;                                  // 主类型：alloca 的分配类型、load/store 的值类型、
                                                // 二元/icmp 的操作数类型、zext 源类型、call 返回类型
    std::string auxStr;                         // icmp 谓词 / Comment 文本
    Type auxType;                               // zext 目标类型 / GEP 容器类型
    std::vector<Value> operands;                // 位置化操作数（因指令而异，见 ll.cpp emit）
    std::vector<std::string> labels;            // Br / BrCond 的目标块名
    // Call 专用：实参表 (类型, 值)；首元素是 callee（Value::Global）
    std::vector<std::pair<Type, Value>> args;

    void emit(std::ostringstream& out) const;   // 打印一行（含两格缩进）
};

struct BasicBlock {
    std::string name;
    std::vector<Instr> instrs;
    void emit(std::ostringstream& out) const;
};

struct Function {
    Type retType;
    std::string name;
    std::vector<std::pair<Type, std::string>> params;  // (类型, 名字)
    std::vector<BasicBlock> blocks;
    void emit(std::ostringstream& out) const;
};

struct Module {
    std::vector<std::string> header;                          // 顶格行原文
    std::vector<std::pair<std::string, std::string>> globals; // (@.str.N, 原始内容)
    std::vector<Function> functions;
    std::string emit() const;   // 完整 .ll 文本
};

// LLBuilder：在 Module 上维护"当前函数/当前块"游标，提供结构化发射接口。
// 编号规则：寄存器 %0 %1 ... 函数内连续；
// 标签首用基名，之后 基名.1 .2 ...；两者都在 beginFunction 时重置。
class LLBuilder {
public:
    // ---- 编号器 ----
    Value freshReg();
    std::string freshLabel(const std::string& base);
    Value addString(const std::string& content);   // 返回 Value::global("@.str.N")

    // ---- 模块区 ----
    void raw(const std::string& line);                   // 顶格行（target/declare/%struct）

    // ---- 函数 ----
    void beginFunction(const Type& retType, const std::string& name,
                       std::vector<std::pair<Type, std::string>> params);
    void endFunction();
    void label(const std::string& name);                 // 结束当前块，开新块

    // ---- 指令发射（追加到当前块尾部）----
    void comment(const std::string& text);
    void emitAlloca(const Value& result, const Type& type);
    void emitLoad(const Value& result, const Type& type, const Value& ptr);
    void emitStore(const Type& type, const Value& value, const Value& ptr);
    // 整数二元运算：opName 为 "add"/"sub"/"mul"/"sdiv"/"udiv"/"srem"/"urem"
    //   /"and"/"or"/"xor"/"shl"/"ashr"/"lshr"
    void emitBinary(const std::string& opName, const Value& result,
                const Type& type, const Value& l, const Value& r);
    void emitIcmp(const std::string& pred, const Value& result,
              const Type& type, const Value& l, const Value& r);
    void emitZext(const Value& result, const Type& fromTy,
              const Type& toTy, const Value& v);
    // 有返回值用 emitCall，void 用 emitCallVoid
    void emitCall(const Value& result, const Type& retType,
              const std::string& fnName,
              const std::vector<std::pair<Type, Value>>& args);
    void emitCallVoid(const std::string& fnName,
              const std::vector<std::pair<Type, Value>>& args);
    void emitRet(const Type& type, const Value& value);
    void emitRetVoid();
    void emitBr(const std::string& labelName);
    void emitBrCond(const Value& cond, const std::string& thenLabel,
                const std::string& elseLabel);
    // GEP：indices 一律按 i32 打印；container 是容器类型（Array/Struct）
    void emitGep(const Value& result, const Type& container,
             const Value& ptr, const std::vector<Value>& indices);

    // codegen 的入口：完整对象树
    const Module& module() const { return module_; }
    std::string str() const { return module_.emit(); }

private:
    Module module_;
    int regNo_ = 0;
    std::unordered_map<std::string, int> labelUse_;
    int strNo_ = 0;

    void append(Instr i);   // 追加到当前函数当前块（空函数时自动开 "entry" 块）
};

} // namespace ll
