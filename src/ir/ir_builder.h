#pragma once

// ============================================================
// IRBuilder：LLVM IR 文本的拼装器
//
// 不建 IR 对象图，直接拼文本（设计见 docs/ir-overview-and-plan.md §5）。
// 两个输出区：
//   模块区 module_ —— target、declare、%struct 类型定义（顶格）
//   函数区 body_   —— define 函数体（指令缩进两格，标签顶格）
// 字符串字面量先登记，str() 时统一输出为全局常量。
//
// 编号规则（LLVM 文本 IR 的硬性要求）：
//   虚拟寄存器 %1 %2 ... 函数内唯一，beginFunction 时重置
//   基本块标签          函数内唯一，首次用基名本身，之后基名.1 .2 ...
// ============================================================

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class IRBuilder {
public:
    // ---- 编号器 ----
    std::string freshReg();                            // "%1" "%2" ...
    std::string freshLabel(const std::string& base);   // "if.then" / "if.then.1" ...
    std::string addString(const std::string& content); // 登记字符串字面量，返回 "@.str.0"

    // ---- 输出 ----
    void emit(const std::string& insn);     // 函数体内一行指令（自动缩进）
    // "%reg = insn" 的便捷写法
    void emitAssign(const std::string& reg, const std::string& insn);
    void label(const std::string& name);    // 基本块标签行（顶格 + 冒号）
    void raw(const std::string& line);      // 模块区一行（顶格）
    void rawBody(const std::string& line);  // 函数区一行顶格（define 头、收尾的 }）
    void comment(const std::string& text);  // 函数体内注释行（; 开头）
    void blankLine();                       // 模块区空行

    void beginFunction();   // 进入新函数：重置寄存器/标签编号
    void endFunction() {}   // 目前无收尾工作，留对称接口

    std::string str() const;   // 完整模块文本：模块区 + 字符串常量区 + 函数区

private:
    std::ostringstream module_;
    std::ostringstream body_;
    int regNo_ = 0;
    std::unordered_map<std::string, int> labelUse_;
    int strNo_ = 0;
    std::vector<std::pair<std::string, std::string>> strings_;  // (全局名, 原始内容)
};
