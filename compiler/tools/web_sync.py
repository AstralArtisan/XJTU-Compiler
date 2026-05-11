#!/usr/bin/env python3
"""Copy or verify the GitHub Pages files generated from web/."""

from __future__ import annotations

import argparse
import filecmp
import shutil
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
WEB_DIR = REPO_ROOT / "web"
DOCS_DIR = REPO_ROOT / "docs"
FILES = ("index.html", "style.css", "app.js")


def sync() -> None:
    DOCS_DIR.mkdir(exist_ok=True)
    for name in FILES:
        shutil.copy2(WEB_DIR / name, DOCS_DIR / name)
    print("docs/ synced from web/")


def check() -> int:
    mismatches = []
    for name in FILES:
        src = WEB_DIR / name
        dst = DOCS_DIR / name
        if not dst.exists() or not filecmp.cmp(src, dst, shallow=False):
            mismatches.append(name)

    if mismatches:
        print("docs/ differs from web/: " + ", ".join(mismatches))
        return 1

    print("docs/ matches web/")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="only verify that docs/ matches web/")
    args = parser.parse_args()
    if args.check:
        return check()
    sync()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
