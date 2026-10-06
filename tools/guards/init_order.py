#!/usr/bin/env python3
"""G4 static-init guard: no new global constructor or lazy static outside the baseline.

    python tools/guards/init_order.py check --build C:\\b\\integ6b_default [--variant bench] [--json out]
    python tools/guards/init_order.py write --build C:\\b\\integ6b_default [--variant bench]
    python tools/guards/init_order.py list  --build C:\\b\\integ6b_default
    python tools/guards/init_order.py selftest

Spec: docs/plans/app-main-shrink.md §5 G4 and V6 (construction rule: an object whose
constructor touches hardware, LVGL, flash or calibration is built in app_main or an
island's start(), never at namespace scope or as a lazy static).

How (GCC builds; readelf and objdump of the build's own toolchain):
- per first-party object (compile_commands.json, not managed_components): the
  `.init_array*` / `.ctors*` / `.preinit_array` relocations name its init functions
  (`_GLOBAL__sub_I_*`). The relocations of those functions (and of the
  `__static_initialization_and_destruction_*` they call) name what they touch:
    objects         writable data defined in the object: what gets dynamically initialised
    reads_external  data of another TU (ELF type OBJECT): a cross-TU init-order dependency
    calls           functions called or address-taken (constructors, __cxa_atexit, ...)
- `_ZGV*` guard variables defined in the object are its lazy statics (function-local or
  template statics with a dynamic initialiser).
- a symbol in a COMDAT group or with weak/unique binding is `shared`: an inline or
  template entity from a header (e.g. fmt's locale facet id), initialised in every TU that
  uses it. Those are compared once, by name, not per object, so a new TU that merely
  includes fmt is not a "new constructor".

Verdict (exit 0 PASS, 1 FAIL, 2 bad input): FAIL on
  - an init function in an object the baseline has none for (unless it only touches
    baselined shared entities),
  - an object in `objects` / `lazy_statics` / `reads_external` that the baseline does not
    list for that object (a static moved to another TU fails too: say so in review),
  - a shared object or lazy static whose name the baseline does not know.
`allow` entries in the baseline ({object or "*", symbol, reason}) permit a planned
addition before it lands. Removals and changed `calls` are reported, never failures.
`write` regenerates the baseline from a build (keeping `allow`): only after review.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import guardlib as gl  # noqa: E402

HERE = Path(__file__).resolve().parent
BASELINES = HERE / "baselines"  # init_order.<variant>.json: default, bench (CONFIG_HMI_REMOTE_UI)
INIT_SECTION_RE = re.compile(r"^\.(init_array|ctors|preinit_array)(\.\d+)?$")
DATA_SECTION_RE = re.compile(r"^\.(s?bss|s?data|tbss|tdata)(?:\.rel\.ro)?(?:\.local)?\.(.+)$")
CALL_RELOCS = {"R_RISCV_CALL", "R_RISCV_CALL_PLT", "R_RISCV_JAL", "R_RISCV_RVC_JUMP"}
IGNORED_SYMBOLS = {"__dso_handle", "*ABS*"}
FOLLOW_RE = re.compile(r"__static_initialization_and_destruction|^_GLOBAL__sub_I_|^_GLOBAL__I_")
MAX_FOLLOW = 8  # init helper functions followed per object (bounded walk, CS-FLW-02)
FIELDS = ("objects", "reads_external", "lazy_statics")


@dataclass
class ObjInit:
    init_functions: list[str] = field(default_factory=list)
    objects: set[str] = field(default_factory=set)          # mangled
    reads_external: set[str] = field(default_factory=set)
    lazy_statics: set[str] = field(default_factory=set)
    calls: set[str] = field(default_factory=set)
    shared_objects: set[str] = field(default_factory=set)
    shared_lazy_statics: set[str] = field(default_factory=set)


def is_shared(sym: gl.Symbol, elf: gl.ElfObject) -> bool:
    sec = elf.section_of(sym)
    return sym.bind in ("WEAK", "UNIQUE") or (sec is not None and "G" in sec.flags)


def analyze(elf: gl.ElfObject, relocs_of, elf_types: dict[str, str]) -> ObjInit:
    """One object's init picture. `relocs_of(section names) -> {section: [(type, sym)]}`."""
    out = ObjInit()
    by_name = elf.by_name()
    sec_by_name = {s.name: s for s in elf.sections.values()}
    for sym in elf.symbols:
        if sym.name.startswith("_ZGV") and sym.ndx.isdigit():
            (out.shared_lazy_statics if is_shared(sym, elf) else out.lazy_statics).add(sym.name)
    init_secs = [s.name for s in elf.sections.values() if INIT_SECTION_RE.match(s.name)]
    if not init_secs:
        return out
    for rows in relocs_of(init_secs).values():
        out.init_functions.extend(sym for _, sym in rows if sym)
    todo = list(dict.fromkeys(out.init_functions))
    done: set[str] = set()
    while todo and len(done) < MAX_FOLLOW:
        fn = todo.pop(0)
        if fn in done:
            continue
        done.add(fn)
        sym = by_name.get(fn)
        sec = elf.section_of(sym) if sym else sec_by_name.get(fn)
        if sec is None:
            continue
        for rtype, ref in relocs_of([sec.name]).get(sec.name, []):
            follow = classify(out, elf, by_name, sec_by_name, elf_types, rtype, ref)
            if follow and follow not in done:
                todo.append(follow)
    return out


