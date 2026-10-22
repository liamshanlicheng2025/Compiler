#include "symbol_table.h"

// ============================================================
// 符号表实现：作用域栈管理（enterScope/exitScope）+ 插入/查找 +
// 内建函数注册。
// ============================================================

SymbolTable::SymbolTable() {
    global_ = std::make_shared<Scope>(nullptr);
    current_ = global_;
}

void SymbolTable::enterScope() {
    current_ = std::make_shared<Scope>(current_);
}

void SymbolTable::exitScope() {
    if (current_->parent()) current_ = current_->parent();
    // 弹出的作用域 shared_ptr 计数归零自动回收，不用手动 delete
}


bool Scope::insert(const Symbol& sym) {
    if (symbols_.find(sym.name) != symbols_.end()) return false;
    symbols_[sym.name] = sym;
    return true;
}

Symbol* Scope::lookupLocal(const std::string& name) {
    auto it = symbols_.find(name);
    if (it == symbols_.end()) return nullptr;
    return &it -> second;
}

bool SymbolTable::insert(const Symbol& sym) {
    return current_ -> insert(sym);
}

Symbol* SymbolTable::lookup(const std::string& name) {
    auto scope = current_;
    while (scope){
        if (auto* s = scope -> lookupLocal(name)) return s;
        scope = scope -> parent();
    }
    return nullptr;
}

void SymbolTable::registerBuiltins() {
    Symbol sym;
    sym.kind = SymbolKind::Function;
    sym.name = "printlnInt";
    sym.paramTypes = {Type{TypeKind::I32}};
    sym.type = Type{TypeKind::Unit};
    global_ -> insert(sym);
    sym.name = "printInt";
    sym.paramTypes = {Type{TypeKind::I32}};
    sym.type = Type{TypeKind::Unit};
    global_ -> insert(sym);
    sym.name = "getInt";
    sym.paramTypes = {};                        // 无参数 = 空 vector
    sym.type = Type{TypeKind::I32};
    global_ -> insert(sym);
    sym.name = "exit";
    sym.paramTypes = {Type{TypeKind::I32}};
    sym.type = Type{TypeKind::Unit};
    global_ -> insert(sym);
    // 新官方测试集（rx-compiler-testcases）的 snake_case 别名，签名相同
    sym.name = "println_i32";
    sym.paramTypes = {Type{TypeKind::I32}};
    sym.type = Type{TypeKind::Unit};
    global_ -> insert(sym);
    sym.name = "print_i32";
    global_ -> insert(sym);              // 参数/返回类型与 println_i32 相同
    sym.name = "get_i32";
    sym.paramTypes = {};
    sym.type = Type{TypeKind::I32};
    global_ -> insert(sym);
    // 内建类型 String：注册为 StructType 使 let x: String 等标注可查；
    // structDef 为空（无字段表，FieldExpr 有专门的空指针拦截）
    Symbol strType;
    strType.kind = SymbolKind::StructType;
    strType.name = "String";
    global_ -> insert(strType);
}
