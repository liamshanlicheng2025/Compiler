#pragma once

// ============================================================
// 类型映射表：源语言 Type → LLVM 类型字符串
//
// 约定（设计见 docs/ir-overview-and-plan.md §4.1 / §5）：
//   i32/u32/isize/usize/整数字面量 → i32（有符号性不进类型，
//       选指令时查 exprTypes 旁表，用 isSignedInt 判断）
//   bool/char → i8（不用 i1，兼容 printf %d，省 zext/trunc）
//   () / !    → void
//   &T / &str → ptr（opaque pointer，不区分指向）
//   [T; N]    → [N x T]
//   struct X  → %struct.X（字段类型见 IRGenerator::visit(StructDef)）
// ============================================================

#include "ast/type.h"

#include <string>

namespace irtypes {

std::string llvmType(const Type& t);

// 有符号整型：除法选 sdiv、右移选 ashr、比较选 icmp s* 时用
bool isSignedInt(const Type& t);

// 四种整型 + 整数字面量
bool isIntLike(const Type& t);

// 该类型的零值常量文本："0" / "0.0" / "null"；void 返回空串
std::string defaultValue(const Type& t);

}  // namespace irtypes