def classify(out: ObjInit, elf: gl.ElfObject, by_name, sec_by_name, elf_types, rtype: str,
             ref: str) -> str | None:
    """File one relocation of an init function; returns a local helper to follow, if any."""
    if not ref or ref.startswith(".L") or ref in IGNORED_SYMBOLS:
        return None
    sym = by_name.get(ref)
    if sym is None or sym.type == "SECTION":
        m = DATA_SECTION_RE.match(ref)
        if m:  # a section symbol: -fdata-sections names the section after its variable
            sec = sec_by_name.get(ref)
            target = (out.shared_objects if sec is not None and "G" in sec.flags else out.objects)
            target.add(m[2])
        elif ref.startswith(".text."):
            name = ref[len(".text."):].removeprefix("startup.")
            out.calls.add(name)
            return ref if FOLLOW_RE.search(name) else None
        return None
    if sym.ndx == "UND":
        if rtype in CALL_RELOCS or elf_types.get(ref) == "FUNC":
            out.calls.add(ref)
        else:
            out.reads_external.add(ref)
        return None
    sec = elf.section_of(sym)
    if sec is None:
        return None
    if "X" in sec.flags:
        out.calls.add(ref)
        return ref if FOLLOW_RE.search(ref) else None
    if "W" in sec.flags:
        (out.shared_objects if is_shared(sym, elf) else out.objects).add(ref)
    return None


def scan(build: gl.Build, readelf: str, objdump: str, elf_types: dict[str, str]) -> dict[str, ObjInit]:
    def one(o: gl.Obj) -> tuple[str, ObjInit]:
        elf = gl.parse_readelf(gl.run([readelf, "-W", "-S", "-s", o.rel], cwd=build.build_dir))

        def relocs_of(secs: list[str]) -> dict[str, list[tuple[str, str]]]:
            args = [a for s in secs for a in ("-j", s)]
            return gl.parse_objdump_relocs(gl.run([objdump, "-r", *args, o.rel], cwd=build.build_dir))
        return o.key, analyze(elf, relocs_of, elf_types)

    with ThreadPoolExecutor(max_workers=4) as pool:
        return dict(pool.map(one, build.objects))


def elf_symbol_types(readelf: str, elf: Path) -> dict[str, str]:
    types: dict[str, str] = {}
    if elf.is_file():
        # by name from its folder: a Windows readelf under WSL cannot open /mnt/c/... paths
        text = gl.run([readelf, "-W", "-s", elf.name], cwd=elf.parent)
        for s in gl.parse_readelf(text).symbols:
            if s.ndx != "UND" and s.name:
                types.setdefault(s.name, s.type)
    return types


