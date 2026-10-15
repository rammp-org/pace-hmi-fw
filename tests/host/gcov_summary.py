#!/usr/bin/env python3
"""Print `COVERAGE <file> lines <pct> branches <pct>` for each source under test.

Used by tests/host/common.mk (`make coverage`). Lines: the summary of `gcov -b -c` (no gcovr
in WSL). Branches: taken at least once, of gcov's branches less its exception edges (TS-COV-02).

Exception edges are excluded (owner decision 2026-10-15), the rule of gcovr's
`--exclude-throw-branches`: a branch gcov's JSON marks `"throw": true` (the edge from a call
into its exception landing pad) is not counted, taken or not. The firmware is built without
C++ exceptions (CONFIG_COMPILER_CXX_EXCEPTIONS is off, CS-LNG-03), so these edges exist only
in the host build and no test can reach them: e.g. components/stick/src/mapping_selftest.cpp
had 14 of its 30 branches (gcc 13) as such edges. `--include-throw-branches` gives the old
figure, for comparison. Branches are counted from gcov's JSON, one per (function, line,
index), the units of gcov's own summary.

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


def summarise_lines(obj_dir: str, src: str) -> str:
    """gcov's own "Lines executed" figure for `src` from its one object."""
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
    lines, current = "0.00", None
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
    return lines


def alternates(obj_dir: str, src: str) -> list[str]:
    """The extra objects holding `src`'s counters (OBJ_DIR/<src>.altN.o), in order."""
    base = os.path.join(obj_dir, src.lstrip("/"))
    return sorted(glob.glob(glob.escape(base) + ".alt*.o"))


Line = tuple[str, int]  # (function, line): gcov lists a line once per function emitted there


def counts(obj: str, src: str, exclude_throw: bool = True
           ) -> tuple[dict[Line, int], dict[tuple[str, int, int], int]]:
    """{(function, line): count} and {(function, line, branch index): count} for `src` from one
    object (gcov JSON), the units gcov's own summary counts; without the exception edges
    (`"throw": true`) when `exclude_throw`."""
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
                        if exclude_throw and br.get("throw"):
                            continue
                        bkey = (*key, i)
                        branches[bkey] = branches.get(bkey, 0) + br["count"]
    return lines, branches


def summarise_union(objs: list[str], src: str, exclude_throw: bool = True) -> tuple[str, str]:
    lines: dict[Line, int] = {}
    branches: dict[tuple[str, int, int], int] = {}
    for obj in objs:
        ln, br = counts(obj, src, exclude_throw)
        for k, v in ln.items():
            lines[k] = lines.get(k, 0) + v
        for k, v in br.items():
            branches[k] = branches.get(k, 0) + v
    lpct = f"{100.0 * sum(1 for v in lines.values() if v > 0) / len(lines):.2f}" if lines else "0.00"
    # taken = a non-zero count, gcov's own rule: a test with threads can leave a counter
    # negative (non-atomic updates), and gcov's summary counts that as taken
    bpct = (f"{100.0 * sum(1 for v in branches.values() if v != 0) / len(branches):.2f}"
            if branches else "n/a")
    return lpct, bpct


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=None, help="print paths relative to this folder")
    ap.add_argument("obj_dir")
    ap.add_argument("--include-throw-branches", action="store_true",
                    help="count the exception edges too (the figure before 2026-10-15)")
    ap.add_argument("srcs", nargs="+")
    args = ap.parse_args()
    exclude_throw = not args.include_throw_branches
    for src in args.srcs:
        src = os.path.abspath(src)
        primary = os.path.join(args.obj_dir, src.lstrip("/") + ".o")
        alts = alternates(args.obj_dir, src)
        if alts:
            lines, branches = summarise_union([primary, *alts], src, exclude_throw)
        else:
            lines = summarise_lines(args.obj_dir, src)
            _, branches = summarise_union([primary], src, exclude_throw)
        shown = os.path.relpath(src, args.root) if args.root else src
        print(f"COVERAGE {shown} lines {lines} branches {branches}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
