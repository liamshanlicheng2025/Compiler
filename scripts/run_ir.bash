#!/bin/bash
# 本地 IR 流水线（照官方 test_llvm.bash 的参数，模拟器用 ravel）：
#   <input.ll> → clang 翻译成 riscv32 汇编 → 和 builtin.s 一起交给 ravel 跑
# 用法: scripts/run_ir.bash <input.ll> [test.in]
#   test.in 省略时用空输入。程序输出打到 stdout，退出码是最后一行。

set -e

LL=$1
IN=${2:-/dev/null}
CLANG=$(which clang-15 || which clang)
RAVEL=$HOME/tools/ravel/install/bin/ravel
BUILTIN=reference/RCompiler-Testcases/IR-1/builtin/builtin.c

TEMPDIR=$(mktemp -d -p /tmp ir.XXXXXXXXXX)
trap 'rm -rf "$TEMPDIR"' EXIT

# .ll → RISC-V 汇编；builtin.c 同 target 编译（-O2 -fno-builtin 照官方）
$CLANG -S --target=riscv32-unknown-elf "$LL" -o "$TEMPDIR/test.s.source"
$CLANG -S --target=riscv32-unknown-elf -O2 -fno-builtin "$BUILTIN" -o "$TEMPDIR/builtin.s.source"
# ravel 不认 @plt 后缀，剥掉
sed 's/@plt//g' "$TEMPDIR/test.s.source" > "$TEMPDIR/test.s"
sed 's/@plt//g' "$TEMPDIR/builtin.s.source" > "$TEMPDIR/builtin.s"

cp "$IN" "$TEMPDIR/test.in" 2>/dev/null || touch "$TEMPDIR/test.in"
"$RAVEL" --input-file="$TEMPDIR/test.in" --output-file="$TEMPDIR/test.out" \
    "$TEMPDIR/test.s" "$TEMPDIR/builtin.s" > "$TEMPDIR/ravel_stdout.txt"
echo "---- 程序输出 ----"
cat "$TEMPDIR/test.out"
echo "---- ravel 报告（含退出码/耗时）----"
cat "$TEMPDIR/ravel_stdout.txt"
