#!/usr/bin/env python3
# 新官方套件回归：reference/rx-compiler-testcases/semantic/*/
# 判定：每个子目录 manifest.json 里 compilation_success = 应否编译通过
# 用法：在仓库根目录跑  python3 scripts/regress_rx.py

import json
import subprocess
import sys
from pathlib import Path

COMPILER = "./build/compiler"
ROOT = Path("reference/rx-compiler-testcases/semantic")


def main() -> int:
    total_ok = total = 0
    for suite_dir in sorted(ROOT.iterdir()):
        manifest = suite_dir / "manifest.json"
        if not manifest.exists():
            continue
        cases = json.load(open(manifest))
        ok = 0
        for c in cases:
            src = suite_dir / c["source"]
            r = subprocess.run([COMPILER, str(src)],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=30)
            if (r.returncode == 0) == bool(c["compilation_success"]):
                ok += 1
        total_ok += ok
        total += len(cases)
        mark = " <<<" if ok < len(cases) else ""
        print(f"{suite_dir.name}: {ok}/{len(cases)}{mark}")
    print(f"TOTAL: {total_ok}/{total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
