#!/usr/bin/env python3
"""L0 coverage gate: L1 host coverage may rise, never fall; the gap to spec is reported.

    python tools/l0/coverage_gate.py check  [--run DIR]   exit 0 = PASS, 1 = FAIL
    python tools/l0/coverage_gate.py update [--run DIR] [--prune]   raise the baseline
    python tools/l0/coverage_gate.py selftest

Input: results/<run>/summary.json from `tests/run.py run --all --coverage` (default: the
newest run in results/). Each test reports `COVERAGE <file> lines <pct> branches <pct>` per
source under test (gcov, tests/host/gcov_summary.py). TS-COV-02 / CS-SAF-05 thresholds:
logic 80 % lines and 70 % branches; safety components 100 % branches. A component is a
safety component when a manifest entry with `safety: true` reports coverage for it.

Rules
- ratchet (the gate): every file in tools/l0/coverage_baseline.json must report lines and
  branches at least at its baseline (EPS slack for float noise). A file that is in the
  baseline but reports nothing fails: coverage cannot vanish silently. A file not in the
  baseline must meet its spec threshold (new code starts clean).
- gap to spec (reported, never fails): files and components under the threshold, as a table
  in the output and in $GITHUB_STEP_SUMMARY.
- the figures carry no line/branch counts, so a component is the plain mean of its files;
  the gate itself runs per file, which is stricter than per component.
- not measured: the test sources themselves (tests/**, */test/**). TOTAL and bare-name rows
  are attributed to the component of the manifest entry that reported them.

`update` only raises (new files are added); `--prune` also drops baseline files that report
nothing. Stdlib only, except that check/update read the manifest through tests/run.py
(PyYAML).
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).resolve().parent / "coverage_baseline.json"
EPS = 0.01  # percentage points
SPEC = {"logic": (80.0, 70.0), "safety": (80.0, 100.0)}  # (lines, branches)


class Row:
    def __init__(self, lines: float, branches: float | None, safety: bool) -> None:
        self.lines, self.branches, self.safety = lines, branches, safety


def pct(text: str) -> float | None:
    try:
        return float(text)
    except ValueError:  # gcov prints n/a when a file has no branches
        return None


def is_test_source(path: str) -> bool:
    return path.startswith("tests/") or "/test/" in path


def component_of(path: str) -> str:
    parts = path.split("/")
    if parts[0] == "components" and len(parts) > 2:
        return parts[1]
    return parts[0] if len(parts) > 1 else "(none)"


def collect(summary: dict, entries: dict[str, dict]) -> dict[str, Row]:
    """summary.json tests -> {repo path: Row}. `entries` maps manifest id -> its data."""
    rows: dict[str, Row] = {}
    for t in summary.get("tests", []):
        data = entries.get(t.get("entry_id", ""), {})
        safety = bool(data.get("safety"))
        owner = component_of(str(data.get("path", "")) + "/x")
        for c in t.get("coverage", []):
            f = c["file"]
            if "/" not in f or f == "TOTAL":  # bare name or the TOTAL row: the entry's component
                if not str(data.get("path", "")).startswith("components/"):
                    continue
                f = f"components/{owner}/{f}"
            if is_test_source(f):
                continue
            ln, br = pct(c["lines_pct"]), pct(c["branches_pct"])
            if ln is None:
                continue
            old = rows.get(f)
            if old is None:
                rows[f] = Row(ln, br, safety)
            else:  # the best of several tests that touch the same file
                old.lines = max(old.lines, ln)
                if br is not None and (old.branches is None or br > old.branches):
                    old.branches = br
                old.safety = old.safety or safety
    return rows


def spec_for(safety: bool) -> tuple[float, float]:
    return SPEC["safety" if safety else "logic"]


def below_spec(row: Row) -> list[str]:
    need_l, need_b = spec_for(row.safety)
    out = []
    if row.lines + EPS < need_l:
        out.append(f"lines {row.lines:.2f} < {need_l:g}")
    if row.branches is not None and row.branches + EPS < need_b:
        out.append(f"branches {row.branches:.2f} < {need_b:g}")
    return out


def compare(rows: dict[str, Row], base: dict[str, dict]) -> list[str]:
    """Ratchet failures: empty means PASS."""
    fails = []
    for f, b in sorted(base.items()):
        r = rows.get(f)
        if r is None:
            fails.append(f"{f}: in the baseline but reports no coverage "
                         f"(run `coverage_gate.py update --prune` if it was removed)")
            continue
        if r.lines + EPS < b["lines"]:
            fails.append(f"{f}: lines {r.lines:.2f} < baseline {b['lines']:.2f}")
        bb = b.get("branches")
        if bb is not None and (r.branches is None or r.branches + EPS < bb):
            got = "n/a" if r.branches is None else f"{r.branches:.2f}"
            fails.append(f"{f}: branches {got} < baseline {bb:.2f}")
    for f, r in sorted(rows.items()):
        if f not in base and below_spec(r):
            fails.append(f"{f}: new file under spec ({'; '.join(below_spec(r))}); "
                         f"new code starts at the threshold")
    return fails


def report(rows: dict[str, Row]) -> tuple[list[str], int, int]:
    """Per-component table (mean of its files) and the count of components under spec."""
    comps: dict[str, list[Row]] = {}
    for f, r in rows.items():
        comps.setdefault(component_of(f), []).append(r)
    lines = ["| component | kind | files | lines % | branches % | spec | files under spec |",
             "|---|---|---|---|---|---|---|"]
    under = 0
    for name in sorted(comps):
        rs = comps[name]
        safety = any(r.safety for r in rs)
        ml = sum(r.lines for r in rs) / len(rs)
        brs = [r.branches for r in rs if r.branches is not None]
        mb = sum(brs) / len(brs) if brs else None
        need_l, need_b = spec_for(safety)
        bad = sum(1 for r in rs if below_spec(r))
        comp_under = ml + EPS < need_l or (mb is not None and mb + EPS < need_b)
        under += comp_under
        mbs = "n/a" if mb is None else f"{mb:.1f}"
        lines.append(f"| {name} | {'safety' if safety else 'logic'} | {len(rs)} | {ml:.1f} | "
                     f"{mbs} | {need_l:g}/{need_b:g} | {'UNDER ' if comp_under else ''}{bad} |")
    return lines, under, len(comps)


def load_baseline() -> dict[str, dict]:
    if not BASELINE.exists():
        return {}
    return json.loads(BASELINE.read_text(encoding="utf-8")).get("files", {})


def write_baseline(files: dict[str, dict]) -> None:
    doc = {"comment": "L1 coverage floor per file, percent (tools/l0/coverage_gate.py). "
                      "Raised by `update`, never lowered; branches null = gcov n/a.",
           "files": {k: files[k] for k in sorted(files)}}
    BASELINE.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")


def newest_run(results: pathlib.Path) -> pathlib.Path:
    runs = sorted(p for p in results.glob("2*") if (p / "summary.json").is_file())
    if not runs:
        sys.exit(f"coverage_gate: no results/<run>/summary.json under {results}")
    return runs[-1]


def current(run: str | None) -> dict[str, Row]:
    sys.path.insert(0, str(ROOT / "tests"))
    import run as runner  # tests/run.py: the manifest loader (needs PyYAML)

    entries, errors = runner.load_manifest()
    if errors:
        sys.exit("coverage_gate: manifest errors: " + "; ".join(errors))
    run_dir = pathlib.Path(run) if run else newest_run(ROOT / "results")
    summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
    if not any(t.get("coverage") for t in summary.get("tests", [])):
        sys.exit(f"coverage_gate: {run_dir} has no coverage (run with --coverage)")
    print(f"coverage_gate: run {run_dir.name}")
    return collect(summary, {e.id: e.data for e in entries})


def dump(rows: dict[str, Row]) -> dict[str, dict]:
    return {f: {"lines": r.lines, "branches": r.branches} for f, r in sorted(rows.items())}


def cmd_check(args: argparse.Namespace) -> int:
    rows = current(args.run)
    table, under, ncomp = report(rows)
    text = (f"Coverage vs spec (TS-COV-02): {under} of {ncomp} components under threshold "
            f"(report only)\n\n" + "\n".join(table))
    print(text)
    summ = os.environ.get("GITHUB_STEP_SUMMARY")
    if summ:
        with open(summ, "a", encoding="utf-8") as fh:
            fh.write("### " + text + "\n")
    print("\ncurrent figures (for `update`):")
    print(json.dumps(dump(rows)))
    base = load_baseline()
    fails = compare(rows, base)
    for f in fails:
        print("FAIL " + f)
    print(f"coverage_gate: {'FAIL' if fails else 'PASS'} ({len(rows)} files, "
          f"{len(base)} in the baseline)")
    return 1 if fails else 0


def merge_raise(base: dict[str, dict], rows: dict[str, Row], prune: bool) -> dict[str, dict]:
    out = {} if prune else dict(base)
    for f, r in rows.items():
        b = base.get(f)
        if b is None:
            out[f] = {"lines": r.lines, "branches": r.branches}
            continue
        bb, nb = b.get("branches"), r.branches
        out[f] = {"lines": max(b["lines"], r.lines),
                  "branches": nb if bb is None else bb if nb is None else max(bb, nb)}
    return out


def cmd_update(args: argparse.Namespace) -> int:
    rows = current(args.run)
    new = merge_raise(load_baseline(), rows, args.prune)
    write_baseline(new)
    print(f"coverage_gate: baseline now {len(new)} files")
    return 0


def selftest() -> int:
    bad = 0

    def check(name: str, got: object, want: object) -> None:
        nonlocal bad
        if got != want:
            bad += 1
            print(f"  BAD  {name}: got {got!r}, want {want!r}")
        else:
            print(f"  ok   {name}")

    def cov(f: str, ln: str, br: str) -> dict:
        return {"file": f, "lines_pct": ln, "branches_pct": br}

    ents = {"A": {"path": "components/a/test", "safety": True},
            "B": {"path": "components/b/test", "safety": False},
            "H": {"path": "tests/host/h", "safety": False}}
    summ = {"tests": [
        {"entry_id": "A", "coverage": [cov("components/a/src/x.cpp", "90.00", "100.00"),
                                       cov("components/a/test/t.cpp", "50", "50"),
                                       cov("TOTAL", "99.5", "n/a")]},
        {"entry_id": "B", "coverage": [cov("components/b/include/y.hpp", "85.00", "n/a"),
                                       cov("y2.hpp", "70", "60")]},
        {"entry_id": "B", "coverage": [cov("components/b/include/y.hpp", "80.00", "75.00")]},
        {"entry_id": "H", "coverage": [cov("bare.cpp", "1", "1"),
                                       cov("tests/host/h/m.cpp", "1", "1")]}]}
    rows = collect(summ, ents)
    check("files", sorted(rows), ["components/a/TOTAL", "components/a/src/x.cpp",
                                  "components/b/include/y.hpp", "components/b/y2.hpp"])
    y = rows["components/b/include/y.hpp"]
    check("best of two tests", (y.lines, y.branches), (85.0, 75.0))
    check("n/a branches kept", rows["components/a/TOTAL"].branches, None)
    check("safety from manifest", rows["components/a/src/x.cpp"].safety, True)
    base = dump(rows)
    check("same figures pass", compare(rows, base), [])
    rows["components/a/src/x.cpp"].lines = 89.0
    check("lines drop fails", len(compare(rows, base)), 1)
    rows["components/a/src/x.cpp"].lines = 90.0
    y.branches = 74.995
    check("float noise within EPS passes", compare(rows, base), [])
    y.branches = 74.0
    check("branches drop fails", len(compare(rows, base)), 1)
    y.branches = 80.0
    check("rise passes", compare(rows, base), [])
    check("branches lost (n/a) fails", len(compare(
        {"f": Row(90.0, None, False)}, {"f": {"lines": 90.0, "branches": 50.0}})), 1)
    check("missing file fails", len(compare({}, {"f": {"lines": 1.0, "branches": None}})), 1)
    check("new file under spec fails", len(compare({"n": Row(79.0, None, False)}, {})), 1)
    check("new file at spec passes", compare({"n": Row(80.0, 70.0, False)}, {}), [])
    check("new safety file needs 100 branches", len(compare({"n": Row(99.0, 99.0, True)}, {})), 1)
    check("below_spec safety", below_spec(Row(100.0, 99.0, True)), ["branches 99.00 < 100"])
    check("below_spec logic ok", below_spec(Row(80.0, 70.0, False)), [])
    _, under, n = report({"components/a/s.cpp": Row(90.0, 90.0, True),
                          "components/b/s.cpp": Row(90.0, 71.0, False)})
    check("report counts", (under, n), (1, 2))
    old = {"f": {"lines": 90.0, "branches": 50.0}, "g": {"lines": 1.0, "branches": None}}
    up = merge_raise(old, {"f": Row(85.0, 60.0, False), "h": Row(5.0, None, False)}, False)
    check("update only raises", (up["f"], sorted(up)), ({"lines": 90.0, "branches": 60.0},
                                                         ["f", "g", "h"]))
    pr = merge_raise(old, {"h": Row(5.0, None, False), "f": Row(1.0, 1.0, False)}, True)
    check("prune drops stale", sorted(pr), ["f", "h"])
    print(f"selftest: {'FAIL' if bad else 'PASS'}")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("check", "update"):
        p = sub.add_parser(name)
        p.add_argument("--run", default=None, help="a results/<run> folder (default: newest)")
        if name == "update":
            p.add_argument("--prune", action="store_true")
    sub.add_parser("selftest")
    args = ap.parse_args()
    return {"check": cmd_check, "update": cmd_update,
            "selftest": lambda _: selftest()}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
