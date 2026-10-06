#!/usr/bin/env python3
"""Print `COVERAGE <file> lines <pct> branches <pct>` for each source under test.

Used by tests/host/common.mk (`make coverage`). Parses the summary of `gcov -b -c` (no gcovr
in WSL). The branch figure is gcov's "Taken at least once" (TS-COV-02).

usage: gcov_summary.py [--root DIR] OBJ_DIR SRC...
OBJ_DIR mirrors absolute source paths: SRC=/a/b.cpp has its object at OBJ_DIR/a/b.cpp.o.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

FILE_RE = re.compile(r"^File '(.*)'$")
LINES_RE = re.compile(r"^Lines executed:\s*([0-9.]+)% of (\d+)")
TAKEN_RE = re.compile(r"^Taken at least once:\s*([0-9.]+)% of (\d+)")


def summarise(obj_dir: str, src: str) -> tuple[str, str]:
    obj = os.path.join(obj_dir, src.lstrip("/") + ".o")
    if not os.path.exists(obj):
        sys.exit(f"gcov_summary: no object for {src} at {obj}")
    with tempfile.TemporaryDirectory() as tmp:
        out = subprocess.run(
            ["gcov", "-b", "-c", "-n", "-o", obj, src],
            cwd=tmp, capture_output=True, text=True, check=False,
        )
    if out.returncode != 0:
        sys.exit(f"gcov_summary: gcov failed for {src}: {out.stderr.strip()}")
    lines, branches, current = "0.00", "n/a", None
    for line in out.stdout.splitlines():
        m = FILE_RE.match(line)
        if m:
            current = os.path.realpath(m.group(1))
            continue
        if current != os.path.realpath(src):
            continue
        m = LINES_RE.match(line)
        if m:
            lines = f"{float(m.group(1)):.2f}"
        m = TAKEN_RE.match(line)
        if m:
            branches = f"{float(m.group(1)):.2f}"
    return lines, branches


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=None, help="print paths relative to this folder")
    ap.add_argument("obj_dir")
    ap.add_argument("srcs", nargs="+")
    args = ap.parse_args()
    for src in args.srcs:
        src = os.path.abspath(src)
        lines, branches = summarise(args.obj_dir, src)
        shown = os.path.relpath(src, args.root) if args.root else src
        print(f"COVERAGE {shown} lines {lines} branches {branches}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
