#!/usr/bin/env python3
# IR 回归：本地跑官方 IR-1 套件（reference/RCompiler-Testcases/IR-1/src/*）
# 流程与评测一致：compiler --emit-llvm → clang → riscv 汇编 → ravel 模拟 →
# diff 输出与期望 .out
import subprocess, sys, tempfile, os, glob, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "reference/RCompiler-Testcases/IR-1/src")
BUILTIN = os.path.join(ROOT, "reference/RCompiler-Testcases/IR-1/builtin/builtin.c")
COMPILER = os.path.join(ROOT, "build/compiler")
CLANG = shutil.which("clang-15") or shutil.which("clang")
RAVEL = os.path.expanduser("~/tools/ravel/install/bin/ravel")

passed, failed, errors = [], [], []
cases = sorted(os.listdir(SRC)) if len(sys.argv) < 2 else sys.argv[1:]
for name in cases:
    d = os.path.join(SRC, name)
    rx = os.path.join(d, name + ".rx")
    if not os.path.isfile(rx):
        continue
    try:
        with tempfile.TemporaryDirectory() as td:
            # 1. 编译 .rx → .ll
            with open(rx) as f:
                r = subprocess.run([COMPILER, "--emit-llvm"], stdin=f,
                                   capture_output=True, timeout=30)
            if r.returncode != 0:
                errors.append((name, "compile: " + r.stderr.decode()[:200]))
                continue
            ll = os.path.join(td, "t.ll")
            open(ll, "wb").write(r.stdout)
            # 2. .ll → riscv 汇编；builtin 同理
            for src, out, extra in [
                    (ll, "test.s", ["-O0", "-x", "ir"]),
                    (BUILTIN, "builtin.s", ["-O2", "-fno-builtin"])]:
                c = subprocess.run([CLANG, "-S",
                                    "--target=riscv32-unknown-none-elf",
                                    "-march=rv32im", "-mabi=ilp32",
                                    "-mllvm", "-riscv-no-aliases",
                                    *extra, src, "-o", os.path.join(td, out + ".src")],
                                   capture_output=True, timeout=60)
                if c.returncode != 0:
                    raise RuntimeError("clang: " + c.stderr.decode()[:200])
                # 去 @plt
                txt = open(os.path.join(td, out + ".src")).read().replace("@plt", "")
                open(os.path.join(td, out), "w").write(txt)
            # 3. ravel 模拟
            inp = os.path.join(d, name + ".in")
            out_f = os.path.join(td, "test.out")
            rv = subprocess.run([RAVEL, f"--input-file={inp}",
                                 f"--output-file={out_f}",
                                 os.path.join(td, "test.s"),
                                 os.path.join(td, "builtin.s")],
                                capture_output=True, timeout=120)
            if rv.returncode != 0:
                errors.append((name, "ravel: " + rv.stderr.decode()[:200]))
                continue
            got = open(out_f).read()
            want = open(os.path.join(d, name + ".out")).read()
            # diff -ZB：忽略行尾空白与末尾空行
            if [l.rstrip() for l in got.splitlines() if l.strip()] != \
               [l.rstrip() for l in want.splitlines() if l.strip()]:
                errors.append((name, f"output mismatch: got {got[:80]!r} want {want[:80]}"))
                continue
            passed.append(name)
    except subprocess.TimeoutExpired:
        errors.append((name, "timeout"))
    except Exception as ex:
        errors.append((name, str(ex)[:200]))

for n in passed:
    print(f"PASS {n}")
for n, why in errors:
    print(f"FAIL {n}: {why}")
print(f"TOTAL: {len(passed)}/{len(passed) + len(errors)}")
