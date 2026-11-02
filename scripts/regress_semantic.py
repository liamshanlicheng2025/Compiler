#!/usr/bin/env python3
# 旧官方套件回归：reference/RCompiler-Testcases/semantic-{1,2}
# 判定：global.json 里 compileexitcode 0 = 应编译通过，-1 = 应拒绝；
#       我们编译器的退出码 0 = 通过，1 = 语法/语义错误
# 用法：在仓库根目录跑  python3 scripts/regress_semantic.py

import json
import subprocess
import sys
from pathlib import Path

COMPILER = "./build/compiler"
SUITES = ["semantic-1", "semantic-2"]
ROOT = Path("reference/RCompiler-Testcases")


def main() -> int:
    total_ok = total = 0
    failed = []
    for suite in SUITES:
        cases = json.load(open(ROOT / suite / "global.json"))
        ok = 0
        for c in cases:
            if not c.get("active", True):
                continue
            src = ROOT / suite / c["source"][0]
            expect_pass = c["compileexitcode"] == 0
            r = subprocess.run([COMPILER, str(src)],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=30)
            if (r.returncode == 0) == expect_pass:
                ok += 1
            else:
                failed.append(f"{suite}/{c['name']}")
        n = len([c for c in cases if c.get("active", True)])
        total_ok += ok
        print(f"{suite}: {ok}/{n}")
    if failed:
        print("FAILED:", ", ".join(failed))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
