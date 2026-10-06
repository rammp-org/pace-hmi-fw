#!/usr/bin/env python3
"""Split main/main.cpp into fragments that main.cpp #includes, with no behaviour change.

Step 3 of docs/plans/refactor.md. The fragments stay in ONE translation unit:
main.cpp #includes them in their original order, so static-initialisation order,
linkage, ODR and inlining cannot change. Every original line lands exactly once.
The later steps then move one fragment at a time into a real component.

    python tools/split_main.py split  <original main.cpp> <main dir>
    python tools/split_main.py verify <original main.cpp> <main dir>

`split` always starts from the original file (re-run it; never hand-patch its
output). `verify` rebuilds the original from main.cpp and the fragments and fails
unless it is byte-identical. Stdlib only.
"""

from __future__ import annotations

import pathlib
import re
import sys

# Each entry: (fragment name, anchor line in the ORIGINAL main.cpp). The anchor is a
# line of the first construct of the fragment; the cut moves up over the comment
# block directly above it. A fragment runs to the next cut. APP_MAIN marks the
# region that stays in main.cpp (app_main and the lambdas that capture its locals).
FRAGMENTS: list[tuple[str, int]] = [
    ("frag_fps", 90),
    ("frag_state", 110),
    ("frag_haptics", 272),
    ("frag_status_band", 327),
    ("frag_stick_config", 488),
    ("frag_rtps_label", 521),
    ("frag_drive_band", 578),
    ("frag_rtps_poll", 647),
    ("frag_brightness", 718),
    ("frag_clock", 767),
    ("frag_stick_button", 853),
    ("frag_hold", 961),
    ("frag_lock", 1070),
    ("frag_refusal", 1197),
    ("frag_drive", 1525),
    ("frag_hold_poll", 1729),
    ("frag_seat", 1837),
    ("frag_bench_pin", 2066),
    ("frag_settings_ui", 2176),
    ("frag_actions", 2799),
    ("frag_diag", 3003),
    ("frag_nav", 3231),
    ("frag_overdraw", 4027),
    ("frag_screens_on_demand", 4126),
    ("frag_display_flip", 4237),
    ("frag_da7280", 4435),
    ("APP_MAIN", 4752),
    ("frag_audio", 6328),
]

HEADER = "// split_main.py fragment of main.cpp ({name}: original lines {a}-{b}); one TU, see main.cpp.\n"
HEADER_RE = re.compile(r"^// split_main\.py fragment of main\.cpp \((\w+): original lines (\d+)-(\d+)\); one TU, see main\.cpp\.\n$")
INCLUDE_LINE = '#include "{name}.inc" // split_main.py\n'
SEPARATOR = "// --\n"  # keeps neighbouring include edits from conflicting in git


def depth_profile(lines: list[str]) -> list[tuple[int, int, int, bool]]:
    """(brace depth, paren depth, #if depth, inside block comment) at the START of each line."""
    out = []
    brace = paren = pp = 0
    in_block = False
    for line in lines:
        out.append((brace, paren, pp, in_block))
        s = line
        stripped = s.lstrip()
        if not in_block and stripped.startswith("#"):
            d = stripped[1:].lstrip()
            if re.match(r"if(n?def)?\b", d):
                pp += 1
            elif d.startswith("endif"):
                pp -= 1
            continue
        i = 0
        in_str: str | None = None
        while i < len(s):
            c = s[i]
            nxt = s[i + 1] if i + 1 < len(s) else ""
            if in_block:
                if c == "*" and nxt == "/":
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if in_str:
                if c == "\\":
                    i += 2
                    continue
                if c == in_str:
                    in_str = None
                i += 1
                continue
            if c == "/" and nxt == "/":
                break
            if c == "/" and nxt == "*":
                in_block = True
                i += 2
                continue
            if c in "\"'":
                # R"(...)" raw strings are not used at top level in this file; a raw
                # string containing braces would only shift depth inside a function.
                in_str = c
            elif c == "{":
                brace += 1
            elif c == "}":
                brace -= 1
            elif c == "(":
                paren += 1
            elif c == ")":
                paren -= 1
            i += 1
    return out