def to_baseline(found: dict[str, ObjInit], dm: dict[str, str]) -> dict:
    """The JSON shape: demangled names, sorted; objects with nothing to say are left out."""
    objs: dict[str, dict] = {}
    shared_o: set[str] = set()
    shared_g: set[str] = set()
    for key, f in sorted(found.items()):
        shared_o |= {dm.get(s, s) for s in f.shared_objects}
        shared_g |= {dm.get(s, s) for s in f.shared_lazy_statics}
        entry = {
            "init_functions": len(f.init_functions),
            "objects": sorted({dm.get(s, s) for s in f.objects}),
            "reads_external": sorted({dm.get(s, s) for s in f.reads_external}),
            "lazy_statics": sorted({dm.get(s, s) for s in f.lazy_statics}),
            "calls": sorted({dm.get(s, s) for s in f.calls}),
            "shared": sorted({dm.get(s, s) for s in f.shared_objects | f.shared_lazy_statics}),
        }
        if entry["init_functions"] or any(entry[k] for k in FIELDS):
            objs[key] = entry
    return {"shared_objects": sorted(shared_o), "shared_lazy_statics": sorted(shared_g),
            "objects": objs}


def compare(cur: dict, base: dict) -> tuple[list[str], list[str]]:
    """(failures, notes) of the current picture against the baseline."""
    fails: list[str] = []
    notes: list[str] = []
    allow = base.get("allow", [])
    bobjs = base.get("objects", {})

    def allowed(key: str, sym: str) -> bool:
        return any(a.get("symbol") == sym and a.get("object") in (key, "*") for a in allow)

    def where_else(fld: str, sym: str, key: str) -> str:
        homes = [k for k, e in bobjs.items() if k != key and sym in e.get(fld, [])]
        return f" (the baseline has it in {', '.join(homes)}: moved?)" if homes else ""

    for kind in ("shared_objects", "shared_lazy_statics"):
        known = set(base.get(kind, []))
        for sym in sorted(set(cur.get(kind, [])) - known):
            if not allowed("*", sym):
                fails.append(f"new {kind.replace('_', ' ')[:-1]}: {sym}")
        for sym in sorted(known - set(cur.get(kind, []))):
            notes.append(f"gone from {kind}: {sym}")
    shared = set(base.get("shared_objects", [])) | set(cur.get("shared_objects", []))
    for key, e in sorted(cur.get("objects", {}).items()):
        b = bobjs.get(key, {})
        own = [s for s in e["objects"] if s not in shared]
        if e["init_functions"] and not b.get("init_functions"):
            unknown = [s for s in own if not allowed(key, s)] + list(e["reads_external"])
            if unknown or not (e["objects"] or e.get("shared")):
                fails.append(f"{key}: new global constructor (init function) touching "
                             f"{unknown or 'nothing identifiable'}")
        for fld in FIELDS:
            for sym in sorted(set(e[fld]) - set(b.get(fld, []))):
                if not allowed(key, sym):
                    fails.append(f"{key}: new {fld[:-1].replace('_', ' ')}: {sym}"
                                 + where_else(fld, sym, key))
            for sym in sorted(set(b.get(fld, [])) - set(e[fld])):
                notes.append(f"{key}: no longer in {fld}: {sym}")
        added = sorted(set(e.get("calls", [])) - set(b.get("calls", [])))
        if added:
            notes.append(f"{key}: init calls added: {added}")
    for key in sorted(set(bobjs) - set(cur.get("objects", {}))):
        notes.append(f"{key}: no init or lazy statics any more (or the object is gone)")
    return fails, notes


def current(args) -> dict:
    build = gl.Build.load(args.build)
    readelf = gl.find_tool("readelf", build.build_dir, args.toolchain_prefix)
    objdump = gl.find_tool("objdump", build.build_dir, args.toolchain_prefix)
    try:
        cxxfilt = gl.find_tool("c++filt", build.build_dir, args.toolchain_prefix)
    except gl.GuardError:
        cxxfilt = None
    found = scan(build, readelf, objdump, elf_symbol_types(readelf, build.elf))
    names = set()
    for f in found.values():
        for s in (f.objects, f.reads_external, f.lazy_statics, f.calls, f.shared_objects,
                  f.shared_lazy_statics):
            names |= s
    snap = to_baseline(found, gl.demangle(names, cxxfilt))
    snap["source"] = {"build": str(args.build), "project": build.project_path,
                      "objects_scanned": len(build.objects)}
    return snap


