#include "ir/type_map.h"

namespace irtypes {

ll::Type llvmType(const Type& t) {
    switch (t.kind) {
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::Isize:
        case TypeKind::Usize:
        case TypeKind::IntLit:  return ll::Type::i32();
        case TypeKind::Bool:
        case TypeKind::Char:    return ll::Type::i8();
        case TypeKind::F32:     return ll::Type::f32();
        case TypeKind::F64:     return ll::Type::f64();
        case TypeKind::Unit:
        case TypeKind::Never:   return ll::Type::voidT();
        case TypeKind::Str:
        case TypeKind::Ref:     return ll::Type::ptr();
        case TypeKind::Array:   return ll::Type::array(llvmType(*t.elem), t.arrayLen);
        case TypeKind::Named:   return ll::Type::struct_(t.name);
        case TypeKind::Unknown: return ll::Type::i32();  // 兜底，语义阶段已拦过
    }
    return ll::Type::i32();
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

ll::Value defaultValue(const Type& t) {
    switch (t.kind) {
        case TypeKind::Str:
        case TypeKind::Ref:     return ll::Value::imm("null");
        default:                return ll::Value::imm("0");
    }
}

}  // namespace irtypes
