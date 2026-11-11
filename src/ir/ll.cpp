#include "ir/ll.h"

#include <cstdio>

// ============================================================
// ll 实现：结构化指令 → 标准 .ll 文本
// 输出格式与旧的字符串拼装器逐字节一致（指令两格缩进、标签顶格带冒号、
// define 头尾格式、字符串常量区格式相同），迁移期可 diff 验证。
// ============================================================

namespace ll {

// ---- Type ----

Type Type::array(Type elem, int n) {
    Type t;
    t.kind = Kind::Array;
    t.arrayLen = n;
    t.elem = std::make_shared<Type>(std::move(elem));
    return t;
}

Type Type::struct_(std::string name, int bytes) {
    Type t;
    t.kind = Kind::Struct;
    t.structName = std::move(name);
    t.structBytes = bytes;
    return t;
}

int Type::size() const {
    switch (kind) {
        case Kind::I1:
        case Kind::I8:     return 1;
        case Kind::I32:
        case Kind::F32:
        case Kind::Ptr:    return 4;
        case Kind::F64:    return 8;
        case Kind::Array:  return arrayLen * (elem ? elem->size() : 0);
        case Kind::Struct: return structBytes;   // 可能是 -1（未定，查布局表）
        case Kind::Void:   return 0;
    }
    return 0;
}

std::string Type::str() const {
    switch (kind) {
        case Kind::Void:   return "void";
        case Kind::I1:     return "i1";
        case Kind::I8:     return "i8";
        case Kind::I32:    return "i32";
        case Kind::F32:    return "float";
        case Kind::F64:    return "double";
        case Kind::Ptr:    return "ptr";
        case Kind::Array:
            return "[" + std::to_string(arrayLen) + " x "
                 + (elem ? elem->str() : "void") + "]";
        case Kind::Struct: return "%struct." + structName;
    }
    return "void";
}

// ---- emit ----

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
        out << "  ; " << auxStr << "\n";
        return;
    }
    out << "  ";
    if (!result.text.empty()) out << result.text << " = ";
    switch (op) {
        case OpCode::Alloca:
            out << "alloca " << type.str();
            break;
        case OpCode::Load:
            out << "load " << type.str() << ", ptr " << operands[0].text;
            break;
        case OpCode::Store:
            out << "store " << type.str() << " " << operands[0].text
                << ", ptr " << operands[1].text;
            break;
        case OpCode::Add: case OpCode::Sub: case OpCode::Mul:
        case OpCode::SDiv: case OpCode::UDiv:
        case OpCode::SRem: case OpCode::URem:
        case OpCode::And: case OpCode::Or: case OpCode::Xor:
        case OpCode::Shl: case OpCode::AShr: case OpCode::LShr:
            out << opName(op) << " " << type.str() << " " << operands[0].text
                << ", " << operands[1].text;
            break;
        case OpCode::ICmp:
            out << "icmp " << auxStr << " " << type.str() << " "
                << operands[0].text << ", " << operands[1].text;
            break;
        case OpCode::ZExt:
            out << "zext " << type.str() << " " << operands[0].text
                << " to " << auxType.str();
            break;
        case OpCode::Call: {
            out << "call " << type.str() << " @" << args[0].second.text << "(";
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) out << ", ";
                out << args[i].first.str() << " " << args[i].second.text;
            }
            out << ")";
            break;
        }
        case OpCode::Ret:
            out << "ret " << type.str() << " " << operands[0].text;
            break;
        case OpCode::RetVoid:
            out << "ret void";
            break;
        case OpCode::Br:
            out << "br label %" << labels[0];
            break;
        case OpCode::BrCond:
            out << "br i1 " << operands[0].text << ", label %" << labels[0]
                << ", label %" << labels[1];
            break;
        case OpCode::GEP:
            out << "getelementptr inbounds " << auxType.str() << ", ptr "
                << operands[0].text;
            for (size_t i = 1; i < operands.size(); ++i)
                out << ", i32 " << operands[i].text;
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
    out << "define " << retType.str() << " @" << name << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) out << ", ";
        out << params[i].first.str() << " %" << params[i].second;
    }
    out << ") {\n";
    for (const auto& b : blocks) b.emit(out);
    out << "}\n";   // 函数间无空行，文件以 } 收尾
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

