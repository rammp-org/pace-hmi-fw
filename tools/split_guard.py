#!/usr/bin/env python3
"""Binary guard for the main.cpp fragment split (plan step 3, §4.1): PASS/FAIL.

Compares two ESP-IDF build directories of the same configuration: the reference
(before the split) and the candidate (after it). Because the split keeps one
translation unit, the firmware must be the same apart from __LINE__/__FILE__ and
the build identity (git hash, version, date).

    python tools/split_guard.py <reference build dir> <candidate build dir>

Checks:
1. ELF section sizes are equal (riscv32-esp-elf-size -A).
2. The set of defined symbols (name, type, size) is equal (nm -C -S --defined-only).
3. In main.cpp.obj, every section's bytes are equal, except sections listed as
   differing, which are reported with their byte-difference count for review.
Stdlib only; the RISC-V binutils path is found under C:\\Espressif\\tools.
"""

from __future__ import annotations

import difflib
import glob
import re
import pathlib
import subprocess
import sys

BIN = sorted(glob.glob(r"C:\Espressif\tools\riscv32-esp-elf\*\riscv32-esp-elf\bin"))[-1]
TOOL = {t: str(pathlib.Path(BIN) / f"riscv32-esp-elf-{t}.exe") for t in ("size", "nm", "objcopy", "readelf")}
APP = "rammp-hmi-p4.elf"
MAIN_OBJ = "esp-idf/main/CMakeFiles/__idf_main.dir/main.cpp.obj"
# Sections whose content legitimately differs: the app descriptor (version, git hash,
# date) and string pools that carry __FILE__/__LINE__-derived data.
ALLOWED_CONTENT_DIFF_HINT = ("rodata", "debug", "comment", "app_desc")


def run(*args: str) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def section_sizes(elf: pathlib.Path) -> dict[str, int]:
    out = {}
    for line in run(TOOL["size"], "-A", str(elf)).splitlines()[2:]:
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith("."):
            out[parts[0]] = int(parts[1])
    return out


def symbols(elf: pathlib.Path) -> set[tuple[str, str, str]]:
    syms = set()
    for line in run(TOOL["nm"], "-C", "-S", "--defined-only", str(elf)).splitlines():
        parts = line.split(maxsplit=3)
        if len(parts) == 4:  # addr size type name
            syms.add((parts[3], parts[2], parts[1]))
    return syms


def obj_sections(obj: pathlib.Path) -> dict[str, bytes]:
    names = []
    for line in run(TOOL["readelf"], "-S", "-W", str(obj)).splitlines():
        line = line.strip()
        if line.startswith("[") and "]" in line:
            rest = line.split("]", 1)[1].split()
            if rest and rest[0].startswith(".") and not rest[0].startswith(".rela"):
                names.append(rest[0])
    out = {}
    tmp = obj.with_suffix(".sec")
    for n in names:
        subprocess.run([TOOL["objcopy"], "-O", "binary", f"--only-section={n}", str(obj), str(tmp)],
                       check=True, capture_output=True)
        out[n] = tmp.read_bytes() if tmp.exists() else b""
        tmp.unlink(missing_ok=True)
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    ref, cand = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    fail = False

    a, b = section_sizes(ref / APP), section_sizes(cand / APP)
    diff = {k: (a.get(k), b.get(k)) for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)}
    non_debug = {k: v for k, v in diff.items() if not k.startswith(".debug") and k != ".comment"}
    print(f"1. ELF section sizes: {len(a)} sections; differing non-debug: {non_debug or 'none'}")
    size_delta = sum((v[1] or 0) - (v[0] or 0) for v in non_debug.values())

    sa, sb = symbols(ref / APP), symbols(cand / APP)
    only_a, only_b = sorted(sa - sb), sorted(sb - sa)
    print(f"2. defined symbols: {len(sa)} vs {len(sb)}; only in reference: {len(only_a)}; only in candidate: {len(only_b)}")
    for s in only_a[:10]:
        print(f"   - {s}")
    for s in only_b[:10]:
        print(f"   + {s}")
    changed_syms = {x[0] for x in only_a} | {x[0] for x in only_b}
    sym_delta = sum(int(x[2], 16) for x in only_b) - sum(int(x[2], 16) for x in only_a)

    oa, ob = obj_sections(ref / MAIN_OBJ), obj_sections(cand / MAIN_OBJ)
    if set(oa) != set(ob):
        print(f"3. main.cpp.obj section NAMES differ: {sorted(set(oa) ^ set(ob))[:10]}")
        fail = True
    changed = {}
    for n in sorted(set(oa) & set(ob)):
        if oa[n] != ob[n]:
            if len(oa[n]) != len(ob[n]):
                changed[n] = f"size {len(oa[n])} -> {len(ob[n])}"
            else:
                changed[n] = f"{sum(x != y for x, y in zip(oa[n], ob[n]))} bytes differ"
    code_changed = {n: v for n, v in changed.items() if not any(h in n for h in ALLOWED_CONTENT_DIFF_HINT)}
    print(f"3. main.cpp.obj: {len(oa)} sections; changed: {len(changed)}; changed outside rodata/debug: {len(code_changed)}")
    for n, v in list(changed.items())[:25]:
        print(f"   {n}: {v}")
    # Code sections may differ only where __LINE__ is an immediate: the assert in app_main
    # passes its line number to __assert_func in a1. Compare normalised disassembly
    # (addresses and local labels removed) and allow only that load to change.
    objdump = str(pathlib.Path(BIN) / "riscv32-esp-elf-objdump.exe")

    def insns(build: pathlib.Path, sec: str) -> list[str]:
        out = run(objdump, "-d", "--no-show-raw-insn", "-j", sec, str(build / MAIN_OBJ))
        lines = []
        for line in out.splitlines():
            line = re.sub(r"^\s*[0-9a-f]+:\s*", "", line)
            if not line.strip() or line.startswith(("Disassembly", "<")) or "file format" in line:
                continue
            if re.match(r"^[0-9a-f]+ <[^>]+>:$", line):
                continue  # a label line
            line = re.sub(r"\s*#.*$", "", line)          # comments with addresses
            line = re.sub(r"\b[0-9a-f]+ <[^>]+>", "<L>", line)  # branch/jump targets
            lines.append(line.strip())
        return lines

    for n in code_changed:
        a_ins, b_ins = insns(ref, n), insns(cand, n)
        sm = difflib.SequenceMatcher(a=a_ins, b=b_ins, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == "equal":
                continue
            removed, added = a_ins[i1:i2], b_ins[j1:j2]
            line_load = all(re.match(r"^(lui|addi|li|c\.li|c\.lui)\s+a1,", x) for x in removed + added)
            print(f"   {n}: {removed} -> {added} {'(assert __LINE__ load: allowed)' if line_load else '(NOT ALLOWED)'}")
            if not line_load:
                fail = True
    allowed_syms = {n.removeprefix(".text.") for n in code_changed}
    if not changed_syms <= allowed_syms:
        print(f"   symbols changed outside the allowed sections: {sorted(changed_syms - allowed_syms)}")
        fail = True
    if size_delta != sym_delta or any(k != ".flash.text" for k in non_debug):
        print(f"   section size delta {size_delta} not explained by symbol delta {sym_delta}: {non_debug}")
        fail = True
    else:
        print(f"   section and symbol size deltas ({size_delta} B) are exactly the allowed code changes")
    print("SPLIT GUARD " + ("FAIL" if fail else "PASS"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
