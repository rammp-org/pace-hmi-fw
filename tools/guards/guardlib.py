"""Shared helpers for the app_main-shrink guards (docs/plans/app-main-shrink.md, §5 and V6/V7/V11).

Stdlib only, Python 3.12+. Runs on Windows or in WSL: Windows paths from the build
(C:/...) are turned into /mnt/c/... when running on Linux.

What lives here:
- `Build`: an ESP-IDF build folder. Its first-party objects come from compile_commands.json
  (the sources under the project folder, not managed_components), keyed `<component>/<object>`
  (e.g. `main/main.cpp.obj`), so keys are the same in every build folder.
- the binutils of the build's toolchain (CMakeCache.txt names them; else the newest
  C:/Espressif/tools/riscv32-esp-elf/*), and parsers for their text output. The parsers take
  text, so each tool's `selftest` feeds them inline samples without a toolchain.
- `demangle()`, `api_leaf()` (the identifier a demangled symbol must be declared as), and a
  Unity-style case runner for the `selftest` modes (the host L1 app contract's output format).
"""

from __future__ import annotations

import inspect
import json
import os
import platform
import re
import subprocess
import sys
import traceback
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from pathlib import Path

IN_WSL = platform.system() == "Linux"
TOOLCHAIN_GLOB_ROOT = "C:/Espressif/tools/riscv32-esp-elf"
TOOL_PREFIX = "riscv32-esp-elf-"
CHUNK = 40  # objects per nm call: keeps a Windows command line far below 32 K characters


class GuardError(Exception):
    """A problem with the inputs (no build folder, no toolchain): exit 2, never a verdict."""


# ---------------------------------------------------------------- paths and processes

def local_path(path: str | Path) -> Path:
    """A path written by Windows (C:/x or C:\\x) as this OS sees it."""
    text = str(path)
    m = re.match(r"^([A-Za-z]):[/\\](.*)$", text)
    if IN_WSL and m:
        return Path(f"/mnt/{m[1].lower()}/{m[2].replace(chr(92), '/')}")
    return Path(text)


def norm(path: str | Path) -> str:
    """A path for comparison: forward slashes, lower case on Windows-style paths."""
    text = str(path).replace("\\", "/")
    return text.lower() if re.match(r"^[A-Za-z]:/", text) or not IN_WSL else text


