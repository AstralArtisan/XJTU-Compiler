#!/usr/bin/env python3
"""端到端回归脚本：跑 1.src ~ 33.src 经 ./compiler parse，按每份用例末尾注释推断预期。

用法：
    python tests/run_parse.py [--compiler PATH] [--grammar PATH]

输出：
    一份四列表格：用例 | expected | got | OK
    最后是统计行：N pass · M fail-as-expected · K mismatch
"""

from __future__ import annotations
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_COMPILER = REPO / "compiler" / ("compiler.exe" if sys.platform.startswith("win") else "compiler")
DEFAULT_GRAMMAR  = "data/c_lite.grammar"

# 注释里出现这些汉字 / 短语，视为 should-fail
FAIL_PATTERNS = re.compile(r"错误|不支持|未声明|未定义|不一致|溢出")

# 部分用例虽未带注释，但实际含违反 PPT 主文法 / 附录 A 词法的瑕疵（经老师确认）：
# 5  print a 中 a 未声明；
# 11 void acc() 却 return 0，add(3.48,) 单参对双形参；
# 21 形参缺末尾分号 + 数组花括号初始化 + i/j 未声明；
# 22 第 6 行 `}；` 全角分号；
# 23 if-else 后多写 `;`；
# 24 形参缺末尾分号 + 声明同时赋值；
# 27 内层 if-else 后多写 `;`；
# 28 `max(arr[],)` 中 arr[] 不是合法实参形态；
# 29 数组花括号初始化 + 笔误；
# 30 第 2 行 `int result；` 全角分号；
# 31 第 4 行 `}；` 全角分号。
EXTRA_EXPECTED_FAIL = {"5","11","21","22","23","24","27","28","29","30","31"}


def expected_status(src: Path) -> str:
    """先按文件末尾注释推断，再用 EXTRA_EXPECTED_FAIL 兜底。"""
    if src.stem in EXTRA_EXPECTED_FAIL:
        return "fail"
    text = src.read_bytes().decode("utf-8", errors="replace")
    for raw in text.splitlines()[::-1][:8]:
        line = raw.strip()
        if "//" in line:
            line = line.split("//", 1)[1]
            if FAIL_PATTERNS.search(line):
                return "fail"
    return "pass"


def run_parse(compiler: Path, grammar: str, src: Path) -> str:
    """返回 'pass' / 'fail' / 'crash'。"""
    try:
        res = subprocess.run(
            [str(compiler), "parse", "-f", str(src), "--grammar", grammar, "--format=json"],
            capture_output=True, text=True, errors="replace",
            timeout=15, cwd=str(compiler.parent),
        )
    except Exception:
        return "crash"
    if not res.stdout.strip():
        return "crash"
    stdout = res.stdout
    brace = stdout.find("{")
    if brace > 0:
        stdout = stdout[brace:]
    try:
        data = json.loads(stdout)
    except json.JSONDecodeError:
        return "crash"
    return "pass" if data.get("accepted") else "fail"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", default=str(DEFAULT_COMPILER))
    ap.add_argument("--grammar",  default=DEFAULT_GRAMMAR)
    args = ap.parse_args()

    compiler = Path(args.compiler).resolve()
    if not compiler.exists():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    tests_dir = REPO / "tests"
    cases = sorted(tests_dir.glob("*.src"), key=lambda p: int(p.stem))

    print(f"{'case':<8} {'expected':<10} {'got':<10} OK")
    print("-" * 40)
    pass_correct = fail_correct = mismatch = 0
    for src in cases:
        exp = expected_status(src)
        got = run_parse(compiler, args.grammar, src)
        ok  = (exp == got)
        if ok and got == "pass": pass_correct += 1
        elif ok and got == "fail": fail_correct += 1
        else: mismatch += 1
        print(f"{src.name:<8} {exp:<10} {got:<10} {'OK ' if ok else 'XX '}")
    print("-" * 40)
    print(f"summary: {pass_correct} pass · {fail_correct} fail-as-expected · {mismatch} mismatch")
    return 0 if mismatch == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
