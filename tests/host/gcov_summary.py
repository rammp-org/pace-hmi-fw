#!/usr/bin/env python3
"""Print `COVERAGE <file> lines <pct> branches <pct>` for each source under test.

Used by tests/host/common.mk (`make coverage`). Parses the summary of `gcov -b -c` (no gcovr
in WSL). The branch figure is gcov's "Taken at least once" (TS-COV-02).

usage: gcov_summary.py [--root DIR] OBJ_DIR SRC...
OBJ_DIR mirrors absolute source paths: SRC=/a/b.cpp has its object at OBJ_DIR/a/b.cpp.o.

A header's inline functions are compiled into every test object that uses them, and the linker
keeps one copy of each: its calls from all the objects count in that copy only. When more than
one object holds a header's counters, the app links them as OBJ_DIR/a/b.hpp.o,
OBJ_DIR/a/b.hpp.alt1.o, ... (with their .gcno/.gcda), and the figures are the union: each line's
and each branch's counts are summed over the objects (gcov's JSON), so a line or branch counts
once, as executed or taken if any object's copy ran it.
"""

import argparse
import glob
import gzip
import json
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


def alternates(obj_dir: str, src: str) -> list[str]:
    """The extra objects holding `src`'s counters (OBJ_DIR/<src>.altN.o), in order."""
    base = os.path.join(obj_dir, src.lstrip("/"))
    return sorted(glob.glob(glob.escape(base) + ".alt*.o"))


Line = tuple[str, int]  # (function, line): gcov lists a line once per function emitted there


def counts(obj: str, src: str) -> tuple[dict[Line, int], dict[tuple[str, int, int], int]]:
    """{(function, line): count} and {(function, line, branch index): count} for `src` from one
    object (gcov JSON), the units gcov's own summary counts."""
    with tempfile.TemporaryDirectory() as tmp:
        out = subprocess.run(
            ["gcov", "-b", "-c", "-j", "-o", obj, src],
            cwd=tmp, capture_output=True, text=True, check=False,
        )
        if out.returncode != 0:
            sys.exit(f"gcov_summary: gcov failed for {src}: {out.stderr.strip()}")
        lines: dict[Line, int] = {}
        branches: dict[tuple[str, int, int], int] = {}
        for path in glob.glob(os.path.join(tmp, "*.gcov.json.gz")):
            with gzip.open(path, "rt", encoding="utf-8") as f:
                data = json.load(f)
            for entry in data.get("files", []):
                if os.path.realpath(os.path.join(data.get("current_working_directory", ""),
                                                 entry["file"])) != os.path.realpath(src):
                    continue
                for ln in entry.get("lines", []):
                    key = (ln.get("function_name", ""), ln["line_number"])
                    lines[key] = lines.get(key, 0) + ln["count"]
                    for i, br in enumerate(ln.get("branches", [])):
                        bkey = (*key, i)
                        branches[bkey] = branches.get(bkey, 0) + br["count"]
    return lines, branches


def summarise_union(objs: list[str], src: str) -> tuple[str, str]:
    lines: dict[Line, int] = {}
    branches: dict[tuple[str, int, int], int] = {}
    for obj in objs:
        ln, br = counts(obj, src)
        for k, v in ln.items():
            lines[k] = lines.get(k, 0) + v
        for k, v in br.items():
            branches[k] = branches.get(k, 0) + v
    lpct = f"{100.0 * sum(1 for v in lines.values() if v > 0) / len(lines):.2f}" if lines else "0.00"
    bpct = (f"{100.0 * sum(1 for v in branches.values() if v > 0) / len(branches):.2f}"
            if branches else "n/a")
    return lpct, bpct


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=None, help="print paths relative to this folder")
    ap.add_argument("obj_dir")
    ap.add_argument("srcs", nargs="+")
    args = ap.parse_args()
    for src in args.srcs:
        src = os.path.abspath(src)
        alts = alternates(args.obj_dir, src)
        if alts:
            primary = os.path.join(args.obj_dir, src.lstrip("/") + ".o")
            lines, branches = summarise_union([primary, *alts], src)
        else:
            lines, branches = summarise(args.obj_dir, src)
        shown = os.path.relpath(src, args.root) if args.root else src
        print(f"COVERAGE {shown} lines {lines} branches {branches}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