def run(argv: list[str], cwd: Path | None = None, stdin: str | None = None) -> str:
    """Run a tool; returns stdout. A non-zero exit is a GuardError (the input is bad)."""
    try:
        proc = subprocess.run(argv, cwd=cwd, input=stdin, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", check=False)
    except OSError as exc:
        raise GuardError(f"cannot run {argv[0]}: {exc}") from exc
    if proc.returncode != 0:
        raise GuardError(f"{Path(argv[0]).name} exit {proc.returncode}: {proc.stderr.strip()[:400]}")
    return proc.stdout


# ---------------------------------------------------------------- toolchain

def find_tool(name: str, build_dir: Path | None = None, prefix: str | None = None) -> str:
    """Path of riscv32-esp-elf-<name>: --toolchain-prefix, else the build's CMakeCache.txt,
    else the newest toolchain under C:/Espressif/tools/riscv32-esp-elf."""
    def existing(stem: str) -> str | None:
        for c in (stem, stem + ".exe"):  # Linux toolchains have no .exe; Windows ones (and WSL) do
            if Path(c).is_file():
                return c
        return None

    if prefix:
        found = existing(str(local_path(prefix + name)))
        if found:
            return found
        raise GuardError(f"no {name} at {prefix}{name}")
    if build_dir is not None and (build_dir / "CMakeCache.txt").is_file():
        cache = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
        m = re.search(r"^CMAKE_NM:FILEPATH=(.*)$", cache, re.M)
        if m:
            nm = str(local_path(m[1].strip()))
            found = existing(re.sub(rf"{TOOL_PREFIX}nm(\.exe)?$", f"{TOOL_PREFIX}{name}", nm))
            if found:
                return found
    root = local_path(TOOLCHAIN_GLOB_ROOT)
    if root.is_dir():
        for ver in sorted(root.iterdir(), reverse=True):
            found = existing(str(ver / "riscv32-esp-elf" / "bin" / f"{TOOL_PREFIX}{name}"))
            if found:
                return found
    raise GuardError(f"cannot find {TOOL_PREFIX}{name}: pass --toolchain-prefix")


# ---------------------------------------------------------------- the build folder

@dataclass
class Obj:
    key: str        # `<component>/<path under __idf_<component>.dir>`, e.g. main/main.cpp.obj
    component: str
    path: Path      # the object file, as this OS sees it
    rel: str        # the object path relative to the build folder (forward slashes)
    source: Path    # the source file


@dataclass
class Build:
    build_dir: Path
    project_path: str
    elf: Path
    objects: list[Obj] = field(default_factory=list)

    @staticmethod
    def load(build_dir: str | Path) -> "Build":
        bd = local_path(build_dir)
        desc_file, cc_file = bd / "project_description.json", bd / "compile_commands.json"
        for f in (desc_file, cc_file):
            if not f.is_file():
                raise GuardError(f"not an ESP-IDF build folder (no {f.name}): {bd}")
        desc = json.loads(desc_file.read_text(encoding="utf-8"))
        project = norm(desc["project_path"]).rstrip("/") + "/"
        build = Build(bd, desc["project_path"], local_path(desc["app_elf"])
                      if Path(desc["app_elf"]).is_absolute() else bd / desc["app_elf"])
        if not build.elf.is_file():
            build.elf = bd / Path(str(desc["app_elf"])).name
        managed = project + "managed_components/"
        # a build folder inside the project (CI: build/, build_bench/) holds generated
        # sources (embedded files, the cert bundle, IPA config): not first-party code
        generated = norm(desc.get("build_dir") or bd).rstrip("/") + "/"
        seen: set[str] = set()
        for e in json.loads(cc_file.read_text(encoding="utf-8")):
            src = norm(e["file"])
            if not src.startswith(project) or src.startswith((managed, generated)):
                continue
            rel = e.get("output") or _output_of(e.get("command", ""))
            if not rel:
                continue
            rel = rel.replace("\\", "/")
            key, comp = object_key(rel)
            if key in seen:
                continue
            seen.add(key)
            build.objects.append(Obj(key, comp, bd / rel, rel, local_path(e["file"])))
        build.objects.sort(key=lambda o: o.key)
        if not build.objects:
            raise GuardError(f"no first-party objects under {project} in {cc_file}")
        missing = [o.rel for o in build.objects if not o.path.is_file()]
        if missing:
            raise GuardError(f"{len(missing)} objects are missing (build incomplete?): {missing[:3]}")
        return build


def _output_of(command: str) -> str:
    m = re.search(r"\s-o\s+(\S+)", command)
    return m[1] if m else ""


def object_key(rel: str) -> tuple[str, str]:
    """(`<component>/<object>`, component) from a build-relative object path."""
    m = re.match(r"^(?:.*/)?esp-idf/([^/]+)/CMakeFiles/__idf_[^/]+\.dir/(.+)$", rel)
    if m:
        return f"{m[1]}/{m[2]}", m[1]
    return rel, "?"


# ---------------------------------------------------------------- readelf / objdump / nm parsers

SECTION_RE = re.compile(
    r"^\s*\[\s*(\d+)\]\s+(\S+)\s+([A-Z_0-9]+)\s+[0-9a-f]{8,16}\s+[0-9a-f]+\s+[0-9a-f]+\s+"
    r"[0-9a-f]{2}\s+([A-Za-z]*)\s+\d+\s+\d+\s+\d+\s*$")
SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+([0-9a-f]+)\s+(\S+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)(?:\s+(\S+))?\s*$")


@dataclass
class Section:
    index: int
    name: str
    type: str
    flags: str


@dataclass
class Symbol:
    name: str
    type: str       # NOTYPE OBJECT FUNC SECTION FILE TLS
    bind: str       # LOCAL GLOBAL WEAK UNIQUE
    ndx: str        # section index, UND, ABS, COM
    size: int


@dataclass
class ElfObject:
    sections: dict[int, Section]
    symbols: list[Symbol]

    def by_name(self) -> dict[str, Symbol]:
        out: dict[str, Symbol] = {}
        for s in self.symbols:
            if s.name and (s.name not in out or out[s.name].ndx == "UND"):
                out[s.name] = s
        return out

    def section_of(self, sym: Symbol) -> Section | None:
        return self.sections.get(int(sym.ndx)) if sym.ndx.isdigit() else None


def parse_readelf(text: str) -> ElfObject:
    """`readelf -W -S -s` output of ONE object."""
    sections: dict[int, Section] = {}
    symbols: list[Symbol] = []
    for line in text.splitlines():
        m = SECTION_RE.match(line)
        if m:
            sections[int(m[1])] = Section(int(m[1]), m[2], m[3], m[4])
            continue
        m = SYMBOL_RE.match(line)
        if m:
            size = int(m[2], 16) if m[2].startswith("0x") else int(m[2]) if m[2].isdigit() else 0
            symbols.append(Symbol(m[7] or "", m[3], m[4], m[6], size))
    return ElfObject(sections, symbols)


RELOC_HEAD_RE = re.compile(r"^RELOCATION RECORDS FOR \[(.+)\]:\s*$")
RELOC_ROW_RE = re.compile(r"^[0-9a-f]+\s+(R_\w+)\s+(\S+)?\s*$")


def parse_objdump_relocs(text: str) -> dict[str, list[tuple[str, str]]]:
    """`objdump -r -j SEC...` output: {section: [(reloc type, symbol), ...]}; addends dropped."""
    out: dict[str, list[tuple[str, str]]] = {}
    cur: list[tuple[str, str]] | None = None
    for line in text.splitlines():
        m = RELOC_HEAD_RE.match(line)
        if m:
            cur = out.setdefault(m[1], [])
            continue
        m = RELOC_ROW_RE.match(line)
        if m and cur is not None:
            sym = re.sub(r"[+-]0x[0-9a-f]+$", "", m[2] or "")
            cur.append((m[1], sym))
    return out


NM_LINE_RE = re.compile(r"^(?P<file>.+?):(?:[0-9a-f]+)?\s+(?P<type>[A-Za-z?-])\s+(?P<name>\S+)\s*$")


def parse_nm(text: str) -> dict[str, list[tuple[str, str]]]:
    """`nm -A` output (relative object paths, no drive letters): {file: [(type, name)]}."""
    out: dict[str, list[tuple[str, str]]] = {}
    for line in text.splitlines():
        m = NM_LINE_RE.match(line)
        if m:
            out.setdefault(m["file"].replace("\\", "/"), []).append((m["type"], m["name"]))
    return out


def nm_objects(nm: str, build: Build, extra: Iterable[str] = ()) -> dict[str, list[tuple[str, str]]]:
    """nm -A over every first-party object, keyed by object key."""
    by_rel = {o.rel: o.key for o in build.objects}
    out: dict[str, list[tuple[str, str]]] = {o.key: [] for o in build.objects}
    rels = [o.rel for o in build.objects]
    for i in range(0, len(rels), CHUNK):
        text = run([nm, "-A", *extra, *rels[i:i + CHUNK]], cwd=build.build_dir)
        for f, rows in parse_nm(text).items():
            if f in by_rel:
                out[by_rel[f]].extend(rows)
    return out


# ---------------------------------------------------------------- names

def demangle(names: Iterable[str], cxxfilt: str | None) -> dict[str, str]:
    """{mangled: demangled}. Without c++filt every name maps to itself."""
    uniq = sorted(set(names))
    if not uniq or cxxfilt is None:
        return {n: n for n in uniq}
    out = run([cxxfilt], stdin="\n".join(uniq) + "\n").splitlines()
    if len(out) != len(uniq):
        raise GuardError("c++filt returned a different number of lines")
    return dict(zip(uniq, out))


SPECIAL_PREFIXES = (
    "vtable for ", "typeinfo name for ", "typeinfo for ", "VTT for ", "guard variable for ",
    "non-virtual thunk to ", "virtual thunk to ", "covariant return thunk to ",
    "construction vtable for ", "transaction clone for ", "TLS init function for ",
    "TLS wrapper function for ", "reference temporary #0 for ",
)
TRAILING_QUALIFIERS = (" const", " volatile", " &&", " &", " noexcept")


def _strip_balanced_tail(s: str, open_c: str, close_c: str) -> tuple[str, bool]:
    """Remove a trailing balanced `open_c ... close_c` group; (rest, removed?)."""
    if not s.endswith(close_c):
        return s, False
    depth = 0
    for i in range(len(s) - 1, -1, -1):
        if s[i] == close_c:
            depth += 1
        elif s[i] == open_c:
            depth -= 1
            if depth == 0:
                return s[:i], True
    return s, False


def split_scope(name: str) -> list[str]:
    """`a::b<c::d>::e` -> [a, b<c::d>, e]: `::` outside <>, () and {}."""
    parts, depth, cur, i = [], 0, "", 0
    while i < len(name):
        ch = name[i]
        if ch in "<({":
            depth += 1
        elif ch in ">)}":
            depth -= 1
        if depth == 0 and name.startswith("::", i):
            parts.append(cur)
            cur, i = "", i + 2
            continue
        cur += ch
        i += 1
    parts.append(cur)
    return parts


def qualified_name(demangled: str) -> str:
    """`ns::Cls::fn(int) const` -> `ns::Cls::fn`; special symbols (vtables...) -> their subject."""
    s = demangled.strip()
    for p in SPECIAL_PREFIXES:
        if s.startswith(p):
            return qualified_name(s[len(p):].split("-in-")[0])
    changed = True
    while changed:
        changed = False
        for q in TRAILING_QUALIFIERS:
            if s.endswith(q):
                s, changed = s[: -len(q)].rstrip(), True
    s, _ = _strip_balanced_tail(s, "(", ")")
    s = re.sub(r"\[abi:[^\]]*\]", "", s).strip()
    # a return type in front (template functions): `void ns::f<int>` -> `ns::f<int>`
    depth, cut = 0, -1
    for i, ch in enumerate(s):
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        elif ch == " " and depth == 0 and not s[:i].endswith("operator"):
            cut = i
    if cut >= 0:
        s = s[cut + 1:]
    return s


def api_leaf(demangled: str) -> str:
    """The identifier a header must declare for this symbol to be its API.

    `hmi::ui::PinModel::press(int)` -> `press`; a constructor `espp::Joystick::Joystick(..)` ->
    `Joystick`; `vtable for hmi::X` -> `X`; an operator -> its class; a C name -> itself.
    """
    parts = [p for p in split_scope(qualified_name(demangled)) if p]
    if not parts:
        return demangled
    leaf = parts[-1]
    if leaf.startswith("operator") or leaf.startswith("{lambda") or leaf.startswith("{unnamed"):
        leaf = parts[-2] if len(parts) > 1 else leaf
    leaf, _ = _strip_balanced_tail(leaf, "<", ">")
    return leaf.lstrip("~").strip()


IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def header_tokens(root: Path, skip_dirs: set[str]) -> set[str]:
    """Every identifier in the first-party headers under `root` (comments included: a name
    only mentioned in a comment still counts, the check is about linkage, not docs)."""
    tokens: set[str] = set()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in skip_dirs and not d.startswith(".")
                       and not d.startswith("build")]
        for f in filenames:
            if f.endswith((".h", ".hpp", ".hh", ".hxx")):
                p = Path(dirpath) / f
                tokens.update(IDENT_RE.findall(p.read_text(encoding="utf-8", errors="replace")))
    return tokens


# ---------------------------------------------------------------- reporting and selftest

def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=False) + "\n", encoding="utf-8", newline="\n")


def run_cases(script: str, cases: list[tuple[str, Callable[[], None]]]) -> int:
    """Run selftest cases and print Unity's format (case lines + summary): exit 0 = all pass.

    A case raises AssertionError (or anything) to fail. Names are `<ID> <sentence>` (TS-UNIT-02).
    """
    fails = 0
    for name, fn in cases:
        line = inspect.getsourcelines(fn)[1] if inspect.isfunction(fn) else 0
        try:
            fn()
            print(f"{script}:{line}:{name}:PASS")
        except Exception as exc:  # noqa: BLE001 - every exception is a failed case
            fails += 1
            msg = str(exc) or type(exc).__name__
            print(f"{script}:{line}:{name}:FAIL: {msg}")
            traceback.print_exc(file=sys.stdout)
    print("\n-----------------------")
    print(f"{len(cases)} Tests {fails} Failures 0 Ignored")
    print("OK" if fails == 0 else "FAIL")
    return 0 if fails == 0 else 1


def expect(what: str, got: object, want: object) -> None:
    if got != want:
        raise AssertionError(f"{what}: got {got!r}, want {want!r}")
