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
// emit() 负责把对象树打印回标准 .ll 文本，输出与旧的 IRBuilder
// 逐字节一致（迁移期可对每个测试用例 diff 验证）。
//
// 类型仍用字符串表示（i32/i8/ptr/[N x T]/%struct.X）：类型是叶子标签，
// codegen 按字符串分派即可，不值得再建一套类型对象。
// ============================================================

#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ll {

enum class OpCode {
    Comment,                                // 伪指令：注释行（; ...）
    Alloca, Load, Store,
    Add, Sub, Mul, SDiv, SRem,
    And, Or, Xor, Shl, AShr,
    ICmp,                                   // aux = 谓词（eq/ne/slt/sle/sgt/sge）
    ZExt,                                   // type = 源类型，aux = 目标类型
    Call,                                   // args 用 (类型, 值) 对
    Ret, RetVoid,
    Br, BrCond,
    GEP                                     // aux = 容器类型（[N x T]/%struct.X）
};

struct Instr {
    OpCode op;
    std::string result;                     // 目标寄存器（store/br/ret 等无结果指令为空）
    std::string type;                       // 主类型：alloca 的分配类型、load/store 的值类型、
                                            // 二元/icmp 的操作数类型、zext 源类型、call 返回类型
    std::string aux;                        // icmp 谓词 / zext 目标类型 / GEP 容器类型
    std::vector<std::string> operands;      // 位置化操作数（因指令而异，见 ll.cpp emit）
    // Call 专用：实参表 (类型, 值)；首元素是 callee（类型为空）
    std::vector<std::pair<std::string, std::string>> args;

    void emit(std::ostringstream& out) const;   // 打印一行（含两格缩进）
};

struct BasicBlock {
    std::string name;
    std::vector<Instr> instrs;
    void emit(std::ostringstream& out) const;
};

struct Function {
    std::string retType;
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;  // (类型, 名字)
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
// 编号规则与旧 IRBuilder 相同：寄存器 %0 %1 ... 函数内连续；
// 标签首用基名，之后 基名.1 .2 ...；两者都在 beginFunction 时重置。
class LLBuilder {
public:
    // ---- 编号器 ----
    std::string freshReg();
    std::string freshLabel(const std::string& base);
    std::string addString(const std::string& content);   // 返回 "@.str.N"

    // ---- 模块区 ----
    void raw(const std::string& line);                   // 顶格行（target/declare/%struct）

    // ---- 函数 ----
    void beginFunction(const std::string& retType, const std::string& name,
                       std::vector<std::pair<std::string, std::string>> params);
    void endFunction();
    void label(const std::string& name);                 // 结束当前块，开新块

    // ---- 指令发射（追加到当前块尾部）----
    void comment(const std::string& text);
    void emitAlloca(const std::string& result, const std::string& type);
    void emitLoad(const std::string& result, const std::string& type,
              const std::string& ptr);
    void emitStore(const std::string& type, const std::string& value,
               const std::string& ptr);
    // 整数二元运算：op 为 "add"/"sub"/"mul"/"sdiv"/"srem"/"and"/"or"/"xor"/"shl"/"ashr"
    void emitBinary(const std::string& opName, const std::string& result,
                const std::string& type, const std::string& l, const std::string& r);
    void emitIcmp(const std::string& pred, const std::string& result,
              const std::string& type, const std::string& l, const std::string& r);
    void emitZext(const std::string& result, const std::string& fromTy,
              const std::string& toTy, const std::string& v);
    // result 传空串 = void 返回不接管
    void emitCall(const std::string& result, const std::string& retType,
              const std::string& fnName,
              const std::vector<std::pair<std::string, std::string>>& args);
    void emitRet(const std::string& type, const std::string& value);
    void emitRetVoid();
    void emitBr(const std::string& labelName);
    void emitBrCond(const std::string& cond, const std::string& thenLabel,
                const std::string& elseLabel);
    // GEP：indices 一律按 i32 打印；elemType 是容器类型（[N x T] 或 %struct.X）
    void emitGep(const std::string& result, const std::string& elemType,
             const std::string& ptr, const std::vector<std::string>& indices);

    std::string str() const { return module_.emit(); }

private:
    Module module_;
    int regNo_ = 0;
    std::unordered_map<std::string, int> labelUse_;
    int strNo_ = 0;

    void append(Instr i);   // 追加到当前函数当前块（空函数时自动开 "entry" 块）
};

} // namespace ll
