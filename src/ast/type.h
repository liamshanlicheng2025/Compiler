#pragma once

// ============================================================
// 类型表示：AST 存"类型标注的语法结构"，语义阶段直接复用
//
// 设计：判别枚举 TypeKind + 按需启用的字段（带标签联合体思路）。
//   - 基础类型（i32/bool/...）：只有 kind
//   - Array：elem + arrayLen
//   - Ref：elem + isMut
//   - Named（struct/enum 名）：name，语义阶段按名字回查符号表
// 子类型用 shared_ptr 持有，与 AST 节点的内存策略一致。
// ============================================================

#include <cstddef>
#include <memory>
#include <string>

enum class TypeKind {
    I32, U32, Isize, Usize,   // 整型（测试集里 i32/usize 最常见）
    IntLit,                   // 整数字面量的未定型：兼容任何整型 kind，
                              // 落到变量符号/数组元素上时归一化为 I32
    F32, F64,                 // 浮点
    Bool, Char,
    Str,                      // 字符串字面量类型 &str 先按 Str 处理
    Unit,                     // ()：无返回值/空块
    Array,                    // [elem; arrayLen]
    Ref,                      // &elem / &mut elem（isMut 区分）
    Named,                    // struct/enum 等具名类型，name 存名字
    Never,                    // !：return/break/continue 的发散类型
    Unknown                   // 无法识别/出错后的兜底，避免级联报错
};

struct Type {
    TypeKind kind = TypeKind::Unknown;
    std::string name;                  // Named：类型名
    std::shared_ptr<Type> elem;        // Array/Ref：子类型
    std::size_t arrayLen = 0;          // Array：长度
    std::string arrayLenExpr;          // Array：长度表达式原文（非常量字面量时
                                       // 由语义阶段解析，如 const 名；为空=无）
    bool isMut = false;                // Ref：是否 &mut

    // 打印用："i32"、"&mut [i32; 5]"、"Point"（已实现，ASTPrinter 依赖）
    std::string toString() const;

    // 语义阶段用：两个类型是否匹配。

    bool equal(const Type& other) const;
};
