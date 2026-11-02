#include "ir/ir_builder.h"

#include <cstdio>

std::string IRBuilder::freshReg() {
    return "%" + std::to_string(++regNo_);
}

std::string IRBuilder::freshLabel(const std::string& base) {
    int& n = labelUse_[base];
    return n++ == 0 ? base : base + "." + std::to_string(n - 1);
}

std::string IRBuilder::addString(const std::string& content) {
    std::string name = "@.str." + std::to_string(strNo_++);
    strings_.emplace_back(name, content);
    return name;
}

void IRBuilder::emit(const std::string& insn) {
    body_ << "  " << insn << "\n";
}

void IRBuilder::emitAssign(const std::string& reg, const std::string& insn) {
    body_ << "  " << reg << " = " << insn << "\n";
}

void IRBuilder::label(const std::string& name) {
    body_ << name << ":\n";
}

void IRBuilder::raw(const std::string& line) {
    module_ << line << "\n";
}

void IRBuilder::rawBody(const std::string& line) {
    body_ << line << "\n";
}

void IRBuilder::comment(const std::string& text) {
    body_ << "  ; " << text << "\n";
}

void IRBuilder::blankLine() {
    module_ << "\n";
}

void IRBuilder::beginFunction() {
    regNo_ = 0;
    labelUse_.clear();
}

// LLVM c"..." 字符串转义：不可打印字符、引号、反斜杠都写成 \XX（十六进制）
static std::string escapeString(const std::string& s) {
    std::string out;
    char buf[4];
    for (unsigned char c : s) {
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            std::snprintf(buf, sizeof buf, "\\%02X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string IRBuilder::str() const {
    std::ostringstream out;
    out << module_.str();
    for (const auto& [name, content] : strings_) {
        out << name << " = private unnamed_addr constant ["
            << (content.size() + 1) << " x i8] c\"" << escapeString(content)
            << "\\00\"\n";
    }
    if (!strings_.empty()) out << "\n";
    out << body_.str();
    return out.str();
}
