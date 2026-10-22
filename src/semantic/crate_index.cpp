#include "semantic/crate_index.h"

#include "../ast/type.h"
#include "semantic/semantic_error.h"


void CrateIndex::run(const CratePtr& crate) {
    symtab_.registerBuiltins();   // 内建函数先于一切注册
    crate->accept(*this);
    checkEntryPoint(crate);
}

// 顶层分发：visit(Crate&) 拿到的是 ItemPtr（共享指针），
// 用 dynamic_pointer_cast 判断具体类型并转成对应智能指针——
// 这样注册进符号表的 shared_ptr<const StructDef> 有合法的来源
// （如果走 accept 分发，visit(StructDef&) 只拿到引用，无法构造 shared_ptr）
void CrateIndex::visit(Crate& node) {
    for (auto& item : node.items) {
        if (auto fn = std::dynamic_pointer_cast<Function>(item))       registerFunction(fn);
        else if (auto s = std::dynamic_pointer_cast<StructDef>(item))  registerStruct(s);
        else if (auto c = std::dynamic_pointer_cast<ConstDef>(item))   registerConst(c);
        else if (auto s = std::dynamic_pointer_cast<StaticDef>(item))  registerStatic(s);
        else if (auto im = std::dynamic_pointer_cast<ImplBlock>(item)) registerImpl(im);
        // enum 只注册名字（SymbolKind::EnumType）：变体 PathExpr 的类型 =
        // 该名字的 Named，不做更深入的检查
        else if (auto en = std::dynamic_pointer_cast<EnumDef>(item)) {
            Symbol sym;
            sym.kind = SymbolKind::EnumType;
            sym.name = en->name;
            if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
        }
    }
}

// 入口检查：main 必须存在、无泛型参数、无参数、无返回类型标注（或显式标注 -> ()）。
// 同名 main 重复定义已被 registerFunction 拦截，此处无需再查。
void CrateIndex::checkEntryPoint(const CratePtr& crate) {
    FunctionPtr mainFn = nullptr;
    for (auto& item : crate -> items) {
        if (auto fn = std::dynamic_pointer_cast<Function>(item)) {
            if (fn -> name == "main") { mainFn = fn; break; }
        }
    }
    if (!mainFn) semaError("缺少 main 函数");
    if (mainFn -> hasGenericParams) semaError("main 不能有泛型参数");
    if (!mainFn -> params.empty()) semaError("main 不能有参数");
    if (mainFn -> returnType && mainFn -> returnType -> kind != TypeKind::Unit)
        semaError("main 不能声明返回类型");
}


// 注册函数签名：参数类型表 + 返回类型（无标注 = Unit），重复定义报错
void CrateIndex::registerFunction(const FunctionPtr& fn) {
    Symbol sym;
    sym.kind = SymbolKind::Function;
    sym.name = fn -> name;
    for (auto& param : fn -> params){
        sym.paramTypes.push_back(param.type);
    }
    if (fn -> returnType) sym.type = *fn -> returnType;
    else sym.type = Type{TypeKind::Unit};
    if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
}

// 注册 struct 类型：Symbol 里回指定义节点（Pass 2 查字段用）
void CrateIndex::registerStruct(const std::shared_ptr<StructDef>& def) {
    Symbol sym;
    sym.kind = SymbolKind::StructType;
    sym.name = def -> name;
    sym.structDef = def;
    if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
}

void CrateIndex::registerConst(const std::shared_ptr<ConstDef>& def) {
    Symbol sym;
    sym.kind = SymbolKind::Const;
    sym.name = def -> name;
    sym.type = def -> type;
    if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
}

// 注册全局静态量：isMut 区分 static / static mut
void CrateIndex::registerStatic(const std::shared_ptr<StaticDef>& def) {
    Symbol sym;
    sym.kind = SymbolKind::Static;
    sym.name = def -> name;
    sym.type = def -> type;
    sym.isMut = def -> isMut;
    if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
}

// 注册 impl：校验目标是已定义的 struct，方法挂到 implMethods[类型名]，
// 返回类型里的 Self 归一化为具体类型名
void CrateIndex::registerImpl(const std::shared_ptr<ImplBlock>& def) {
    auto* target = symtab_.lookup(def -> typeName);
    if (!target) semaError("impl 的目标类型未定义：" + def->typeName);
    if (target->kind != SymbolKind::StructType)
        semaError("impl 的目标不是 struct 类型：" + def->typeName);
    auto& existing = symtab_.implMethods[def->typeName];
    for (auto& method : def -> methods) {
        if (method->returnType && method->returnType->kind == TypeKind::Named
            && method->returnType->name == "Self")
            method->returnType->name = def->typeName;
        for (auto& m : existing) {
            if (m->name == method->name)
                semaError("类型 " + def->typeName + " 的方法重复定义：" + method->name);
        }
        existing.push_back(method);
    }
}