def cut_lines(lines: list[str]) -> list[tuple[str, int]]:
    """Resolve each anchor to a 0-based cut index and check it is safe."""
    prof = depth_profile(lines)
    cuts = []
    for name, anchor in FRAGMENTS:
        i = anchor - 1
        while i > 0 and lines[i - 1].startswith("//"):
            i -= 1
        # A section banner (a //// line ending a comment block) one blank line above
        # belongs to this fragment too.
        if i > 1 and lines[i - 1].strip() == "" and lines[i - 2].startswith("////"):
            i -= 1
            while i > 0 and lines[i - 1].startswith("//"):
                i -= 1
        brace, paren, pp, in_block = prof[i]
        if (brace, paren, pp, in_block) != (0, 0, 0, False):
            sys.exit(f"unsafe cut for {name} at line {i + 1}: depth {brace}/{paren}/#if {pp}/block {in_block}")
        cuts.append((name, i))
    idx = [c for _, c in cuts]
    if idx != sorted(idx) or len(set(idx)) != len(idx):
        sys.exit("cuts are not strictly increasing")
    return cuts


def split(original: pathlib.Path, main_dir: pathlib.Path) -> None:
    text = open(original, encoding="utf-8", newline="").read()
    lines = text.splitlines(keepends=True)
    cuts = cut_lines(lines)
    out_main: list[str] = lines[: cuts[0][1]]
    for k, (name, start) in enumerate(cuts):
        end = cuts[k + 1][1] if k + 1 < len(cuts) else len(lines)
        body = lines[start:end]
        if name == "APP_MAIN":
            out_main.extend(body)
            continue
        hdr = HEADER.format(name=name, a=start + 1, b=end)
        (main_dir / f"{name}.inc").write_text(hdr + "".join(body), encoding="utf-8", newline="")
        out_main.append(INCLUDE_LINE.format(name=name))
        out_main.append(SEPARATOR)
    (main_dir / "main.cpp").write_text("".join(out_main), encoding="utf-8", newline="")
    for name, start in cuts:
        print(f"{name:24s} from line {start + 1}")


def rebuild(main_dir: pathlib.Path) -> str:
    out = []
    lines = open(main_dir / "main.cpp", encoding="utf-8", newline="").read().splitlines(keepends=True)
    skip_sep = False
    for line in lines:
        if skip_sep and line == SEPARATOR:
            skip_sep = False
            continue
        skip_sep = False
        m = re.match(r'^#include "(frag_\w+)\.inc" // split_main\.py\n$', line)
        if m:
            frag = open(main_dir / f"{m.group(1)}.inc", encoding="utf-8", newline="").read().splitlines(keepends=True)
            if not frag or not HEADER_RE.match(frag[0]):
                sys.exit(f"{m.group(1)}.inc: missing or changed split_main.py header")
            out.extend(frag[1:])
            skip_sep = True
            continue
        out.append(line)
    return "".join(out)


def verify(original: pathlib.Path, main_dir: pathlib.Path) -> int:
    want = original.read_bytes().decode("utf-8")
    got = rebuild(main_dir)
    if got != want:
        a, b = want.splitlines(), got.splitlines()
        for n, (x, y) in enumerate(zip(a, b), 1):
            if x != y:
                print(f"first difference at line {n}:\n  want: {x!r}\n  got:  {y!r}")
                break
        else:
            print(f"length differs: want {len(a)} lines, got {len(b)}")
        print("VERIFY FAIL")
        return 1
    print(f"VERIFY PASS: main.cpp + fragments == original ({len(want.splitlines())} lines)")
    return 0


def main() -> int:
    if len(sys.argv) != 4 or sys.argv[1] not in ("split", "verify"):
        print(__doc__)
        return 2
    original, main_dir = pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    if sys.argv[1] == "split":
        split(original, main_dir)
        return verify(original, main_dir)
    return verify(original, main_dir)


if __name__ == "__main__":
    sys.exit(main())
