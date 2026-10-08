#!/usr/bin/env python3
"""L0 include boundary: no file includes another component's private `src/` files.

    python tools/l0/include_boundary.py check [--root DIR]
    python tools/l0/include_boundary.py selftest

A component's public API is its `include/` directory; `src/` is private (CS-LAY-03, gap 8 of
docs/plans/compliance-gaps.md). A violation is an `#include` whose path, taken as written or
resolved against the including file's folder, lands in `components/<other>/src/`. Files in
`main/`, `tests/`, `sim/` and `scripts/` belong to no component, so for them every
component's `src/` is another's. Includes of a component's own `src/` are fine.

Not scanned: generated code (components/ui), the vendored BSP (components/m5stack-tab5),
and external/. Exit 0 PASS, 1 violations, 2 bad usage. Stdlib only.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

SKIP_COMPONENTS = {"ui", "m5stack-tab5"}
SCAN_TOP = ["components", "main", "tests", "sim", "scripts"]
SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".ipp"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
COMPONENT_SRC_RE = re.compile(r"(?:^|/)(?:components/)?([A-Za-z0-9_.-]+)/src/")


def owner(rel: Path) -> str | None:
    """The component a repo-relative file belongs to, or None (main/, tests/, ...)."""
    parts = rel.parts
    return parts[1] if len(parts) > 2 and parts[0] == "components" else None


def target_component(root: Path, rel: Path, inc: str) -> str | None:
    """The component whose src/ the include names, or None if it names no component's src/."""
    # Resolved against the including file's folder (and the root): "../../other/src/x.hpp".
    for base in ((root / rel).parent, root):
        try:
            cand = (base / inc).resolve()
            hit = cand.relative_to(root.resolve())
        except ValueError:
            continue
        if not cand.exists():
            continue
        p = hit.parts
        if len(p) > 3 and p[0] == "components" and p[2] == "src":
            return p[1]
    # As written: "other/src/x.hpp" or "components/other/src/x.hpp" (an include dir to src).
    m = COMPONENT_SRC_RE.search(inc)
    if m and (root / "components" / m.group(1)).is_dir():
        return m.group(1)
    return None


def scan(root: Path) -> list[str]:
    out: list[str] = []
    for top in SCAN_TOP:
        for f in sorted((root / top).rglob("*")):
            if f.suffix not in SUFFIXES or not f.is_file():
                continue
            rel = f.relative_to(root)
            mine = owner(rel)
            if top == "components" and (len(rel.parts) < 3 or rel.parts[1] in SKIP_COMPONENTS):
                continue
            for n, line in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                m = INCLUDE_RE.match(line)
                if not m:
                    continue
                other = target_component(root, rel, m.group(1))
                if other is not None and other != mine:
                    out.append(f"{rel.as_posix()}:{n}: includes {m.group(1)!r}, private to component {other!r}")
    return out


def cmd_check(args) -> int:
    root = Path(args.root).resolve()
    bad = scan(root)
    for line in bad:
        print(line)
    print(f"include_boundary: {'FAIL' if bad else 'PASS'} ({len(bad)} violations)")
    return 1 if bad else 0


def _tree(files: dict[str, str]) -> Path:
    root = Path(tempfile.mkdtemp())
    for name, text in files.items():
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8", newline="\n")
    return root


def cmd_selftest(_args) -> int:
    base = {"components/a/src/a.cpp": "", "components/a/include/a.hpp": "",
            "components/b/src/b.hpp": "", "components/b/include/b.hpp": ""}

    def run(extra: dict[str, str]) -> list[str]:
        return scan(_tree({**base, **extra}))

    cases = []

    def case(name):
        def deco(fn):
            cases.append((name, fn))
            return fn
        return deco

    @case("IB-001 a public include and an own src include pass")
    def _():
        assert run({"components/a/src/x.cpp": '#include "a.hpp"\n#include "a.hpp"\n#include "../src/a.cpp"\n'
                    '#include "src/a.cpp"\n#include <b.hpp>\n'}) == []

    @case("IB-002 a relative include into another component's src fails")
    def _():
        r = run({"components/a/src/x.cpp": '#include "../../b/src/b.hpp"\n'})
        assert len(r) == 1 and "private to component 'b'" in r[0], r

    @case("IB-003 a path naming another component's src fails, quoted or angled")
    def _():
        r = run({"components/a/src/x.cpp": '#include <b/src/b.hpp>\n',
                 "components/a/src/y.cpp": '  #  include "components/b/src/b.hpp"\n'})
        assert len(r) == 2, r

    @case("IB-004 main, tests and sim count as no component")
    def _():
        r = run({"main/m.cpp": '#include "../components/a/src/a.cpp"\n',
                 "tests/host/t.cpp": '#include "b/src/b.hpp"\n', "sim/s.cpp": '#include "a/src/a.cpp"\n'})
        assert len(r) == 3, r

    @case("IB-005 generated and vendored components are not scanned")
    def _():
        assert run({"components/ui/g.c": '#include "../../a/src/a.cpp"\n',
                    "components/m5stack-tab5/v.c": '#include "../../a/src/a.cpp"\n'}) == []

    @case("IB-006 an unrelated src/ path (a third-party library) passes")
    def _():
        assert run({"components/a/src/x.cpp": '#include "lvgl/src/lv.h"\n'}) == []

    fails = 0
    for name, fn in cases:
        try:
            fn()
            print(f"include_boundary.py:{name}:PASS")
        except Exception as exc:  # noqa: BLE001
            fails += 1
            print(f"include_boundary.py:{name}:FAIL: {exc}")
    print(f"\n{len(cases)} Tests {fails} Failures 0 Ignored\n{'OK' if not fails else 'FAIL'}")
    return 1 if fails else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    sub.add_parser("selftest")
    args = ap.parse_args()
    return {"check": cmd_check, "selftest": cmd_selftest}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
