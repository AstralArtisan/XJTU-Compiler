#!/usr/bin/env python3
"""Cross-platform smoke tests for the compiler CLI."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def compiler_path() -> Path:
    exe = ROOT / ("compiler.exe" if os.name == "nt" else "compiler")
    if exe.exists():
        return exe
    fallback = ROOT / "compiler"
    if fallback.exists():
        return fallback
    return exe


COMPILER = compiler_path()


def run_case(name: str, args: list[str], *, stdin: str | None = None,
             allow_failure: bool = False, json_output: bool = False) -> str:
    print(f"=== {name} ===")
    proc = subprocess.run(
        [str(COMPILER), *args],
        input=stdin,
        text=True,
        capture_output=True,
        cwd=ROOT,
    )
    if proc.stdout:
        lines = proc.stdout.rstrip().splitlines()
        preview = lines if len(lines) <= 12 else lines[:12] + [f"... ({len(lines) - 12} more lines)"]
        print("\n".join(preview))
    if proc.stderr:
        print(proc.stderr.rstrip(), file=sys.stderr)

    if json_output:
        json.loads(proc.stdout)
        print("JSON OK")

    if proc.returncode != 0 and not allow_failure:
        raise SystemExit(f"{name} failed with exit code {proc.returncode}")
    return proc.stdout


def main() -> int:
    if not COMPILER.exists():
        raise SystemExit(f"compiler binary not found: {COMPILER}")

    run_case("scan mode 1", ["scan"], stdin="1\n5\nid if 485 841.6541 www\n")
    run_case("scan unsigned numbers", ["scan"], stdin="1\n5\nid if 485 841.6541 www\n")
    run_case("scan mode 2", ["scan"], stdin="2\nwhile(true){int a=-123;}\n")
    run_case("scan file", ["scan", "-f", "tests/scan/test1.c"])
    run_case("scan regression file", ["scan", "-f", "tests/scan/regression.c"])
    run_case("scan invalid operators", ["scan"], stdin="2\nint x & y | z && q || r;\n")
    run_case("scan compare", ["scan", "--compare", "-f", "tests/scan/sample.c"])
    run_case("dfa enumerate", ["dfa", "data/simple.dfa", "--enumerate", "3"])
    run_case("dfa trace accept", ["dfa", "data/simple.dfa", "--test", "aa", "--trace"])
    run_case("dfa trace reject", ["dfa", "data/simple.dfa", "--test", "aba", "--trace"],
             allow_failure=True)
    run_case("dfa json", ["dfa", "data/simple.dfa", "--format=json"], json_output=True)
    run_case("scan json", ["scan", "-f", "tests/scan/sample.c", "--format=json"],
             json_output=True)
    run_case("lr0 expr (PPT example)", ["lr0", "data/expr.grammar"])
    run_case("lr0 conflicts", ["lr0", "data/expr_ambig.grammar", "--show=conflicts"])
    run_case("lr0 epsilon", ["lr0", "data/eps.grammar", "--show=closure"])
    run_case("lr0 json", ["lr0", "data/expr.grammar", "--format=json"], json_output=True)
    print("All smoke tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