Value LLBuilder::freshReg() {
    return Value::reg("%" + std::to_string(regNo_++));
}

std::string LLBuilder::freshLabel(const std::string& base) {
    int& n = labelUse_[base];
    return n++ == 0 ? base : base + "." + std::to_string(n - 1);
}

Value LLBuilder::addString(const std::string& content) {
    std::string name = "@.str." + std::to_string(strNo_++);
    module_.globals.emplace_back(name, content);
    return Value::global(name);
}

void LLBuilder::raw(const std::string& line) {
    module_.header.push_back(line);
}

void LLBuilder::beginFunction(
    const Type& retType, const std::string& name,
    std::vector<std::pair<Type, std::string>> params) {
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
    Instr i;
    i.op = OpCode::Comment;
    i.auxStr = text;
    append(std::move(i));
}

void LLBuilder::emitAlloca(const Value& result, const Type& type) {
    Instr i;
    i.op = OpCode::Alloca;
    i.result = result;
    i.type = type;
    append(std::move(i));
}

void LLBuilder::emitLoad(const Value& result, const Type& type,
                     const Value& ptr) {
    Instr i;
    i.op = OpCode::Load;
    i.result = result;
    i.type = type;
    i.operands = {ptr};
    append(std::move(i));
}

void LLBuilder::emitStore(const Type& type, const Value& value,
                      const Value& ptr) {
    Instr i;
    i.op = OpCode::Store;
    i.type = type;
    i.operands = {value, ptr};
    append(std::move(i));
}

void LLBuilder::emitBinary(const std::string& name, const Value& result,
                       const Type& type, const Value& l, const Value& r) {
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
    Instr i;
    i.op = op;
    i.result = result;
    i.type = type;
    i.operands = {l, r};
    append(std::move(i));
}

void LLBuilder::emitIcmp(const std::string& pred, const Value& result,
                     const Type& type, const Value& l, const Value& r) {
    Instr i;
    i.op = OpCode::ICmp;
    i.result = result;
    i.type = type;
    i.auxStr = pred;
    i.operands = {l, r};
    append(std::move(i));
}

void LLBuilder::emitZext(const Value& result, const Type& fromTy,
                     const Type& toTy, const Value& v) {
    Instr i;
    i.op = OpCode::ZExt;
    i.result = result;
    i.type = fromTy;
    i.auxType = toTy;
    i.operands = {v};
    append(std::move(i));
}

void LLBuilder::emitCall(const Value& result, const Type& retType,
                     const std::string& fnName,
                     const std::vector<std::pair<Type, Value>>& args) {
    Instr i;
    i.op = OpCode::Call;
    i.result = result;
    i.type = retType;
    i.args = args;
    i.args.insert(i.args.begin(), {Type::voidT(), Value::global(fnName)});
    append(std::move(i));
}

void LLBuilder::emitCallVoid(const std::string& fnName,
                     const std::vector<std::pair<Type, Value>>& args) {
    emitCall(Value::reg(""), Type::voidT(), fnName, args);
}

void LLBuilder::emitRet(const Type& type, const Value& value) {
    Instr i;
    i.op = OpCode::Ret;
    i.type = type;
    i.operands = {value};
    append(std::move(i));
}

void LLBuilder::emitRetVoid() {
    Instr i;
    i.op = OpCode::RetVoid;
    append(std::move(i));
}

void LLBuilder::emitBr(const std::string& labelName) {
    Instr i;
    i.op = OpCode::Br;
    i.labels = {labelName};
    append(std::move(i));
}

void LLBuilder::emitBrCond(const Value& cond, const std::string& thenLabel,
                       const std::string& elseLabel) {
    Instr i;
    i.op = OpCode::BrCond;
    i.operands = {cond};
    i.labels = {thenLabel, elseLabel};
    append(std::move(i));
}

void LLBuilder::emitGep(const Value& result, const Type& container,
                    const Value& ptr, const std::vector<Value>& indices) {
    Instr i;
    i.op = OpCode::GEP;
    i.result = result;
    i.auxType = container;
    i.operands.push_back(ptr);
    for (const auto& idx : indices) i.operands.push_back(idx);
    append(std::move(i));
}

} // namespace ll