def cmd_check(args) -> int:
    base_file = Path(args.baseline)
    if not base_file.is_file():
        raise gl.GuardError(f"no baseline {base_file}: run `write` first (and review it)")
    base = json.loads(base_file.read_text(encoding="utf-8"))
    cur = current(args)
    fails, notes = compare(cur, base)
    for n in notes:
        print(f"note: {n}")
    for f in fails:
        print(f"FAIL: {f}")
    verdict = "FAIL" if fails else "PASS"
    print(f"init_order: {verdict} ({cur['source']['objects_scanned']} objects, "
          f"{len(cur['objects'])} with init or lazy statics, {len(fails)} failures, "
          f"{len(notes)} notes)")
    if args.json:
        gl.write_json(Path(args.json), {"verdict": verdict, "failures": fails, "notes": notes,
                                        "current": cur})
    return 0 if not fails else 1


def cmd_write(args) -> int:
    base_file = Path(args.baseline)
    old = json.loads(base_file.read_text(encoding="utf-8")) if base_file.is_file() else {}
    cur = current(args)
    out = {"schema": 1,
           "about": "G4 init-order baseline (tools/guards/init_order.py). Regenerate with "
                    "`write` only after reviewing the diff; `allow` is hand-kept.",
           "allow": old.get("allow", []), **cur}
    gl.write_json(base_file, out)
    print(f"wrote {base_file}: {len(cur['objects'])} objects with init or lazy statics, "
          f"{len(cur['shared_objects'])} shared objects")
    return 0


def cmd_list(args) -> int:
    cur = current(args)
    for key, e in cur["objects"].items():
        print(f"{key}: init={e['init_functions']}")
        for fld in (*FIELDS, "calls"):
            for s in e[fld]:
                print(f"    {fld:<15} {s}")
    for kind in ("shared_objects", "shared_lazy_statics"):
        for s in cur[kind]:
            print(f"{kind}: {s}")
    return 0


# ---------------------------------------------------------------- selftest

SAMPLE_READELF = """\
Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 4] .text.startup._GLOBAL__sub_I_app_main PROGBITS        00000000 03718a 0001e6 00  AX  0   0  2
  [ 5] .init_array       INIT_ARRAY      00000000 037370 000004 04  WA  0   0  4
  [ 6] .sbss._ZL10lvgl_mutex NOBITS          00000000 0379d4 000004 00  WA  0   0  4
  [ 7] .bss._ZL10logger_nav NOBITS          00000000 0379d4 000038 00  WA  0   0  4
  [ 8] .sbss._ZGVZ8app_mainE5stick NOBITS          00000000 0379d4 000008 00  WA  0   0  8
  [ 9] .sbss._ZGVN3fmt3v1212format_facetISt6localeE2idE NOBITS          00000000 037928 000008 00 WAG  0   0  8
  [10] .rodata           PROGBITS        00000000 03b720 00015c 00   A  0   0  4
  [11] .bss._ZL6anchor   NOBITS          00000000 0379d4 000004 00  WA  0   0  4
  [12] .text._Z41__static_initialization_and_destruction_0v PROGBITS 00000000 000000 000010 00 AX 0 0 2

Symbol table '.symtab' contains 12 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 00000000     4 OBJECT  LOCAL  DEFAULT    6 _ZL10lvgl_mutex
     2: 00000000    56 OBJECT  LOCAL  DEFAULT    7 _ZL10logger_nav
     3: 00000000     8 OBJECT  LOCAL  DEFAULT    8 _ZGVZ8app_mainE5stick
     4: 00000000     8 OBJECT  WEAK   DEFAULT    9 _ZGVN3fmt3v1212format_facetISt6localeE2idE
     5: 00000000   486 FUNC    LOCAL  DEFAULT    4 _GLOBAL__sub_I_app_main
     6: 00000000     0 NOTYPE  LOCAL  DEFAULT   10 .LANCHOR0
     7: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND __cxa_atexit
     8: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND _ZN4espp6LoggerC1ERKNS0_6ConfigE
     9: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND other_tu_object
    10: 00000000     0 SECTION LOCAL  DEFAULT   11 .bss._ZL6anchor
    11: 00000000    16 FUNC    LOCAL  DEFAULT   12 _Z41__static_initialization_and_destruction_0v
"""

