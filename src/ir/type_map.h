#pragma once

// ============================================================
// 类型映射表：源语言 Type → ll::Type（结构化 LLVM 类型）
//
// 约定（设计见 docs/ir-overview-and-plan.md §4.1 / §5）：
//   i32/u32/isize/usize/整数字面量 → i32（有符号性不进类型，
//       选指令时查 exprTypes 旁表，用 isSignedInt 判断）
//   bool/char → i8（不用 i1，兼容 printf %d，省 zext/trunc）
//   () / !    → void
//   &T / &str → ptr（opaque pointer，不区分指向）
//   [T; N]    → Array(N, 元素类型)
//   struct X  → Struct("X")（字段布局 codegen 查布局表，
//       定义见 IRGenerator::visit(StructDef)）
// ============================================================

#include "ast/type.h"
#include "ir/ll.h"

#include <string>

namespace irtypes {

ll::Type llvmType(const Type& t);

// 有符号整型：除法选 sdiv、右移选 ashr、比较选 icmp s* 时用
bool isSignedInt(const Type& t);

// 四种整型 + 整数字面量
bool isIntLike(const Type& t);

// 该类型的零值常量："0" / "0.0" / null；void 返回空（不可用）
ll::Value defaultValue(const Type& t);

}  // namespace irtypes
