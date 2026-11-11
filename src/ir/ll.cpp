#include "ir/ll.h"

#include <cstdio>

// ============================================================
// ll 实现：结构化指令 → 标准 .ll 文本
// 输出格式与旧 IRBuilder 逐字节一致（指令两格缩进、标签顶格带冒号、
// define 头尾格式、字符串常量区格式相同），迁移期可 diff 验证。
// ============================================================

namespace ll {

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

static const char* opName(OpCode op) {
    switch (op) {
        case OpCode::Add:  return "add";
        case OpCode::Sub:  return "sub";
        case OpCode::Mul:  return "mul";
        case OpCode::SDiv: return "sdiv";
        case OpCode::UDiv: return "udiv";
        case OpCode::SRem: return "srem";
        case OpCode::URem: return "urem";
        case OpCode::And:  return "and";
        case OpCode::Or:   return "or";
        case OpCode::Xor:  return "xor";
        case OpCode::Shl:  return "shl";
        case OpCode::AShr: return "ashr";
        case OpCode::LShr: return "lshr";
        default:           return "?";
    }
}

void Instr::emit(std::ostringstream& out) const {
    if (op == OpCode::Comment) {
        out << "  ; " << operands[0] << "\n";
        return;
    }
    out << "  ";
    if (!result.empty()) out << result << " = ";
    switch (op) {
        case OpCode::Alloca:
            out << "alloca " << type;
            break;
        case OpCode::Load:
            out << "load " << type << ", ptr " << operands[0];
            break;
        case OpCode::Store:
            out << "store " << type << " " << operands[0] << ", ptr " << operands[1];
            break;
        case OpCode::Add: case OpCode::Sub: case OpCode::Mul:
        case OpCode::SDiv: case OpCode::UDiv:
        case OpCode::SRem: case OpCode::URem:
        case OpCode::And: case OpCode::Or: case OpCode::Xor:
        case OpCode::Shl: case OpCode::AShr: case OpCode::LShr:
            out << opName(op) << " " << type << " " << operands[0]
                << ", " << operands[1];
            break;
        case OpCode::ICmp:
            out << "icmp " << aux << " " << type << " " << operands[0]
                << ", " << operands[1];
            break;
        case OpCode::ZExt:
            out << "zext " << type << " " << operands[0] << " to " << aux;
            break;
        case OpCode::Call: {
            out << "call " << type << " @" << args[0].second << "(";
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) out << ", ";
                out << args[i].first << " " << args[i].second;
            }
            out << ")";
            break;
        }
        case OpCode::Ret:
            out << "ret " << type << " " << operands[0];
            break;
        case OpCode::RetVoid:
            out << "ret void";
            break;
        case OpCode::Br:
            out << "br label %" << operands[0];
            break;
        case OpCode::BrCond:
            out << "br i1 " << operands[0] << ", label %" << operands[1]
                << ", label %" << operands[2];
            break;
        case OpCode::GEP:
            out << "getelementptr inbounds " << aux << ", ptr " << operands[0];
            for (size_t i = 1; i < operands.size(); ++i)
                out << ", i32 " << operands[i];
            break;
        case OpCode::Comment:
            break;   // 上面已处理
    }
    out << "\n";
}

void BasicBlock::emit(std::ostringstream& out) const {
    out << name << ":\n";
    for (const auto& i : instrs) i.emit(out);
}

void Function::emit(std::ostringstream& out) const {
    out << "define " << retType << " @" << name << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) out << ", ";
        out << params[i].first << " %" << params[i].second;
    }
    out << ") {\n";
    for (const auto& b : blocks) b.emit(out);
    out << "}\n";   // 与旧 IRBuilder 一致：函数间无空行，文件以 } 收尾
}

std::string Module::emit() const {
    std::ostringstream out;
    for (const auto& line : header) out << line << "\n";
    for (const auto& [name, content] : globals) {
        out << name << " = private unnamed_addr constant ["
            << (content.size() + 1) << " x i8] c\"" << escapeString(content)
            << "\\00\"\n";
    }
    if (!globals.empty()) out << "\n";
    for (const auto& f : functions) f.emit(out);
    return out.str();
}

// ------------------------------------------------------------

std::string LLBuilder::freshReg() {
    return "%" + std::to_string(regNo_++);
}

std::string LLBuilder::freshLabel(const std::string& base) {
    int& n = labelUse_[base];
    return n++ == 0 ? base : base + "." + std::to_string(n - 1);
}