SAMPLE_OBJDUMP = {
    ".init_array": "RELOCATION RECORDS FOR [.init_array]:\nOFFSET   TYPE              VALUE\n"
                   "00000000 R_RISCV_32        _GLOBAL__sub_I_app_main\n",
    ".text.startup._GLOBAL__sub_I_app_main": """\
RELOCATION RECORDS FOR [.text.startup._GLOBAL__sub_I_app_main]:
OFFSET   TYPE              VALUE
00000004 R_RISCV_HI20      _ZL10lvgl_mutex
00000004 R_RISCV_RELAX     *ABS*
00000008 R_RISCV_HI20      __dso_handle
00000010 R_RISCV_LO12_I    .LANCHOR0+0x00000004
00000024 R_RISCV_CALL_PLT  __cxa_atexit
00000036 R_RISCV_BRANCH    .L12060
0000003a R_RISCV_HI20      _ZL10logger_nav
0000003e R_RISCV_CALL_PLT  _ZN4espp6LoggerC1ERKNS0_6ConfigE
00000040 R_RISCV_LO12_S    _ZGVN3fmt3v1212format_facetISt6localeE2idE
00000044 R_RISCV_HI20      other_tu_object
00000048 R_RISCV_CALL_PLT  _Z41__static_initialization_and_destruction_0v
""",
    ".text._Z41__static_initialization_and_destruction_0v": """\
RELOCATION RECORDS FOR [.text._Z41__static_initialization_and_destruction_0v]:
OFFSET   TYPE              VALUE
00000002 R_RISCV_HI20      .bss._ZL6anchor
""",
}


def _sample_init() -> ObjInit:
    elf = gl.parse_readelf(SAMPLE_READELF)

    def relocs_of(secs):
        text = "".join(SAMPLE_OBJDUMP.get(s, "") for s in secs)
        return gl.parse_objdump_relocs(text)
    return analyze(elf, relocs_of, {"other_tu_object": "OBJECT"})


def _base_from_sample() -> dict:
    return to_baseline({"main/main.cpp.obj": _sample_init()}, {})


def t_parse() -> None:
    elf = gl.parse_readelf(SAMPLE_READELF)
    gl.expect("sections", len(elf.sections), 9)
    gl.expect("flags", elf.sections[9].flags, "WAG")
    gl.expect("symbols", len(elf.symbols), 12)
    r = gl.parse_objdump_relocs(SAMPLE_OBJDUMP[".text.startup._GLOBAL__sub_I_app_main"])
    gl.expect("addend dropped", r[".text.startup._GLOBAL__sub_I_app_main"][3],
              ("R_RISCV_LO12_I", ".LANCHOR0"))


def t_classify() -> None:
    f = _sample_init()
    gl.expect("init functions", f.init_functions, ["_GLOBAL__sub_I_app_main"])
    gl.expect("objects", sorted(f.objects), ["_ZL10logger_nav", "_ZL10lvgl_mutex", "_ZL6anchor"])
    gl.expect("reads external", f.reads_external, {"other_tu_object"})
    gl.expect("lazy statics", f.lazy_statics, {"_ZGVZ8app_mainE5stick"})
    gl.expect("shared guard", f.shared_lazy_statics,
              {"_ZGVN3fmt3v1212format_facetISt6localeE2idE"})
    gl.expect("shared object", f.shared_objects, {"_ZGVN3fmt3v1212format_facetISt6localeE2idE"})
    gl.expect("calls", "_ZN4espp6LoggerC1ERKNS0_6ConfigE" in f.calls and "__cxa_atexit" in f.calls,
              True)


def t_same_passes() -> None:
    base = _base_from_sample()
    fails, notes = compare(base, base)
    gl.expect("fails", fails, [])
    gl.expect("notes", notes, [])


def t_new_object_fails() -> None:
    base = _base_from_sample()
    cur = _base_from_sample()
    cur["objects"]["main/main.cpp.obj"]["objects"].append("_ZL6keypad")
    fails, _ = compare(cur, base)
    gl.expect("one failure", len(fails), 1)
    gl.expect("names it", "_ZL6keypad" in fails[0], True)


