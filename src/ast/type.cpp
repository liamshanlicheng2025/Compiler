#include "ast/type.h"

// toString 已实现（ASTPrinter 依赖它打印类型标注）；equal 留给语义阶段实现。

std::string Type::toString() const {
    switch (kind) {
        case TypeKind::I32:    return "i32";
        case TypeKind::U32:    return "u32";
        case TypeKind::Isize:  return "isize";
        case TypeKind::Usize:  return "usize";
        case TypeKind::IntLit: return "{integer}";
        case TypeKind::F32:    return "f32";
        case TypeKind::F64:    return "f64";
        case TypeKind::Bool:   return "bool";
        case TypeKind::Char:   return "char";
        case TypeKind::Str:    return "str";
        case TypeKind::Unit:   return "()";
        case TypeKind::Never:  return "!";
        case TypeKind::Unknown:return "?";
        case TypeKind::Named:  return name;
        case TypeKind::Array:
            return "[" + (elem ? elem->toString() : "?") + "; " + std::to_string(arrayLen) + "]";
        case TypeKind::Ref:
            return std::string("&") + (isMut ? "mut " : "") + (elem ? elem->toString() : "?");
    }
    return "?";
}

bool Type::equal(const Type& other) const {
    // 一边是 Unknown（出错兜底）或 Never（return/break 等发散表达式）时视为相等，
    // 避免一次错误级联出大量后续误报
    if (kind == TypeKind::Unknown || kind == TypeKind::Never ||
        other.kind == TypeKind::Unknown || other.kind == TypeKind::Never) return true;
    // 整数字面量（IntLit）兼容任何整型：let x: u32 = 0; 合法；
    // 但两个具体整型之间严格区分（i32 + usize 报错，见测试 type10）
    auto isConcreteInt = [](TypeKind k) {
        return k == TypeKind::I32 || k == TypeKind::U32
            || k == TypeKind::Isize || k == TypeKind::Usize;
    };
    if (kind == TypeKind::IntLit && isConcreteInt(other.kind)) return true;
    if (other.kind == TypeKind::IntLit && isConcreteInt(kind)) return true;
    if (kind == TypeKind::IntLit && other.kind == TypeKind::IntLit) return true;
    if (kind != other.kind) return false;
    // elem 是单个子类型，递归 equal 一次即可
    if (kind == TypeKind::Array){
        // 长度 0 = 未知（数组长度是常量表达式时算不出，如 [usize; N+1]），作通配
        if (arrayLen != 0 && other.arrayLen != 0 && arrayLen != other.arrayLen)
            return false;
        if (!elem || !other.elem) return elem == other.elem;
        return elem->equal(*other.elem);
    }
    if (kind == TypeKind::Ref){
        // 调用方向是 actual.equal(expected)：&mut T 可以当 &T 用（自动 reborrow，
        // basic23/expr38），即 this 是 &mut、other 是 & 时放行；反之不行
        if (isMut != other.isMut && !(isMut && !other.isMut)) return false;
        if (!elem || !other.elem) return elem == other.elem;
        return elem->equal(*other.elem);
    }
    if (kind == TypeKind::Named) return name == other.name;
    return true;
}
