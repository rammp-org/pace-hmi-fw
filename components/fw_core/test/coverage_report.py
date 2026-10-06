#!/usr/bin/env python3
"""Prints `COVERAGE <file> lines <pct> branches <pct>` for each file under test (TS-COV-02).

Runs gcov in JSON mode on every .gcda in the build folder and merges the results per source
file, because a header is compiled into several objects. A line counts as covered if any object
ran it; a branch counts as taken if any object took it. Branches on exception paths ("throw")
are left out: the firmware is built without exceptions (CS-LNG-03).

Usage: coverage_report.py <build_dir> <repo> <file>...
File names are printed relative to <repo>.
Exit code: 0 when the report was made (thresholds are judged by the runner, not here).
"""

import glob
import gzip
import json
import os
import subprocess
import sys
import tempfile


def gcov_json(build_dir):
    gcdas = sorted(glob.glob(os.path.join(build_dir, "*.gcda")))
    if not gcdas:
        sys.exit("coverage_report: no .gcda files in " + build_dir)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["gcov", "--json-format", "--branch-probabilities", "-o", build_dir]
                       + gcdas, cwd=tmp, check=True, stdout=subprocess.DEVNULL)
        for path in glob.glob(os.path.join(tmp, "*.gcov.json.gz")):
            with gzip.open(path, "rt") as f:
                yield json.load(f)


def main():
    build_dir, repo = sys.argv[1], os.path.realpath(sys.argv[2])
    files = [os.path.realpath(p) for p in sys.argv[3:]]
    lines = {}     # (file, line) -> covered
    branches = {}  # (file, line, index) -> taken
    for doc in gcov_json(build_dir):
        for entry in doc["files"]:
            path = os.path.realpath(os.path.join(doc.get("current_working_directory", ""),
                                                 entry["file"]))
            if path not in files:
                continue
            for line in entry["lines"]:
                key = (path, line["line_number"])
                lines[key] = lines.get(key, False) or line["count"] > 0
                index = 0
                for branch in line.get("branches", []):
                    if branch.get("throw", False):
                        continue
                    bkey = (path, line["line_number"], index)
                    branches[bkey] = branches.get(bkey, False) or branch["count"] > 0
                    index += 1

    def pct(table, path):
        hits = [v for k, v in table.items() if k[0] == path]
        return (100.0 * sum(hits) / len(hits)) if hits else 100.0, len(hits)

    total_l, total_b = [0, 0], [0, 0]
    for path in files:
        lp, ln = pct(lines, path)
        bp, bn = pct(branches, path)
        total_l[0] += sum(v for k, v in lines.items() if k[0] == path)
        total_l[1] += ln
        total_b[0] += sum(v for k, v in branches.items() if k[0] == path)
        total_b[1] += bn
        print(f"COVERAGE {os.path.relpath(path, repo)} lines {lp:.1f} branches {bp:.1f}"
              f"  ({ln} lines, {bn} branches)")
    tl = 100.0 * total_l[0] / total_l[1] if total_l[1] else 100.0
    tb = 100.0 * total_b[0] / total_b[1] if total_b[1] else 100.0
    print(f"COVERAGE TOTAL lines {tl:.1f} branches {tb:.1f}")


if __name__ == "__main__":
    main()