def t_new_tu_fails_and_says_moved() -> None:
    base = _base_from_sample()
    cur = _base_from_sample()
    moved = cur["objects"].pop("main/main.cpp.obj")
    cur["objects"]["main/drive_view.cpp.obj"] = moved
    fails, notes = compare(cur, base)
    gl.expect("constructor flagged", any("new global constructor" in f for f in fails), True)
    gl.expect("move hinted", any("moved?" in f for f in fails), True)
    gl.expect("old place noted", any("main/main.cpp.obj" in n for n in notes), True)


def t_shared_only_new_tu_passes() -> None:
    base = _base_from_sample()
    cur = _base_from_sample()
    cur["objects"]["main/fmt_user.cpp.obj"] = {
        "init_functions": 1, "objects": [], "reads_external": [], "lazy_statics": [],
        "calls": [], "shared": list(base["shared_objects"])}
    fails, _ = compare(cur, base)
    gl.expect("fmt-only init is not a new constructor", fails, [])


def t_new_lazy_and_external_fail() -> None:
    base = _base_from_sample()
    cur = _base_from_sample()
    cur["objects"]["main/main.cpp.obj"]["lazy_statics"].append("guard variable for keypad")
    cur["objects"]["main/main.cpp.obj"]["reads_external"].append("rd_pin")
    cur["shared_objects"].append("hmi::inline_thing")
    fails, _ = compare(cur, base)
    gl.expect("three failures", len(fails), 3)


def t_allow_permits() -> None:
    base = _base_from_sample()
    base["allow"] = [{"object": "main/main.cpp.obj", "symbol": "_ZL6keypad", "reason": "test"}]
    cur = _base_from_sample()
    cur["objects"]["main/main.cpp.obj"]["objects"].append("_ZL6keypad")
    fails, _ = compare(cur, base)
    gl.expect("allowed", fails, [])


def t_removal_is_note() -> None:
    base = _base_from_sample()
    cur = _base_from_sample()
    cur["objects"]["main/main.cpp.obj"]["objects"].remove("_ZL10logger_nav")
    fails, notes = compare(cur, base)
    gl.expect("no failure", fails, [])
    gl.expect("noted", len(notes), 1)


def cmd_selftest(_args) -> int:
    return gl.run_cases("tools/guards/init_order.py", [
        ("INI-001 readelf and objdump output parse into sections, symbols and relocations", t_parse),
        ("INI-002 init relocations split into objects, external reads, lazy statics and shared", t_classify),
        ("INI-003 a build equal to its baseline passes with no notes", t_same_passes),
        ("INI-004 a new dynamically initialised object fails and is named", t_new_object_fails),
        ("INI-005 a static moved to a new TU fails and the move is pointed out", t_new_tu_fails_and_says_moved),
        ("INI-006 a new TU whose init only touches baselined shared entities passes", t_shared_only_new_tu_passes),
        ("INI-007 a new lazy static, cross-TU read and shared object each fail", t_new_lazy_and_external_fail),
        ("INI-008 an allow entry permits a planned addition", t_allow_permits),
        ("INI-009 a removal is a note, never a failure", t_removal_is_note),
    ])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("check", "write", "list"):
        s = sub.add_parser(name)
        s.add_argument("--build", required=True, help="ESP-IDF build folder")
        s.add_argument("--variant", default="default",
                       help="picks baselines/init_order.<variant>.json (default, bench)")
        s.add_argument("--baseline", help="a baseline file instead of the variant's")
        s.add_argument("--toolchain-prefix", help="e.g. C:/.../bin/riscv32-esp-elf-")
        s.add_argument("--json", help="check: also write the result here")
    sub.add_parser("selftest")
    args = p.parse_args()
    if args.cmd != "selftest" and not args.baseline:
        args.baseline = str(BASELINES / f"init_order.{args.variant}.json")
    try:
        return {"check": cmd_check, "write": cmd_write, "list": cmd_list,
                "selftest": cmd_selftest}[args.cmd](args)
    except gl.GuardError as exc:
        print(f"init_order: INVALID input: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
