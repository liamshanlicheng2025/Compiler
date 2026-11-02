#include "ir/type_map.h"

namespace irtypes {

std::string llvmType(const Type& t) {
    switch (t.kind) {
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::Isize:
        case TypeKind::Usize:
        case TypeKind::IntLit:  return "i32";
        case TypeKind::Bool:
        case TypeKind::Char:    return "i8";
        case TypeKind::F32:     return "float";
        case TypeKind::F64:     return "double";
        case TypeKind::Unit:
        case TypeKind::Never:   return "void";
        case TypeKind::Str:
        case TypeKind::Ref:     return "ptr";
        case TypeKind::Array:
            return "[" + std::to_string(t.arrayLen) + " x "
                 + llvmType(*t.elem) + "]";
        case TypeKind::Named:   return "%struct." + t.name;
        case TypeKind::Unknown: return "i32";  // 兜底，语义阶段已拦过
    }
    return "i32";
}

bool isSignedInt(const Type& t) {
    return t.kind == TypeKind::I32 || t.kind == TypeKind::Isize
        || t.kind == TypeKind::IntLit;
}

bool isIntLike(const Type& t) {
    return t.kind == TypeKind::I32 || t.kind == TypeKind::U32
        || t.kind == TypeKind::Isize || t.kind == TypeKind::Usize
        || t.kind == TypeKind::IntLit;
}

std::string defaultValue(const Type& t) {
    switch (t.kind) {
        case TypeKind::Unit:
        case TypeKind::Never:   return "";
        case TypeKind::F32:
        case TypeKind::F64:     return "0.0";
        case TypeKind::Str:
        case TypeKind::Ref:     return "null";
        default:                return "0";
    }
}

}  // namespace irtypes
