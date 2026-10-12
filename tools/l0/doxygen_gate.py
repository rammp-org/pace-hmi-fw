#!/usr/bin/env python3
"""L0 Doxygen ratchet (CS-DOC-01): documentation warnings may shrink, never grow.

    python tools/l0/doxygen_gate.py check  [--doxygen BIN] [--log FILE]   0 = PASS, 1 = FAIL
    python tools/l0/doxygen_gate.py update [--doxygen BIN] [--log FILE]   lower the baseline
    python tools/l0/doxygen_gate.py selftest

`check` runs `doxygen ci/Doxyfile` from the repo root (first-party headers; WARN_IF_UNDOCUMENTED,
WARN_NO_PARAMDOC, WARN_IF_DOC_ERROR; the vendored, generated and test code is excluded in the
Doxyfile), collects the warning log in results/doxygen/warnings.log, counts warnings per file
and compares with tools/l0/doxygen_baseline.json. It fails when the total or any file's count
rises above the baseline, or a file not in the baseline has any warning. With --log the run
is skipped and an existing log is judged (used by the selftest and for local replays).

The spec wants WARN_AS_ERROR (CS-DOC-01); the legacy headers cannot pass that yet, so the
Doxyfile keeps it NO and this ratchet stands in until the baseline reaches zero, when
WARN_AS_ERROR=YES replaces it. `update` only lowers counts (and drops files at zero).
Stdlib only; needs doxygen 1.9.x on PATH (apt-get install doxygen).
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).resolve().parent / "doxygen_baseline.json"
DOXYFILE = "ci/Doxyfile"
LOG = ROOT / "results" / "doxygen" / "warnings.log"
LOC_RE = re.compile(r"^(?P<file>.+?):\d+: ")
NONE = "(no file)"


def to_rel(path: str, root: pathlib.Path) -> str:
    """A warning path as repo-relative, forward slashes (doxygen prints the absolute path)."""
    p = path.replace("\\", "/")
    r = str(root).replace("\\", "/").rstrip("/") + "/"
    if p.startswith(r):
        return p[len(r):]
    m = re.match(r"^/mnt/[a-z]/", p)  # a log written under WSL, read from Windows
    if m:
        rest = p[len(m.group(0)):]
        tail = r.split(":/", 1)[-1] if ":" in r else r.lstrip("/")
        return rest[len(tail):] if rest.startswith(tail) else rest
    return p


def parse(text: str, root: pathlib.Path) -> dict[str, int]:
    """Warning count per repo-relative file. Continuation lines carry no 'warning:'."""
    counts: collections.Counter[str] = collections.Counter()
    for line in text.splitlines():
        if "warning:" not in line:
            continue
        m = LOC_RE.match(line)
        counts[to_rel(m.group("file"), root) if m else NONE] += 1
    return dict(counts)


def compare(cur: dict[str, int], base: dict[str, int]) -> list[str]:
    fails = []
    for f, n in sorted(cur.items()):
        b = base.get(f, 0)
        if n > b:
            fails.append(f"{f}: {n} warnings > baseline {b}")
    if sum(cur.values()) > sum(base.values()) and not fails:
        fails.append(f"total {sum(cur.values())} > baseline {sum(base.values())}")
    return fails


def lower(base: dict[str, int], cur: dict[str, int]) -> dict[str, int]:
    out = {}
    for f, b in base.items():
        n = min(b, cur.get(f, 0))
        if n > 0:
            out[f] = n
    return out


def load_baseline() -> dict[str, int]:
    if not BASELINE.exists():
        return {}
    return json.loads(BASELINE.read_text(encoding="utf-8")).get("files", {})


def write_baseline(files: dict[str, int]) -> None:
    doc = {"comment": "Doxygen warnings per file (tools/l0/doxygen_gate.py, ci/Doxyfile). "
                      "Lowered by `update`, never raised.",
           "total": sum(files.values()),
           "files": {k: files[k] for k in sorted(files)}}
    BASELINE.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")


def current(args: argparse.Namespace) -> dict[str, int]:
    if args.log:
        return parse(pathlib.Path(args.log).read_text(encoding="utf-8", errors="replace"), ROOT)
    exe = args.doxygen or shutil.which("doxygen")
    if not exe:
        sys.exit("doxygen_gate: doxygen not found (apt-get install doxygen, or --doxygen BIN)")
    LOG.parent.mkdir(parents=True, exist_ok=True)
    LOG.unlink(missing_ok=True)
    proc = subprocess.run([exe, DOXYFILE], cwd=ROOT, capture_output=True, text=True, check=False)
    if proc.returncode != 0 or not LOG.exists():
        sys.exit(f"doxygen_gate: doxygen failed (exit {proc.returncode}): {proc.stderr[-500:]}")
    return parse(LOG.read_text(encoding="utf-8", errors="replace"), ROOT)


def cmd_check(args: argparse.Namespace) -> int:
    cur, base = current(args), load_baseline()
    by_comp: collections.Counter[str] = collections.Counter()
    for f, n in cur.items():
        parts = f.split("/")
        by_comp[parts[1] if parts[0] == "components" and len(parts) > 2 else parts[0]] += n
    text = [f"Doxygen warnings (CS-DOC-01): {sum(cur.values())} now, baseline {sum(base.values())}",
            "", "| component | warnings |", "|---|---|"]
    text += [f"| {c} | {n} |" for c, n in sorted(by_comp.items())]
    print("\n".join(text))
    summ = os.environ.get("GITHUB_STEP_SUMMARY")
    if summ:
        with open(summ, "a", encoding="utf-8") as fh:
            fh.write("### " + "\n".join(text) + "\n")
    fails = compare(cur, base)
    for f in fails:
        print("FAIL " + f)
    shrunk = sum(base.values()) - sum(cur.values())
    if not fails and shrunk > 0:
        print(f"note: {shrunk} fewer than the baseline; run `doxygen_gate.py update`")
    print(f"doxygen_gate: {'FAIL' if fails else 'PASS'}")
    return 1 if fails else 0


def cmd_update(args: argparse.Namespace) -> int:
    cur = current(args)
    base = load_baseline()
    new = lower(base, cur) if base else {f: n for f, n in cur.items() if n > 0}
    write_baseline(new)
    print(f"doxygen_gate: baseline now {sum(new.values())} warnings in {len(new)} files")
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

    root = pathlib.Path(tempfile.gettempdir()) / "repo"
    r = str(root).replace("\\", "/")
    log = (f"{r}/components/a/include/a.hpp:12: warning: Member f() is not documented.\n"
           f"{r}/components/a/include/a.hpp:20: warning: warning: Compound A is not documented.\n"
           "  continuation line without the marker\n"
           f"{r}/main/m.hpp:3: warning: argument 'x' not found\n"
           "warning: ambiguous something\n")
    cur = parse(log, root)
    check("parse", cur, {"components/a/include/a.hpp": 2, "main/m.hpp": 1, NONE: 1})
    check("WSL path", parse("/mnt/c/w/repo/main/m.hpp:3: warning: x", pathlib.Path("C:/w/repo")),
          {"main/m.hpp": 1})
    check("same passes", compare(cur, cur), [])
    check("fewer passes", compare({"main/m.hpp": 1}, cur), [])
    check("file rises", len(compare({**cur, "main/m.hpp": 2}, cur)), 1)
    check("new file with warnings fails", len(compare({**cur, "x.hpp": 1}, cur)), 1)
    check("moved warning with equal total still fails per file",
          len(compare({"components/a/include/a.hpp": 3}, cur)), 1)
    check("lower only lowers", lower({"a": 3, "b": 1}, {"a": 5, "b": 0, "c": 2}), {"a": 3})
    check("lower keeps drops", lower({"a": 3}, {"a": 1}), {"a": 1})
    print(f"selftest: {'FAIL' if bad else 'PASS'}")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("check", "update"):
        p = sub.add_parser(name)
        p.add_argument("--doxygen", default=None, help="doxygen binary (default: PATH)")
        p.add_argument("--log", default=None, help="judge an existing warnings log; no run")
    sub.add_parser("selftest")
    args = ap.parse_args()
    return {"check": cmd_check, "update": cmd_update,
            "selftest": lambda _: selftest()}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
