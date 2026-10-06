#!/usr/bin/env python3
"""G2 exported-symbols guard: each first-party object exports only its header API, once.

    python tools/guards/exports.py check --build C:\\b\\integ6b_default [--src C:\\w\\guards] [--json out]
    python tools/guards/exports.py list  --build C:\\b\\integ6b_default [--src ...]
    python tools/guards/exports.py selftest

Spec: docs/plans/app-main-shrink.md §5 G2, §6 ("static in a header"), V11.

Per first-party object (compile_commands.json, not managed_components), from `nm -A`:
  E1 duplicate definition: a strong global symbol (T D B R G S C) defined in two or more
     first-party objects. In an archive the linker may take the first member and never
     report the second, so this is not always a link error.
  E2 duplicate local static: a function-local static (`_ZZ...`) or its guard (`_ZGV...`)
     with internal linkage defined in two or more objects - each TU got its own copy (a
     `static` or anonymous-namespace function with a local static, in a header).
     Weak/unique (`V`, `u`, `W`) ones are inline-function statics the linker merges: fine.
  E3 undeclared export: a strong global symbol whose identifier (api_leaf: `ns::Cls::fn(..)`
     -> `fn`, a constructor -> its class, `vtable for X` -> `X`) appears in no first-party
     header under --src (main/, components/, external/; test/ and tests/ folders skipped):
     external linkage nothing declares, i.e. an accidental export (should be `static` or in
     an anonymous namespace). `app_main` is the IDF entry point and is exempt. E3 is not
     applied to generated or vendored components (components/ui: SquareLine export,
     CS-LAY-04; components/m5stack-tab5: vendored BSP), the ratchet's own scope; their count
     is printed on every run so it is never silent. E1 and E2 cover them.
`allow` in tools/guards/baselines/exports_allowlist.json ({symbol, object, rule, reason})
excuses one finding; every entry needs a reason and is printed on every run (never silent).

Exit 0 PASS, 1 FAIL, 2 bad input.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import guardlib as gl  # noqa: E402

HERE = Path(__file__).resolve().parent
DEFAULT_ALLOW = HERE / "baselines" / "exports_allowlist.json"
STRONG = set("TDBRGSC")
LOCAL_DATA_OR_CODE = set("tdbrgs")
BUILTIN_EXEMPT = {"app_main": "the ESP-IDF entry point: called by IDF, declared by no header"}
E3_EXCLUDED_COMPONENTS = {"ui": "generated (SquareLine)", "m5stack-tab5": "vendored BSP"}
SKIP_DIRS = {"managed_components", "test", "tests", "build", "results", "sim", "docs", ".git"}


def findings(nm_rows: dict[str, list[tuple[str, str]]], dm: dict[str, str],
             tokens: set[str]) -> list[dict]:
    """Every E1/E2/E3 finding: {rule, symbol (demangled), mangled, objects, detail}."""
    out: list[dict] = []
    strong: dict[str, list[str]] = defaultdict(list)
    local_static: dict[str, list[str]] = defaultdict(list)
    for key, rows in sorted(nm_rows.items()):
        for typ, name in rows:
            if typ in STRONG:
                strong[name].append(key)
            elif typ in LOCAL_DATA_OR_CODE and name.startswith(("_ZZ", "_ZGV")):
                local_static[name].append(key)
    for name, objs in sorted(strong.items()):
        if len(set(objs)) > 1:
            out.append({"rule": "E1", "symbol": dm.get(name, name), "mangled": name,
                        "objects": sorted(set(objs)), "detail": "strong definition in several objects"})
    for name, objs in sorted(local_static.items()):
        if len(set(objs)) > 1:
            out.append({"rule": "E2", "symbol": dm.get(name, name), "mangled": name,
                        "objects": sorted(set(objs)),
                        "detail": "internal-linkage local static copied into several objects"})
    for name, objs in sorted(strong.items()):
        if name in BUILTIN_EXEMPT:
            continue
        leaf = gl.api_leaf(dm.get(name, name))
        if leaf not in tokens:
            out_of_scope = all(o.split("/", 1)[0] in E3_EXCLUDED_COMPONENTS for o in objs)
            out.append({"rule": "E3-out-of-scope" if out_of_scope else "E3",
                        "symbol": dm.get(name, name), "mangled": name,
                        "objects": sorted(set(objs)),
                        "detail": f"'{leaf}' is in no first-party header"})
    return out


def split_allowed(found: list[dict], allow: list[dict]) -> tuple[list[dict], list[tuple[dict, dict]], list[dict]]:
    """(failures, (finding, allow entry) pairs excused, allow entries that matched nothing)."""
    fails: list[dict] = []
    excused: list[tuple[dict, dict]] = []
    used: set[int] = set()
    for f in found:
        hit = next((i for i, a in enumerate(allow)
                    if a.get("rule") == f["rule"] and a.get("symbol") in (f["symbol"], f["mangled"])
                    and a.get("object", "*") in ("*", *f["objects"]) and a.get("reason")), None)
        if hit is None:
            fails.append(f)
        else:
            used.add(hit)
            excused.append((f, allow[hit]))
    stale = [a for i, a in enumerate(allow) if i not in used]
    return fails, excused, stale


def gather(args) -> tuple[list[dict], int]:
    build = gl.Build.load(args.build)
    nm = gl.find_tool("nm", build.build_dir, args.toolchain_prefix)
    try:
        cxxfilt = gl.find_tool("c++filt", build.build_dir, args.toolchain_prefix)
    except gl.GuardError:
        cxxfilt = None
    rows = gl.nm_objects(nm, build, ["--defined-only"])
    names = {n for r in rows.values() for t, n in r if t in STRONG or n.startswith(("_ZZ", "_ZGV"))}
    src = gl.local_path(args.src or build.project_path)
    if not src.is_dir():
        raise gl.GuardError(f"no source tree at {src}: pass --src")
    tokens = gl.header_tokens(src, SKIP_DIRS)
    return findings(rows, gl.demangle(names, cxxfilt), tokens), len(build.objects)


def show(f: dict) -> str:
    return f"{f['rule']} {f['symbol']}  [{', '.join(f['objects'])}]: {f['detail']}"


def cmd_check(args) -> int:
    allow_file = Path(args.allow)
    allow = json.loads(allow_file.read_text(encoding="utf-8")).get("allow", []) if allow_file.is_file() else []
    bad = [a for a in allow if not a.get("reason")]
    if bad:
        raise gl.GuardError(f"allow entries without a reason: {bad}")
    found, n_objs = gather(args)
    skipped = [f for f in found if f["rule"] == "E3-out-of-scope"]
    found = [f for f in found if f["rule"] != "E3-out-of-scope"]
    fails, excused, stale = split_allowed(found, allow)
    by_comp: dict[str, int] = defaultdict(int)
    for f in skipped:
        by_comp[f["objects"][0].split("/", 1)[0]] += 1
    for comp, n in sorted(by_comp.items()):
        print(f"note: E3 not applied to {comp} ({E3_EXCLUDED_COMPONENTS[comp]}): {n} undeclared "
              f"exports there (`list` shows them)")
    for f, a in excused:
        print(f"allowed: {show(f)} -- {a['reason']}")
    for a in stale:
        print(f"note: allow entry matched nothing (remove it): {a}")
    for f in fails:
        print(f"FAIL: {show(f)}")
    verdict = "FAIL" if fails else "PASS"
    counts = {r: sum(1 for f in fails if f["rule"] == r) for r in ("E1", "E2", "E3")}
    print(f"exports: {verdict} ({n_objs} objects; failures E1={counts['E1']} E2={counts['E2']} "
          f"E3={counts['E3']}; {len(excused)} allowed)")
    if args.json:
        gl.write_json(Path(args.json), {"verdict": verdict, "failures": fails,
                                        "allowed": [{**f, "reason": a["reason"]} for f, a in excused],
                                        "stale_allow": stale})
    return 0 if not fails else 1


def cmd_list(args) -> int:
    found, _ = gather(args)
    for f in found:
        print(show(f))
    return 0


# ---------------------------------------------------------------- selftest

SAMPLE_NM = """\
esp-idf/main/CMakeFiles/__idf_main.dir/a.cpp.obj:00000000 T _Z9a_publicv
esp-idf/main/CMakeFiles/__idf_main.dir/a.cpp.obj:00000000 T _Z12a_helper_fnv
esp-idf/main/CMakeFiles/__idf_main.dir/a.cpp.obj:00000000 b _ZZL6helperiE5count
esp-idf/main/CMakeFiles/__idf_main.dir/a.cpp.obj:00000000 b _ZGVZL6helperiE5cache
esp-idf/main/CMakeFiles/__idf_main.dir/a.cpp.obj:00000000 V _ZZN3fmt6detail1fEvE1x
esp-idf/main/CMakeFiles/__idf_main.dir/a.cpp.obj:00000000 T app_main
esp-idf/main/CMakeFiles/__idf_main.dir/a.cpp.obj:00000000 W _ZN3Foo3barEv
esp-idf/main/CMakeFiles/__idf_main.dir/b.cpp.obj:00000000 T _Z9a_publicv
esp-idf/main/CMakeFiles/__idf_main.dir/b.cpp.obj:00000000 b _ZZL6helperiE5count
esp-idf/main/CMakeFiles/__idf_main.dir/b.cpp.obj:00000000 b _ZGVZL6helperiE5cache
esp-idf/main/CMakeFiles/__idf_main.dir/b.cpp.obj:00000000 V _ZZN3fmt6detail1fEvE1x
esp-idf/main/CMakeFiles/__idf_main.dir/b.cpp.obj:00000000 R _ZTVN3hmi7MyClassE
esp-idf/main/CMakeFiles/__idf_main.dir/b.cpp.obj:00000000 D boot_logo
"""
SAMPLE_DM = {
    "_Z9a_publicv": "a_public()", "_Z12a_helper_fnv": "a_helper_fn()",
    "_ZZL6helperiE5count": "helper(int)::count",
    "_ZGVZL6helperiE5cache": "guard variable for helper(int)::cache",
    "_ZTVN3hmi7MyClassE": "vtable for hmi::MyClass",
}
SAMPLE_TOKENS = {"a_public", "MyClass", "boot_logo"}


def _sample_rows() -> dict[str, list[tuple[str, str]]]:
    rows: dict[str, list[tuple[str, str]]] = {}
    for f, r in gl.parse_nm(SAMPLE_NM).items():
        rows[gl.object_key(f)[0]] = r
    return rows


def t_parse_nm() -> None:
    rows = _sample_rows()
    gl.expect("objects", sorted(rows), ["main/a.cpp.obj", "main/b.cpp.obj"])
    gl.expect("rows in a", len(rows["main/a.cpp.obj"]), 7)


def t_rules() -> None:
    found = findings(_sample_rows(), SAMPLE_DM, SAMPLE_TOKENS)
    got = sorted((f["rule"], f["mangled"]) for f in found)
    gl.expect("findings", got, [
        ("E1", "_Z9a_publicv"),
        ("E2", "_ZGVZL6helperiE5cache"), ("E2", "_ZZL6helperiE5count"),
        ("E3", "_Z12a_helper_fnv"),
    ])
    rows = {"ui/images/x.c.obj": [("R", "ui_img_orphan")], "main/c.cpp.obj": [("T", "c_fn")]}
    got = sorted((f["rule"], f["mangled"]) for f in findings(rows, {}, set()))
    gl.expect("generated component out of E3 scope", got,
              [("E3", "c_fn"), ("E3-out-of-scope", "ui_img_orphan")])


def t_leaf() -> None:
    cases = {
        "hmi::ui::PinModel::press(int)": "press",
        "espp::Joystick::Joystick(espp::Joystick::Config const&)": "Joystick",
        "espp::Joystick::~Joystick()": "Joystick",
        "vtable for hmi::MyClass": "MyClass",
        "hmi::ota::check_first_block[abi:cxx11](std::span<unsigned char const, 4294967295u>)":
            "check_first_block",
        "espp::M5StackTab5::volume() const": "volume",
        "hmi::Foo<int>::operator()(int) const": "Foo",
        "void hmi::tpl<int>(int)": "tpl",
        "boot_logo": "boot_logo",
        "std::function<void (int)> hmi::make_cb()": "make_cb",
    }
    for dem, want in cases.items():
        gl.expect(dem, gl.api_leaf(dem), want)


def t_allow() -> None:
    found = findings(_sample_rows(), SAMPLE_DM, SAMPLE_TOKENS)
    allow = [{"rule": "E3", "symbol": "a_helper_fn()", "object": "main/a.cpp.obj", "reason": "x"},
             {"rule": "E1", "symbol": "nothing()", "reason": "y"}]
    fails, excused, stale = split_allowed(found, allow)
    gl.expect("excused", [f["mangled"] for f, _ in excused], ["_Z12a_helper_fnv"])
    gl.expect("fails left", len(fails), 3)
    gl.expect("stale", [a["symbol"] for a in stale], ["nothing()"])
    no_reason = [{"rule": "E3", "symbol": "a_helper_fn()"}]
    gl.expect("no reason, no excuse", len(split_allowed(found, no_reason)[0]), 4)


def t_tokens() -> None:
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "main").mkdir()
        (root / "main" / "x.hpp").write_text("void a_public();\n", encoding="utf-8")
        (root / "main" / "test").mkdir()
        (root / "main" / "test" / "t.hpp").write_text("void only_in_test();\n", encoding="utf-8")
        (root / "main" / "frag.inc").write_text("void only_in_inc();\n", encoding="utf-8")
        toks = gl.header_tokens(root, SKIP_DIRS)
        gl.expect("header token", "a_public" in toks, True)
        gl.expect("test dir skipped", "only_in_test" in toks, False)
        gl.expect(".inc is not a header", "only_in_inc" in toks, False)


def t_build_inside_project() -> None:
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / "proj"
        bd = root / "build_bench"
        objdir = bd / "esp-idf" / "main" / "CMakeFiles" / "__idf_main.dir"
        (objdir / "__" / "__").mkdir(parents=True)
        (root / "managed_components" / "x").mkdir(parents=True)
        entries = []
        for src, obj in ((root / "main" / "a.cpp", "a.cpp.obj"),
                         (bd / "click.wav.S", "__/__/click.wav.S.obj"),
                         (root / "managed_components" / "x" / "m.c", "m.c.obj")):
            (objdir / obj).write_bytes(b"")
            entries.append({"directory": str(bd), "file": str(src),
                            "output": f"esp-idf/main/CMakeFiles/__idf_main.dir/{obj}"})
        (bd / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
        (bd / "project_description.json").write_text(json.dumps(
            {"project_path": str(root), "build_dir": str(bd), "app_elf": "app.elf"}),
            encoding="utf-8")
        keys = [o.key for o in gl.Build.load(bd).objects]
        gl.expect("only first-party sources", keys, ["main/a.cpp.obj"])


def cmd_selftest(_args) -> int:
    return gl.run_cases("tools/guards/exports.py", [
        ("EXP-001 nm -A output parses into rows per object key", t_parse_nm),
        ("EXP-002 duplicate strong, duplicate local static and undeclared export are found", t_rules),
        ("EXP-003 a demangled symbol reduces to the identifier its header declares", t_leaf),
        ("EXP-004 allow entries excuse only with a reason and stale ones are reported", t_allow),
        ("EXP-005 header tokens skip test folders and fragments", t_tokens),
        ("EXP-006 a build folder inside the project contributes no generated sources", t_build_inside_project),
    ])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("check", "list"):
        s = sub.add_parser(name)
        s.add_argument("--build", required=True, help="ESP-IDF build folder")
        s.add_argument("--src", help="source tree for the headers (default: the build's project)")
        s.add_argument("--allow", default=str(DEFAULT_ALLOW))
        s.add_argument("--toolchain-prefix")
        s.add_argument("--json")
    sub.add_parser("selftest")
    args = p.parse_args()
    try:
        return {"check": cmd_check, "list": cmd_list, "selftest": cmd_selftest}[args.cmd](args)
    except gl.GuardError as exc:
        print(f"exports: INVALID input: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
