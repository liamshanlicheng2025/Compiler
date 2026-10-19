#pragma once

// ============================================================
// 符号表：语义分析的地基
//
// 三层结构：
//   Symbol     —— 一个名字的全部信息（变量/函数/类型/常量）
//   Scope      —— 一层作用域：名字 → Symbol 的表 + 指向父作用域的指针
//   SymbolTable—— 作用域栈的管理者：进块压栈、出块弹栈、链式查找
//
// 内存策略与 AST 一致：Scope 用 shared_ptr 持有，离开作用域弹栈后自动回收。
// lookup 返回的 Symbol* 是观察指针（不持有所有权），Symbol 存在 Scope 的表里，
// 作用域弹出后即失效，不得跨作用域保存。
// ============================================================

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/type.h"

class StructDef;
class Function;

enum class SymbolKind {
    Variable,    // let 声明的局部变量（isMut 区分 let / let mut）
    Function,    // 函数（paramTypes + returnType）
    Const,       // const 全局常量（不可赋值；hasConstValue 时带编译期值）
    Static,      // static 全局静态量（isMut 区分 static / static mut）
    StructType,  // struct 定义（structDef 指针拿字段表）
    EnumType     // enum 定义：只注册名字，变体的类型 = Named(名字)
};

struct Symbol {
    SymbolKind kind;
    std::string name;

    // 变量/常量/静态量：自身的类型；函数：返回类型；类型符号：不用
    Type type;

    bool isMut = false;

    // 仅 Function 用
    std::vector<Type> paramTypes;

    // 仅 StructType 用：回指 AST 定义节点，查字段用
    std::shared_ptr<const StructDef> structDef;

    // 仅 Const 用：初值是整数字面量时记下编译期值（数组长度常量解析用）
    bool hasConstValue = false;
    long long constValue = 0;
};

class Scope {
public:
    explicit Scope(std::shared_ptr<Scope> parent) : parent_(std::move(parent)) {}

    // 伪代码：name 已在 symbols_ 里 → 返回 false（重复定义，调用方报错）；
    //         否则插入返回 true
    bool insert(const Symbol& sym);

    // 同层重名直接覆盖（Rust 允许同作用域 let 遮蔽，comprehensive2）
    void upsert(const Symbol& sym) { symbols_[sym.name] = sym; }

    // 伪代码：只查当前层，找到返回指针，没有返回 nullptr
    Symbol* lookupLocal(const std::string& name);

    std::shared_ptr<Scope> parent() const { return parent_; }

private:
    std::shared_ptr<Scope> parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

class SymbolTable {
public:
    SymbolTable();   // 创建全局作用域并设为当前作用域

    // 进入/离开一层作用域（Block、函数体、impl……）
    void enterScope();
    void exitScope();

    // 伪代码：向当前作用域插入；重复定义返回 false
    bool insert(const Symbol& sym);

    // let 遮蔽专用：同层重名覆盖旧符号
    void insertOrShadow(const Symbol& sym) { current_->upsert(sym); }

    // 伪代码：从当前作用域开始沿 parent 链逐层向外找，
    //         找到返回指针，全部没有返回 nullptr（调用方报"未声明"）
    Symbol* lookup(const std::string& name);

    // 伪代码：向全局作用域注册内建函数，在 Pass 1 开始前调用一次：
    //   printlnInt(i32) -> unit
    //   printInt(i32)   -> unit
    //   getInt()        -> i32
    //   exit(i32)       -> unit
    void registerBuiltins();

    std::shared_ptr<Scope> current() const { return current_; }
    std::shared_ptr<Scope> global() const { return global_; }

    // impl 方法表：类型名 → 方法列表。不在作用域体系里（方法是靠
    // receiver 的类型查到的，不是靠名字查到的）。Pass 1 填充，Pass 2 查询。
    std::unordered_map<std::string, std::vector<std::shared_ptr<Function>>> implMethods;

private:
    std::shared_ptr<Scope> global_;
    std::shared_ptr<Scope> current_;
};
