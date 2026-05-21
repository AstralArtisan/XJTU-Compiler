#!/usr/bin/env python3
"""Mirror web/ into docs/ for GitHub Pages deployment.

Recursively syncs everything under web/ (HTML, CSS, JS modules, assets) and
deletes any file in docs/ that no longer exists in web/, so the deployed copy
stays a faithful mirror of the development directory.
"""

from __future__ import annotations

import argparse
import filecmp
import shutil
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
WEB_DIR = REPO_ROOT / "web"
DOCS_DIR = REPO_ROOT / "docs"


def iter_web_files() -> list[Path]:
    return sorted(p for p in WEB_DIR.rglob("*") if p.is_file())


def relative_to_web(path: Path) -> Path:
    return path.relative_to(WEB_DIR)


def sync() -> None:
    DOCS_DIR.mkdir(exist_ok=True)
    expected = set()
    for src in iter_web_files():
        rel = relative_to_web(src)
        dst = DOCS_DIR / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        expected.add(rel)

    # Drop files from docs/ that are no longer present in web/
    for dst in sorted(p for p in DOCS_DIR.rglob("*") if p.is_file()):
        if dst.relative_to(DOCS_DIR) not in expected:
            dst.unlink()

    # Clean up any now-empty directories
    for d in sorted((p for p in DOCS_DIR.rglob("*") if p.is_dir()), reverse=True):
        try:
            d.rmdir()
        except OSError:
            pass

    print("docs/ synced from web/")


def check() -> int:
    mismatches: list[str] = []
    extras: list[str] = []
    expected = set()
    for src in iter_web_files():
        rel = relative_to_web(src)
        expected.add(rel)
        dst = DOCS_DIR / rel
        if not dst.exists() or not filecmp.cmp(src, dst, shallow=False):
            mismatches.append(str(rel))

    for dst in (p for p in DOCS_DIR.rglob("*") if p.is_file()):
        if dst.relative_to(DOCS_DIR) not in expected:
            extras.append(str(dst.relative_to(DOCS_DIR)))

    if mismatches or extras:
        if mismatches:
            print("docs/ differs from web/: " + ", ".join(mismatches))
        if extras:
            print("docs/ has extras not in web/: " + ", ".join(extras))
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