std::string LLBuilder::addString(const std::string& content) {
    std::string name = "@.str." + std::to_string(strNo_++);
    module_.globals.emplace_back(name, content);
    return name;
}

void LLBuilder::raw(const std::string& line) {
    module_.header.push_back(line);
}

void LLBuilder::beginFunction(
    const std::string& retType, const std::string& name,
    std::vector<std::pair<std::string, std::string>> params) {
    regNo_ = 0;
    labelUse_.clear();
    Function f;
    f.retType = retType;
    f.name = name;
    f.params = std::move(params);
    module_.functions.push_back(std::move(f));
    label("entry");   // LLVM 要求函数至少一个基本块，入口块直接开好
}

void LLBuilder::endFunction() {}

void LLBuilder::label(const std::string& name) {
    BasicBlock b;
    b.name = name;
    module_.functions.back().blocks.push_back(std::move(b));
}

void LLBuilder::append(Instr i) {
    auto& f = module_.functions.back();
    if (f.blocks.empty()) label("entry");
    f.blocks.back().instrs.push_back(std::move(i));
}

void LLBuilder::comment(const std::string& text) {
    append({OpCode::Comment, "", "", "", {text}, {}});
}

void LLBuilder::emitAlloca(const std::string& result, const std::string& type) {
    append({OpCode::Alloca, result, type, "", {}, {}});
}

void LLBuilder::emitLoad(const std::string& result, const std::string& type,
                     const std::string& ptr) {
    append({OpCode::Load, result, type, "", {ptr}, {}});
}

void LLBuilder::emitStore(const std::string& type, const std::string& value,
                      const std::string& ptr) {
    append({OpCode::Store, "", type, "", {value, ptr}, {}});
}

void LLBuilder::emitBinary(const std::string& name, const std::string& result,
                       const std::string& type, const std::string& l,
                       const std::string& r) {
    OpCode op = OpCode::Add;
    if (name == "add") op = OpCode::Add;
    else if (name == "sub") op = OpCode::Sub;
    else if (name == "mul") op = OpCode::Mul;
    else if (name == "sdiv") op = OpCode::SDiv;
    else if (name == "udiv") op = OpCode::UDiv;
    else if (name == "srem") op = OpCode::SRem;
    else if (name == "urem") op = OpCode::URem;
    else if (name == "and") op = OpCode::And;
    else if (name == "or") op = OpCode::Or;
    else if (name == "xor") op = OpCode::Xor;
    else if (name == "shl") op = OpCode::Shl;
    else if (name == "ashr") op = OpCode::AShr;
    else if (name == "lshr") op = OpCode::LShr;
    append({op, result, type, "", {l, r}, {}});
}

void LLBuilder::emitIcmp(const std::string& pred, const std::string& result,
                     const std::string& type, const std::string& l,
                     const std::string& r) {
    append({OpCode::ICmp, result, type, pred, {l, r}, {}});
}

void LLBuilder::emitZext(const std::string& result, const std::string& fromTy,
                     const std::string& toTy, const std::string& v) {
    append({OpCode::ZExt, result, fromTy, toTy, {v}, {}});
}

void LLBuilder::emitCall(const std::string& result, const std::string& retType,
                     const std::string& fnName,
                     const std::vector<std::pair<std::string, std::string>>& args) {
    Instr i;
    i.op = OpCode::Call;
    i.result = result;
    i.type = retType;
    i.args = args;
    i.args.insert(i.args.begin(), {"", fnName});
    append(std::move(i));
}

void LLBuilder::emitRet(const std::string& type, const std::string& value) {
    append({OpCode::Ret, "", type, "", {value}, {}});
}

void LLBuilder::emitRetVoid() {
    append({OpCode::RetVoid, "", "", "", {}, {}});
}

void LLBuilder::emitBr(const std::string& labelName) {
    append({OpCode::Br, "", "", "", {labelName}, {}});
}

void LLBuilder::emitBrCond(const std::string& cond, const std::string& thenLabel,
                       const std::string& elseLabel) {
    append({OpCode::BrCond, "", "", "", {cond, thenLabel, elseLabel}, {}});
}

void LLBuilder::emitGep(const std::string& result, const std::string& elemType,
                    const std::string& ptr,
                    const std::vector<std::string>& indices) {
    Instr i;
    i.op = OpCode::GEP;
    i.result = result;
    i.aux = elemType;
    i.operands.push_back(ptr);
    for (auto& idx : indices) i.operands.push_back(idx);
    append(std::move(i));
}

} // namespace ll
