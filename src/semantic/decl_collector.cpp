#include "semantic/decl_collector.h"

#include "semantic/semantic_error.h"


void DeclCollector::run(const CratePtr& crate) {
    symtab_.registerBuiltins();   // 内建函数先于一切注册
    crate->accept(*this);
}

// 顶层分发：visit(Crate&) 拿到的是 ItemPtr（共享指针），
// 用 dynamic_pointer_cast 判断具体类型并转成对应智能指针——
// 这样注册进符号表的 shared_ptr<const StructDef> 有合法的来源
// （如果走 accept 分发，visit(StructDef&) 只拿到引用，无法构造 shared_ptr）
void DeclCollector::visit(Crate& node) {
    for (auto& item : node.items) {
        if (auto fn = std::dynamic_pointer_cast<Function>(item))       registerFunction(fn);
        else if (auto s = std::dynamic_pointer_cast<StructDef>(item))  registerStruct(s);
        else if (auto c = std::dynamic_pointer_cast<ConstDef>(item))   registerConst(c);
        else if (auto s = std::dynamic_pointer_cast<StaticDef>(item))  registerStatic(s);
        else if (auto im = std::dynamic_pointer_cast<ImplBlock>(item)) registerImpl(im);
        // enum 只注册名字（SymbolKind::EnumType）：变体 PathExpr 的类型 = 该名字
        // 的 Named（misc11 用 Operation::Add 做值比较），不做更深入的检查
        else if (auto en = std::dynamic_pointer_cast<EnumDef>(item)) {
            Symbol sym;
            sym.kind = SymbolKind::EnumType;
            sym.name = en->name;
            if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
        }
    }
}

// 注册函数签名：参数类型表 + 返回类型（无标注 = Unit），重复定义报错
void DeclCollector::registerFunction(const FunctionPtr& fn) {
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
void DeclCollector::registerStruct(const std::shared_ptr<StructDef>& def) {
    Symbol sym;
    sym.kind = SymbolKind::StructType;
    sym.name = def -> name;
    sym.structDef = def;
    if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
}

void DeclCollector::registerConst(const std::shared_ptr<ConstDef>& def) {
    // 伪代码：Symbol{Const, name, type=def->type}；insert 失败报错
    Symbol sym;
    sym.kind = SymbolKind::Const;
    sym.name = def -> name;
    sym.type = def -> type;
    if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
}

// 注册全局静态量：isMut 区分 static / static mut
void DeclCollector::registerStatic(const std::shared_ptr<StaticDef>& def) {
    Symbol sym;
    sym.kind = SymbolKind::Static;
    sym.name = def -> name;
    sym.type = def -> type;
    sym.isMut = def -> isMut;
    if (!symtab_.insert(sym)) semaError("重复定义：" + sym.name);
}

void DeclCollector::registerImpl(const std::shared_ptr<ImplBlock>& def) {
    // 修正：判断目标是否 struct 要查符号的 kind（SymbolKind::StructType），
    // Symbol::type 存的是类型的 Type，两者无关
    auto* target = symtab_.lookup(def -> typeName);
    if (!target) semaError("impl 的目标类型未定义：" + def->typeName);
    if (target->kind != SymbolKind::StructType)
        semaError("impl 的目标不是 struct 类型：" + def->typeName);
    // 修正：methods 按值存 FunctionPtr，用引用遍历；implMethods 的值是 vector，
    // 没有 find 成员，同名查重要手写循环比名字（比指针地址没意义）
    auto& existing = symtab_.implMethods[def->typeName];
    for (auto& method : def -> methods) {
        // Self 归一化：impl 里声明的 -> Self 改成具体类型名（basic5 的 Stack::new() -> Self）
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
