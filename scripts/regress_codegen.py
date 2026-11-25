#!/usr/bin/env python3
# Codegen 套件回归：官方新仓库 reference/rx-testcases-2025/codegen/*（manifest 驱动）
# 流程：compiler --emit-llvm → clang → riscv 汇编 → ravel 模拟 → diff 输出
# 用法: python3 scripts/regress_codegen.py [目录名过滤子串...]
import subprocess, sys, tempfile, os, glob, shutil, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "reference/rx-testcases-2025/codegen")
BUILTIN = os.path.join(ROOT, "runtime.c")
COMPILER = os.path.join(ROOT, "build/compiler")
CLANG = shutil.which("clang-15") or shutil.which("clang")
RAVEL = os.path.expanduser("~/tools/ravel/install/bin/ravel")

passed, errors = [], []
filters = sys.argv[1:]
cats = sorted(d for d in os.listdir(SRC) if os.path.isdir(os.path.join(SRC, d)))
for cat in cats:
    if filters and not any(f in cat for f in filters):
        continue
    cdir = os.path.join(SRC, cat)
    mf = os.path.join(cdir, "manifest.json")
    if not os.path.isfile(mf):
        continue
    for entry in json.load(open(mf)):
        src = os.path.join(cdir, entry["source"])
        name = f"{cat}/{entry['source']}"
        try:
            with tempfile.TemporaryDirectory() as td:
                # 1. .rx → .ll（同一 source 只编一次）
                s_test = os.path.join(td, "test.s")
                if not os.path.exists(s_test):
                    with open(src) as f:
                        r = subprocess.run([COMPILER, "--emit-llvm"], stdin=f,
                                           capture_output=True, timeout=60)
                    if r.returncode != 0:
                        errors.append((name, "compile: " + r.stderr.decode()[:300]))
                        continue
                    ll = os.path.join(td, "t.ll")
                    open(ll, "wb").write(r.stdout)
                    for s, out, extra in [
                            (ll, "test.s", ["-O0", "-x", "ir"]),
                            (BUILTIN, "builtin.s", ["-O2", "-fno-builtin"])]:
                        c = subprocess.run([CLANG, "-S",
                                            "--target=riscv32-unknown-none-elf",
                                            "-march=rv32im", "-mabi=ilp32",
                                            "-mllvm", "-riscv-no-aliases",
                                            *extra, s, "-o", out + ".src"],
                                           cwd=td, capture_output=True, timeout=60)
                        if c.returncode != 0:
                            raise RuntimeError("clang: " + c.stderr.decode()[:300])
                        txt = open(os.path.join(td, out + ".src")).read().replace("@plt", "")
                        open(os.path.join(td, out), "w").write(txt)
                # 2. 每个 io 对跑一次 ravel
                for io in entry.get("io", []):
                    inp = io["input"]
                    inp_path = os.path.join(cdir, inp) if inp else os.path.join(td, "empty.in")
                    if not io["input"]:
                        open(inp_path, "w").close()
                    out_f = os.path.join(td, "test.out")
                    rv = subprocess.run([RAVEL, f"--input-file={inp_path}",
                                         f"--output-file={out_f}",
                                         s_test, os.path.join(td, "builtin.s")],
                                        capture_output=True, timeout=120)
                    if rv.returncode != 0:
                        errors.append((name, "ravel: " + (rv.stderr.decode() or rv.stdout.decode())[:300]))
                        break
                    got = open(out_f).read()
                    want = open(os.path.join(cdir, io["output"])).read()
                    if [l.rstrip() for l in got.splitlines() if l.strip()] != \
                       [l.rstrip() for l in want.splitlines() if l.strip()]:
                        errors.append((name, f"output mismatch: got {got[:120]!r} want {want[:120]!r}"))
                        break
                else:
                    passed.append(name)
        except subprocess.TimeoutExpired:
            errors.append((name, "timeout"))
        except Exception as ex:
            errors.append((name, str(ex)[:300]))

for n in passed:
    print(f"PASS {n}")
for n, why in errors:
    print(f"FAIL {n}: {why}")
print(f"TOTAL: {len(passed)}/{len(passed) + len(errors)}")
