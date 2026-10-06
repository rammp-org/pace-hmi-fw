#!/usr/bin/env python3
"""L0 ratchet: legacy debt may shrink, never grow; new files start clean.

Counts per-file grep metrics over the first-party sources and compares them with
tools/l0/baseline.json (TS-LVL L0; CODING_SPEC CS-FIL-01, CS-LOG-01, CS-CON,
CS-MEM-01, CS-NAM-01, CS-TYP-04/05, CS-CMP-03, CS-ERR-04).

  python tools/l0/ratchet.py check      exit 0 = PASS, 1 = FAIL
  python tools/l0/ratchet.py update     lower the baseline to today's counts; never raises
  python tools/l0/ratchet.py update --init   write the first baseline (only if none exists)
  python tools/l0/ratchet.py selftest   exercise the parser on inline samples

Scope: main/** and components/** except components/ui (generated),
components/m5stack-tab5 (vendored), any test/ or generated/ folder, and
main/boot_logo.[ch] (generated pixel data). components/joystick and
main/sample_ui_* are in scope as legacy.

main/main.cpp and every main/frag_*.inc are ONE unit named main/main.cpp: each
`#include "frag_*.inc"` line is replaced by the fragment's text before counting,
so splitting main.cpp into fragments moves no count.

Comments and string/char literals are stripped before anything is counted.

Rules
- lines (non-blank lines after stripping comments): a path in the baseline must
  not grow; any other path must stay within CS-FIL-01 (header 800, other 1000).
  The baseline holds only files over the limit.
- fn_over_60 (SHOULD, CS-FIL-01): a path in the baseline must not grow; other
  paths are not limited by it, except in a component whose own .clang-tidy sets
  readability-function-size.LineThreshold to 60 (CS-FIL-01 safety code): there it
  is forbidden like the metrics below.
- every other metric is forbidden: count <= baseline, and a path absent from the
  baseline must have 0.
- static_state (CS-CMP-03 [review] "no mutable function-local static"; CS-OWN):
  non-const `static`/`thread_local` variables at block scope (function-local
  statics, in lambdas too) and static data members at class scope. Namespace-scope
  statics are mutable_globals, not this.
- app_main_lines (CS-LAY-01: app_main <= 300 lines): non-blank code lines (comments
  stripped, as `lines`) of the function named app_main, in whichever unit holds it.
  Like `lines`: a path in the baseline must not grow; any other path must stay
  within 300. The baseline holds only paths over the limit.

Heuristics (regex and brace depth), not a compiler: they count the same way every
time, which is all a ratchet needs. Stdlib only, Python 3.12+.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import pathlib
import re
import sys
import tempfile
from collections.abc import Callable

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).resolve().parent / "baseline.json"

SOURCE_EXT = {".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".ipp"}
HEADER_EXT = {".h", ".hpp"}
LIMIT_HEADER = 800
LIMIT_SOURCE = 1000
LIMIT_FUNCTION = 120
SHOULD_FUNCTION = 60
LIMIT_APP_MAIN = 300  # CS-LAY-01
APP_MAIN = "app_main"

EXCLUDED_COMPONENTS = {"ui", "m5stack-tab5"}
EXCLUDED_DIRS = {"test", "generated"}
EXCLUDED_FILES = {"main/boot_logo.c", "main/boot_logo.h"}
UNIT = "main/main.cpp"
FRAG_RE = re.compile(r"^main/frag_[A-Za-z0-9_]+\.inc$")
FRAG_INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"(frag_[A-Za-z0-9_]+\.inc)"[ \t]*(//.*)?$')

# UI files may call lv_* (CS-UI: only the UI task touches LVGL).
UI_FILE_RES = [
    re.compile(r"^main/[A-Za-z0-9_]+_ui\.(?:cpp|hpp|h)$"),
    re.compile(r"^main/log_view\.(?:cpp|hpp|h)$"),
    re.compile(r"^main/joystick_cal\.(?:cpp|hpp|h)$"),
    re.compile(r"^main/main\.cpp$"),
    re.compile(r"^main/sample_ui_[A-Za-z0-9_]+\.(?:c|h)$"),
    re.compile(r"^components/ui/"),
]

# --- metrics -----------------------------------------------------------------

CAST_TYPE = (
    r"(?:(?:const|volatile|unsigned|signed|struct)\s+)*"
    r"(?:void\s*\*|char|short|int|long(?:\s+long)?|float|double|bool|"
    r"[A-Za-z_]\w*_t|lv_\w+)(?:\s*\*+)?"
)
CAST_RE = re.compile(r"\(\s*" + CAST_TYPE + r"\s*\)\s*(?=[\w(&*~!\[-])")
CAST_SKIP_WORDS = {"sizeof", "alignof", "_Alignof", "decltype", "typeof", "__typeof__", "alignas"}

REGEX_METRICS: dict[str, re.Pattern[str]] = {
    "std_thread": re.compile(r"\bstd::j?thread\b"),
    "xtaskcreate": re.compile(r"\bxTaskCreate\w*"),
    "vtaskdelay": re.compile(r"\bvTaskDelay(?:Until)?\b"),
    "sleep_this_thread": re.compile(r"\bthis_thread::sleep_(?:for|until)\b"),
    "esp_timer_create": re.compile(r"\besp_timer_create\b"),
    "log_direct": re.compile(r"\bprintf\s*\(|\bfmt::print(?:ln)?\b|\bESP_(?:EARLY_|DRAM_)?LOG[EWIDV]\b"),
    "locks": re.compile(
        r"\b(?:recursive_|timed_|shared_|recursive_timed_)?mutex\b|\block_guard\b|\bunique_lock\b"
        r"|\bscoped_lock\b|\bxSemaphore\w*|\bportMUX\w*"
    ),
    "heap_raw": re.compile(
        r"\bnew\b|\b(?:malloc|calloc|realloc|free|aligned_alloc|strdup)\s*\(|\bheap_caps_\w+"
    ),
    "typedef": re.compile(r"\btypedef\b"),
    "assert": re.compile(r"\bassert\s*\("),
    "esp_error_check": re.compile(r"\bESP_ERROR_CHECK\b"),
}
LV_RE = re.compile(r"\blv_\w+")
IF_CONFIG_RE = re.compile(r"^[ \t]*#[ \t]*(?:if|ifdef|ifndef|elif|elifdef|elifndef)\b.*\bCONFIG_", re.M)
INCLUDE_LINE_RE = re.compile(r"^[ \t]*#[ \t]*include\b.*$", re.M)

METRICS = sorted(
    list(REGEX_METRICS)
    + ["lines", "fn_over_120", "fn_over_60", "lv_outside_ui", "c_cast", "if_config", "mutable_globals",
       "static_state", "app_main_lines"]
)
LIMIT_METRICS = {"lines", "fn_over_60", "app_main_lines"}  # everything else is forbidden on new paths


# --- lexing ------------------------------------------------------------------

RAW_PREFIX_RE = re.compile(r'(?:u8|u|U|L)?R"([^()\\ \t\n]{0,16})\(')


def strip_code(text: str) -> str:
    """Remove comments and the contents of string/char literals; keep every newline."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
            continue
        if c in "uULR" and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = RAW_PREFIX_RE.match(text, i)
            if m:
                end = text.find(")" + m.group(1) + '"', m.end())
                end = n if end < 0 else end + len(m.group(1)) + 2
                out.append('""' + "\n" * text.count("\n", i, end))
                i = end
                continue
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"' and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append('""')
            i = j + 1
            continue
        if c == "'":
            k = i
            while k > 0 and (text[k - 1].isalnum() or text[k - 1] in "_'"):
                k -= 1
            if k < i and text[k].isdigit():  # digit separator: 1'000'000
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != "'" and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append("''")
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def blank_preprocessor(code: str) -> str:
    """Blank every preprocessor line (with its continuations), keeping line count."""
    lines = code.split("\n")
    cont = False
    for idx, line in enumerate(lines):
        if cont or line.lstrip().startswith("#"):
            cont = line.rstrip().endswith("\\")
            lines[idx] = ""
    return "\n".join(lines)


# --- structure: function lengths and namespace-scope variables ---------------

NS_RE = re.compile(r"(?:inline\s+)?namespace\b[\w:\s]*|extern\s*\"\"")
TYPE_HEAD_RE = re.compile(r"\b(?:class|struct|union)\b")
ENUM_HEAD_RE = re.compile(r"\benum\b")
CTOR_INIT_RE = re.compile(r"\)\s*(?:noexcept\s*)?:(?!:)")
OPERATOR_RE = re.compile(r"\boperator\s*(?:\(\)|[^\s(]+)")
NOT_VAR_START_RE = re.compile(
    r"^(?:using|typedef|friend|static_assert|extern|template|class|struct|enum|union|namespace|"
    r"return|public|private|protected)\b"
)
CONST_RE = re.compile(r"\b(?:constexpr|const)\b")


def _drop_template_params(head: str) -> str:
    """Remove a leading `template <...>` (nested angles) so its `=` defaults don't count."""
    h = head.lstrip()
    while h.startswith("template"):
        k = h.find("<")
        if k < 0:
            return h
        depth, j = 0, k
        while j < len(h):
            if h[j] == "<":
                depth += 1
            elif h[j] == ">":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        h = h[j + 1 :].lstrip()
    return h


def _paren_before_eq(head: str) -> bool:
    h = OPERATOR_RE.sub("operator", _drop_template_params(head))
    p = h.find("(")
    e = re.search(r"(?<![=!<>])=(?!=)", h)
    return p >= 0 and (e is None or p < e.start())


def _classify(head: str) -> str:
    h = " ".join(head.split())
    if not h:
        return "other"
    if NS_RE.fullmatch(h):
        return "ns"
    bare = _drop_template_params(h)
    if TYPE_HEAD_RE.search(bare) and "(" not in bare and "=" not in bare:
        return "class"
    if ENUM_HEAD_RE.search(bare) and "=" not in bare:
        return "enum"
    if _paren_before_eq(h):
        if CTOR_INIT_RE.search(h) and re.search(r"[\w>]$", h):
            return "init"
        return "func"
    return "var"


def _is_mutable_var(head: str, at_brace: bool) -> bool:
    h = " ".join(head.split())
    if not h or NOT_VAR_START_RE.match(h) or CONST_RE.search(h):
        return False
    if at_brace:
        return True
    return not _paren_before_eq(h)


ACCESS_RE = re.compile(r"^\s*(?:(?:public|private|protected)\s*:(?!:)\s*)+")
ATTRIBUTE_RE = re.compile(r"\[\[.*?\]\]\s*")
STORAGE_WORDS = {"static", "thread_local", "inline", "constinit", "volatile", "constexpr", "const", "mutable"}
STATIC_WORD_RE = re.compile(r"\b(?:static|thread_local)\b")


def _strip_angles(h: str) -> str:
    """Remove `<...>` template argument lists (nested), so `std::function<void()> f` reads as a variable."""
    out: list[str] = []
    depth = 0
    for i, c in enumerate(h):
        if c == "<" and (depth or (i > 0 and (h[i - 1].isalnum() or h[i - 1] in "_ "))):
            depth += 1
            continue
        if c == ">" and depth:
            depth -= 1
            continue
        if not depth:
            out.append(c)
    return "".join(out)


def _static_offset(head: str, scope: str) -> int | None:
    """Offset of `static`/`thread_local` if `head` declares a non-const static-storage variable.

    scope "class": a static data member (a head with `(` before `=` is a member function);
    scope "block": a function-local static (a block-scope `static` is always a variable).
    """
    h = ACCESS_RE.sub(lambda m: " " * len(m.group(0)), head) if scope == "class" else head
    h = ATTRIBUTE_RE.sub(lambda m: " " * len(m.group(0)), h)
    pos = len(h) - len(h.lstrip())
    leading: list[str] = []
    for w in re.finditer(r"\w+|\S", h[pos:]):
        if w.group(0) not in STORAGE_WORDS:
            break
        leading.append(w.group(0))
    if "static" not in leading and "thread_local" not in leading:
        return None
    flat = " ".join(h.split())
    if CONST_RE.search(flat):
        return None
    if scope == "class" and "(" in _strip_angles(_drop_template_params(flat)).split("=", 1)[0]:
        return None  # a static member function
    m = STATIC_WORD_RE.search(h, pos)
    return m.start() if m else None


class Structure:
    """What one brace-depth pass over a unit finds."""

    def __init__(self) -> None:
        self.lengths: list[int] = []  # function body lengths in lines
        self.functions: list[tuple[str, int, int]] = []  # (name, first line, last line)
        self.global_lines: list[int] = []  # mutable namespace-scope variables (line of the head)
        self.static_lines: list[int] = []  # non-const function-local statics and static data members

    @property
    def globals_(self) -> int:
        return len(self.global_lines)


def _function_name(head: str) -> str:
    h = OPERATOR_RE.sub("operator", _drop_template_params(" ".join(head.split())))
    p = h.find("(")
    m = re.search(r"([~\w:]+)\s*$", h[:p] if p >= 0 else h)
    return m.group(1) if m else ""


def scan(code: str) -> Structure:
    """Functions, mutable namespace-scope variables and non-const statics of one unit.

    `code` is stripped and preprocessor-blanked. Functions are brace blocks opened
    at namespace or class scope by a head with `(` before any `=`. Every other block
    (a function body, a lambda, a braced initialiser) is block scope.
    """
    st = Structure()
    stack: list[tuple[str, int, str]] = []
    head: list[str] = []  # declaration text since the last `;`, `{` or `}` (namespace and class scope)
    head_ln: list[int] = []  # the line of each character in `head`
    stmt: list[str] = []  # statement text since the last `;`, `{` or `}` (block scope)
    stmt_ln: list[int] = []
    line = 1

    def first_line(lns: list[int], text: str) -> int:
        k = len(text) - len(text.lstrip())
        return lns[k] if k < len(lns) else line

    def block_static() -> None:
        text = "".join(stmt)
        k = _static_offset(text, "block")
        if k is not None:
            st.static_lines.append(stmt_ln[k])

    for ch in code:
        decl = not stack or stack[-1][0] in ("ns", "class")
        in_class = bool(stack) and stack[-1][0] == "class"
        all_ns = all(k == "ns" for k, _, _ in stack)
        if ch == "\n":
            line += 1
            if decl:
                head.append(" ")
                head_ln.append(line)
            else:
                stmt.append(" ")
                stmt_ln.append(line)
        elif ch == "{":
            if decl and "".join(head).count("(") > "".join(head).count(")"):
                stack.append(("init", line, ""))  # e.g. `f(int x = {})`: still inside the head
                continue
            if decl:
                text = "".join(head)
                kind = _classify(text)
                if kind == "init":
                    stack.append(("init", line, ""))
                    continue
                if kind == "var" and all_ns and _is_mutable_var(text, at_brace=True):
                    st.global_lines.append(first_line(head_ln, text))
                if kind == "var" and in_class:
                    k = _static_offset(text, "class")
                    if k is not None:
                        st.static_lines.append(head_ln[k])
                name = _function_name(text) if kind == "func" else ""
                stack.append((kind if kind in ("ns", "class", "func") else "other", line, name))
                head, head_ln = [], []
            else:
                block_static()
                stack.append(("other", line, ""))
            stmt, stmt_ln = [], []
        elif ch == "}":
            stmt, stmt_ln = [], []
            if not stack:
                continue  # unbalanced (e.g. braces split across #if arms): stay at top level
            kind, start, name = stack.pop()
            if kind == "func":
                st.lengths.append(line - start + 1)
                st.functions.append((name, start, line))
            if kind == "init":
                head.append("{}")
                head_ln.extend((line, line))
            elif not stack or stack[-1][0] in ("ns", "class"):
                head, head_ln = [], []
        elif decl:
            if ch == ";":
                text = "".join(head)
                if all_ns and _is_mutable_var(text, at_brace=False):
                    st.global_lines.append(first_line(head_ln, text))
                if in_class:
                    k = _static_offset(text, "class")
                    if k is not None:
                        st.static_lines.append(head_ln[k])
                head, head_ln = [], []
            else:
                head.append(ch)
                head_ln.append(line)
        elif ch == ";":
            block_static()
            stmt, stmt_ln = [], []
        else:
            stmt.append(ch)
            stmt_ln.append(line)
    return st


def scan_structure(code: str) -> tuple[list[int], int]:
    """Return (function body lengths in lines, mutable namespace-scope variable count)."""
    st = scan(code)
    return st.lengths, st.globals_


# --- per-file metrics --------------------------------------------------------


def is_ui_file(path: str) -> bool:
    return any(r.search(path) for r in UI_FILE_RES)


def count_casts(code: str) -> int:
    count = 0
    for m in CAST_RE.finditer(code):
        before = code[: m.start()]
        if before and (before[-1].isalnum() or before[-1] == "_"):
            continue  # a call or macro: f(int), sizeof(int)
        word = re.search(r"(\w+)\s*$", before)
        if word and word.group(1) in CAST_SKIP_WORDS:
            continue
        count += 1
    return count


def measure(path: str, text: str) -> dict[str, int]:
    """Every metric for one unit of source text."""
    code = strip_code(text)
    no_inc = INCLUDE_LINE_RE.sub("", code)
    m: dict[str, int] = {name: len(rx.findall(no_inc)) for name, rx in REGEX_METRICS.items()}
    m["lines"] = sum(1 for ln in code.split("\n") if ln.strip())
    m["if_config"] = len(IF_CONFIG_RE.findall(code))
    m["lv_outside_ui"] = 0 if is_ui_file(path) else len(LV_RE.findall(no_inc))
    m["c_cast"] = count_casts(no_inc)
    st = scan(blank_preprocessor(code))
    m["fn_over_120"] = sum(1 for n in st.lengths if n > LIMIT_FUNCTION)
    m["fn_over_60"] = sum(1 for n in st.lengths if n > SHOULD_FUNCTION)
    m["mutable_globals"] = st.globals_
    m["static_state"] = len(st.static_lines)
    lines = code.split("\n")
    m["app_main_lines"] = max(
        (sum(1 for ln in lines[a - 1 : b] if ln.strip()) for name, a, b in st.functions if name == APP_MAIN),
        default=0,
    )
    return m


# --- file set ----------------------------------------------------------------


def in_scope(rel: str) -> bool:
    parts = rel.split("/")
    if pathlib.PurePosixPath(rel).suffix not in SOURCE_EXT or rel in EXCLUDED_FILES:
        return False
    if parts[0] == "main":
        return not (set(parts[1:-1]) & EXCLUDED_DIRS)
    if parts[0] == "components" and len(parts) >= 3:
        return parts[1] not in EXCLUDED_COMPONENTS and not (set(parts[2:-1]) & EXCLUDED_DIRS)
    return False


def read_text(p: pathlib.Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def collect(root: pathlib.Path) -> dict[str, str]:
    """{unit path: source text}, with main.cpp's fragments spliced in."""
    files: dict[str, str] = {}
    for top in ("main", "components"):
        base = root / top
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            rel = p.relative_to(root).as_posix()
            if p.is_file() and in_scope(rel):
                files[rel] = read_text(p)
    frags = {k: v for k, v in files.items() if FRAG_RE.match(k)}
    for k in frags:
        del files[k]
    if UNIT in files or frags:
        files[UNIT] = splice_unit(files.get(UNIT, ""), frags)
    return files


def splice_unit(main_text: str, frags: dict[str, str]) -> str:
    """main.cpp with each `#include "frag_*.inc"` replaced by the fragment's text."""
    used: set[str] = set()
    out: list[str] = []
    for line in main_text.split("\n"):
        m = FRAG_INCLUDE_RE.match(line)
        key = f"main/{m.group(1)}" if m else None
        if key in frags:
            used.add(key)
            out.append(_frag_body(frags[key]))
        else:
            out.append(line)
    for key in sorted(set(frags) - used):  # orphan fragments still count
        out.append(_frag_body(frags[key]))
    return "\n".join(out)


def _frag_body(text: str) -> str:
    lines = [ln for ln in text.rstrip("\n").split("\n") if not re.match(r"^\s*#\s*pragma\s+once\b", ln)]
    return "\n".join(lines)


def measure_tree(root: pathlib.Path) -> dict[str, dict[str, int]]:
    return {path: measure(path, text) for path, text in collect(root).items()}


# --- baseline and verdict ----------------------------------------------------


def line_limit(path: str) -> int:
    return LIMIT_HEADER if pathlib.PurePosixPath(path).suffix in HEADER_EXT else LIMIT_SOURCE


def hard_limit(name: str, path: str) -> int | None:
    """The limit of a limit metric (the baseline holds only paths over it); None if forbidden."""
    if name == "lines":
        return line_limit(path)
    if name == "app_main_lines":
        return LIMIT_APP_MAIN
    return None


def make_baseline(current: dict[str, dict[str, int]]) -> dict[str, dict[str, int]]:
    out: dict[str, dict[str, int]] = {name: {} for name in METRICS}
    for path, m in current.items():
        for name in METRICS:
            n = m[name]
            limit = hard_limit(name, path)
            if n > (0 if limit is None else limit):
                out[name][path] = n
    return out


# Placements a rule prescribes, so they are not legacy debt:
#   locks in the channel helpers (CS-OWN-08: "Mutexes and semaphores appear only in the
#   channel helpers ..."); `#ifdef CONFIG_` in a component's config header (CS-TYP-05:
#   "Each Kconfig option becomes a constexpr value, once, in the component's config header").
RULE_PLACEMENTS: dict[str, re.Pattern[str]] = {
    "locks": re.compile(r"^components/fw_core/(include/fw_core/port/|src/port_)"),
    "if_config": re.compile(r"^components/[^/]+/include/(.+/)?config\.hpp$"),
}


# A component whose own .clang-tidy sets readability-function-size.LineThreshold to 60 (or
# less) is safety code for CS-FIL-01 ("Safety-relevant components: function 60, set in their
# own .clang-tidy"): there fn_over_60 is a hard limit, not a SHOULD. Both YAML forms of
# CheckOptions are read (list of {key, value}, and a key: value map).
FUNCTION_SIZE_RE = re.compile(
    r"readability-function-size\.LineThreshold['\"]?\s*(?:,?\s*value\s*:|:)\s*['\"]?(\d+)"
)


def component_of(path: str) -> str:
    """`components/<name>` for a component file, `main` for main/."""
    parts = path.split("/")
    return "/".join(parts[:2]) if parts[0] == "components" else parts[0]


def clang_tidy_threshold(text: str) -> int | None:
    text = re.sub(r"(?m)#.*$", "", text)
    m = FUNCTION_SIZE_RE.search(text)
    return int(m.group(1)) if m else None


def strict_components(root: pathlib.Path) -> frozenset[str]:
    """Components (and main) whose own .clang-tidy sets a function LineThreshold <= 60."""
    out: set[str] = set()
    dirs = [root / "main"] + (sorted((root / "components").iterdir()) if (root / "components").is_dir() else [])
    for d in dirs:
        f = d / ".clang-tidy"
        if f.is_file():
            n = clang_tidy_threshold(read_text(f))
            if n is not None and n <= SHOULD_FUNCTION:
                out.add(d.relative_to(root).as_posix())
    return frozenset(out)


def violations(base: dict[str, dict[str, int]], current: dict[str, dict[str, int]],
               strict: frozenset[str] = frozenset()) -> list[str]:
    """FAIL lines. `strict`: components where fn_over_60 is a hard limit (strict_components)."""
    bad: list[str] = []
    for path in sorted(current):
        for name in METRICS:
            n = current[path][name]
            have = base.get(name, {})
            if path in have:
                if n > have[path]:
                    bad.append(f"{name} {path}: {n} > baseline {have[path]}")
            elif name == "lines":
                if n > line_limit(path):
                    bad.append(f"lines {path}: {n} > hard limit {line_limit(path)} (CS-FIL-01)")
            elif name == "app_main_lines":
                if n > LIMIT_APP_MAIN:
                    bad.append(f"app_main_lines {path}: {n} > hard limit {LIMIT_APP_MAIN} (CS-LAY-01)")
            elif name == "fn_over_60":
                if component_of(path) in strict and n > 0:
                    bad.append(f"fn_over_60 {path}: {n} > 0 ({component_of(path)}/.clang-tidy sets "
                               f"LineThreshold {SHOULD_FUNCTION}: hard limit, CS-FIL-01)")
                # elsewhere SHOULD only; fn_over_120 is the hard limit
            elif name in RULE_PLACEMENTS and RULE_PLACEMENTS[name].match(path):
                pass  # the rule puts it here
            elif n > 0:
                bad.append(f"{name} {path}: {n} > 0 (not in baseline: must be clean)")
    return bad


def lowered(base: dict[str, dict[str, int]], current: dict[str, dict[str, int]]) -> dict[str, dict[str, int]]:
    """min(old, new) for every baseline entry; entries that reach 0 (or the limit) go."""
    out: dict[str, dict[str, int]] = {name: {} for name in METRICS}
    for name in METRICS:
        for path, old in base.get(name, {}).items():
            n = current.get(path, {}).get(name, 0)
            new = min(old, n)
            limit = hard_limit(name, path)
            if new > (0 if limit is None else limit):
                out[name][path] = new
    return out


def dump(base: dict[str, dict[str, int]]) -> str:
    doc = {"version": 1, "metrics": {k: dict(sorted(base[k].items())) for k in sorted(base)}}
    return json.dumps(doc, indent=2, sort_keys=True) + "\n"


def load(path: pathlib.Path) -> dict[str, dict[str, int]]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    if doc.get("version") != 1:
        raise SystemExit(f"ratchet: {path}: unknown baseline version {doc.get('version')}")
    return doc["metrics"]


def totals(base: dict[str, dict[str, int]]) -> dict[str, int]:
    return {name: sum(base.get(name, {}).values()) for name in METRICS}


# --- commands ----------------------------------------------------------------


def cmd_check(root: pathlib.Path, baseline: pathlib.Path) -> int:
    base = load(baseline)
    current = measure_tree(root)
    bad = violations(base, current, strict_components(root))
    for line in bad:
        print(f"FAIL {line}")
    stale = sum(1 for name, paths in base.items() for p, old in paths.items()
                if current.get(p, {}).get(name, 0) < old)
    if stale:
        print(f"note: {stale} baseline entries can be lowered: run `ratchet.py update`")
    verdict = "FAIL" if bad else "PASS"
    print(f"ratchet: {verdict} ({len(current)} units, {len(bad)} violations)")
    return 1 if bad else 0


def cmd_update(root: pathlib.Path, baseline: pathlib.Path, init: bool) -> int:
    current = measure_tree(root)
    if init:
        if baseline.exists():
            print(f"ratchet: {baseline} exists; --init only writes the first baseline")
            return 1
        new = make_baseline(current)
    else:
        base = load(baseline)
        bad = violations(base, current, strict_components(root))
        if bad:
            for line in bad:
                print(f"FAIL {line}")
            print("ratchet: update refuses to raise the baseline; fix the code instead")
            return 1
        new = lowered(base, current)
    baseline.parent.mkdir(parents=True, exist_ok=True)
    baseline.write_bytes(dump(new).encode("utf-8"))
    for name, n in totals(new).items():
        print(f"{name:18} {n}")
    print(f"ratchet: wrote {baseline}")
    return 0


# --- selftest ----------------------------------------------------------------

SAMPLE = r'''
// printf("in a comment") ESP_LOGI std::thread
/* lv_obj_create( new malloc( */
#include <mutex>
#include "lvgl.h"
#ifdef CONFIG_HMI_REMOTE_UI
#endif
#if defined(CONFIG_X) && 1
#endif
#if SOMETHING_ELSE
#endif
namespace hmi {
namespace {
int counter = 0;
static lv_obj_t *screen = nullptr;
constexpr int LIMIT = 3;
const char *const NAME = "x{";
std::array<int, 2> table{1, 2};
static void helper(int a = 1);
}  // namespace
typedef int legacy_t;
struct Config {
  int x;
  static int shared;
  void method() {
    std::mutex m;
    std::lock_guard<std::mutex> g(m);
  }
};
enum class Mode { A, B };
void f() {
  const char *s = "printf(\"ESP_LOGI}\")";
  char c = '}';
  long big = 1'000'000;
  auto *p = new int(3);
  void *q = malloc(4);
  void *r = heap_caps_malloc(4, 0);
  int i = (int)big;
  size_t z = sizeof(int) * 2;
  (void)c;
  std::thread t([] {});
  xTaskCreatePinnedToCore(nullptr, "", 0, nullptr, 0, nullptr, 0);
  vTaskDelay(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  esp_timer_create(nullptr, nullptr);
  printf("%d", i);
  fmt::print("x");
  ESP_LOGI("t", "x");
  lv_obj_t *o = lv_obj_create(nullptr);
  assert(p);
  static_assert(true);
  ESP_ERROR_CHECK(esp_timer_create(nullptr, nullptr));
  snprintf(nullptr, 0, "x");
}
}  // namespace hmi
'''

SAMPLE_EXPECT = {
    "std_thread": 1,
    "xtaskcreate": 1,
    "vtaskdelay": 1,
    "sleep_this_thread": 1,
    "esp_timer_create": 2,
    "log_direct": 3,
    "locks": 3,  # mutex, lock_guard, mutex (template argument)
    "heap_raw": 3,  # new, malloc(, heap_caps_malloc
    "typedef": 1,
    "assert": 1,
    "esp_error_check": 1,
    "lv_outside_ui": 3,  # lv_obj_t (x2), lv_obj_create
    "c_cast": 1,
    "if_config": 2,
    "mutable_globals": 3,  # counter, screen, table
    "static_state": 1,  # Config::shared
    "fn_over_60": 0,
    "fn_over_120": 0,
}


def _long_fn(name: str, body_lines: int) -> str:
    return f"void {name}() {{\n" + "  x();\n" * body_lines + "}\n"


Expect = Callable[[str, object, object], None]


def _selftest_strict_functions(expect: Expect) -> None:
    """fn_over_60 is a hard limit only in components whose .clang-tidy sets LineThreshold 60."""
    zero = dict.fromkeys(METRICS, 0)
    tidy = {
        "safe": "Checks: '-*'\nInheritParentConfig: true\nCheckOptions:\n"
                "  - { key: readability-function-size.LineThreshold, value: 60 }\n",
        "block": "CheckOptions:\n  - key: readability-function-size.LineThreshold\n    value: '60'\n",
        "map": "CheckOptions:\n  readability-function-size.LineThreshold: 40\n",
        "loose": "CheckOptions:\n  - { key: readability-function-size.LineThreshold, value: 120 }\n",
        "commented": "CheckOptions:\n  # - { key: readability-function-size.LineThreshold, value: 60 }\n",
        "other": "CheckOptions:\n  - { key: readability-function-size.StatementThreshold, value: 60 }\n",
    }
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        for name, text in tidy.items():
            (root / "components" / name / "src").mkdir(parents=True)
            (root / "components" / name / ".clang-tidy").write_text(text, encoding="utf-8")
        (root / "components" / "plain" / "src").mkdir(parents=True)
        strict = strict_components(root)
        expect("strict components (LineThreshold <= 60, list/block/map YAML)", sorted(strict),
               ["components/block", "components/map", "components/safe"])
        over = {**zero, "fn_over_60": 1}
        expect("fn_over_60 forbidden in a 60-line component",
               len(violations({}, {"components/safe/src/a.cpp": over}, strict)), 1)
        for name in ("loose", "commented", "other", "plain"):
            expect(f"fn_over_60 still a SHOULD in components/{name}",
                   violations({}, {f"components/{name}/src/a.cpp": over}, strict), [])
        expect("fn_over_60 still a SHOULD in main/", violations({}, {"main/x.cpp": over}, strict), [])
        expect("fn_over_60 SHOULD when no strict set is given",
               violations({}, {"components/safe/src/a.cpp": over}), [])
        legacy = {"fn_over_60": {"components/safe/src/a.cpp": 2}}
        expect("strict legacy within its baseline",
               violations(legacy, {"components/safe/src/a.cpp": {**zero, "fn_over_60": 2}}, strict), [])
        expect("strict legacy may not grow",
               len(violations(legacy, {"components/safe/src/a.cpp": {**zero, "fn_over_60": 3}}, strict)), 1)
        # End to end: a 61-line function fails check only in the strict component.
        bl = root / "baseline.json"
        bl.write_text(dump(make_baseline({})), encoding="utf-8")
        (root / "components" / "loose" / "src" / "a.cpp").write_text(_long_fn("a", 59), encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            expect("check: 61-line function in a 120-line component", cmd_check(root, bl), 0)
            (root / "components" / "safe" / "src" / "a.cpp").write_text(_long_fn("a", 59), encoding="utf-8")
            expect("check: 61-line function in a 60-line component", cmd_check(root, bl), 1)
            expect("update refuses it too", cmd_update(root, bl, init=False), 1)
            (root / "components" / "safe" / "src" / "a.cpp").write_text(_long_fn("a", 58), encoding="utf-8")
            expect("check: 60-line function in a 60-line component", cmd_check(root, bl), 0)


def _selftest_app_main(expect: Expect) -> None:
    """app_main_lines: non-blank code lines of app_main; 300 hard limit; legacy may only fall."""
    zero = dict.fromkeys(METRICS, 0)
    body = "  x();\n" * 5 + "\n  // a comment line\n  /* block\n     comment */\n\n" + "  y();\n" * 3
    unit = (
        "void app_main_helper() {\n" + "  z();\n" * 400 + "}\n"
        'extern "C" void app_main(void) {\n' + body + "}\n"
        "struct Foo { void app_main_like() {\n" + "  z();\n" * 50 + "} };\n"
    )
    expect("app_main_lines counts non-blank code lines of app_main only",
           measure(UNIT, unit)["app_main_lines"], 10)  # head + 8 statements + closing brace
    expect("no app_main: 0", measure("main/x.cpp", "void f() {\n}\n")["app_main_lines"], 0)
    expect("app_main_lines measured in any unit",
           measure("main/app.cpp", "extern \"C\" void app_main(void) {\n" + "  x();\n" * 400 + "}\n")["app_main_lines"],
           402)
    expect("app_main within 300 on a new path",
           violations({}, {UNIT: {**zero, "app_main_lines": 300}}), [])
    expect("app_main over 300 on a new path",
           violations({}, {UNIT: {**zero, "app_main_lines": 301}}),
           [f"app_main_lines {UNIT}: 301 > hard limit 300 (CS-LAY-01)"])
    expect("app_main moved to another file is still limited",
           len(violations({"app_main_lines": {UNIT: 1007}}, {"main/app.cpp": {**zero, "app_main_lines": 400}})), 1)
    legacy = {"app_main_lines": {UNIT: 1007}}
    expect("legacy app_main may fall", violations(legacy, {UNIT: {**zero, "app_main_lines": 1006}}), [])
    expect("legacy app_main may not grow",
           violations(legacy, {UNIT: {**zero, "app_main_lines": 1008}}),
           [f"app_main_lines {UNIT}: 1008 > baseline 1007"])
    expect("baseline holds only app_main over 300",
           make_baseline({UNIT: {**zero, "app_main_lines": 301}, "main/a.cpp": {**zero, "app_main_lines": 300}})
           ["app_main_lines"], {UNIT: 301})
    expect("update lowers app_main", lowered(legacy, {UNIT: {**zero, "app_main_lines": 900}})["app_main_lines"],
           {UNIT: 900})
    expect("update drops app_main at the limit",
           lowered(legacy, {UNIT: {**zero, "app_main_lines": 300}})["app_main_lines"], {})


def selftest() -> int:
    failures: list[str] = []

    def expect(what: str, got: object, want: object) -> None:
        if got != want:
            failures.append(f"{what}: got {got!r}, want {want!r}")

    # Lexer: comments and literals gone, newlines kept.
    s = strip_code('a = "b // c"; // d\n/* e\n f */ g = \'"\'; h = R"x(i "j" )x";\n1\'000;')
    expect("strip text", s, 'a = ""; \n \n g = \'\'; h = "";\n1000;')
    expect("strip keeps lines", strip_code("/*\n\n*/x").count("\n"), 2)

    m = measure("components/demo/src/demo.cpp", SAMPLE)
    for name, want in SAMPLE_EXPECT.items():
        expect(f"sample {name}", m[name], want)
    expect("ui file lv_", measure("main/about_ui.cpp", SAMPLE)["lv_outside_ui"], 0)

    # Function lengths: brace depth, lambdas inside, ctor init lists, operator=.
    code = (
        _long_fn("a", 59)  # 61 lines: over 60
        + _long_fn("b", 119)  # 121 lines: over 120 and over 60
        + "Foo::Foo(int v) : x_{v}, y_(v) {\n" + "  x();\n" * 70 + "}\n"
        + "Foo &Foo::operator=(const Foo &o) {\n  return *this;\n}\n"
        + "struct S {\n  int g() const {\n" + "    x();\n" * 61 + "  }\n};\n"
        + "static auto lam = [](int) {\n" + "  x();\n" * 80 + "};\n"
    )
    lengths, globals_ = scan_structure(blank_preprocessor(strip_code(code)))
    expect("fn lengths", lengths, [61, 121, 72, 3, 63])
    expect("static_state none in plain functions", scan(blank_preprocessor(strip_code(code))).static_lines, [])
    expect("lambda global", globals_, 1)

    # Casts: real casts counted, calls/sizeof/void/declarations not.
    expect("casts", count_casts("x = (uint8_t)y; z = (char *)p; f(int); sizeof (int) * 2; (void)q; "
                                "void g(void); std::function<void(int)> h; w = (esp_err_t) -1;"), 3)

    # Fragments: splitting main.cpp moves no count.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "main").mkdir()
        whole = "namespace {\nint g = 0;\n}\n" + _long_fn("big", 130) + _long_fn("small", 3)
        (root / "main" / "main.cpp").write_text(whole, encoding="utf-8")
        before = measure_tree(root)
        (root / "main" / "frag_a.inc").write_text("#pragma once\n" + _long_fn("big", 130), encoding="utf-8")
        (root / "main" / "frag_b.inc").write_text(_long_fn("small", 3), encoding="utf-8")
        (root / "main" / "main.cpp").write_text(
            "namespace {\nint g = 0;\n}\n// --- big\n#include \"frag_a.inc\"\n// --- small\n#include \"frag_b.inc\"\n",
            encoding="utf-8",
        )
        after = measure_tree(root)
        expect("split units", sorted(after), [UNIT])
        expect("split invariant", after[UNIT], before[UNIT])

        # Scope: excluded components, test/ and generated/ folders, boot_logo.
        for rel in ("components/ui/x.c", "components/m5stack-tab5/src/x.cpp", "components/c/test/t.cpp",
                    "components/c/generated/g.cpp", "main/boot_logo.c"):
            expect(f"out of scope {rel}", in_scope(rel), False)
        for rel in ("components/joystick/src/joystick.cpp", "main/sample_ui_home.c", "components/c/include/c.hpp"):
            expect(f"in scope {rel}", in_scope(rel), True)

        # Verdicts.
        base = make_baseline(after)
        expect("baseline fn_over_120", base["fn_over_120"], {UNIT: 1})
        expect("baseline lines (under limit) absent", base["lines"], {})
        expect("clean check", violations(base, after), [])
        grown = {UNIT: dict(after[UNIT], fn_over_120=2)}
        expect("legacy grows", len(violations(base, grown)), 1)
        new_ok = dict(after, **{"components/n/src/n.cpp": dict.fromkeys(METRICS, 0)})
        new_ok["components/n/src/n.cpp"].update(lines=900, fn_over_60=2)
        expect("new file within limits", violations(base, new_ok), [])
        new_bad = dict(after, **{"components/n/include/n.hpp": dict.fromkeys(METRICS, 0)})
        new_bad["components/n/include/n.hpp"].update(lines=801, locks=1)
        expect("new file dirty", len(violations(base, new_bad)), 2)

        # update lowers, never raises.
        better = {UNIT: dict(after[UNIT], fn_over_120=0)}
        expect("update lowers", lowered(base, better)["fn_over_120"], {})
        expect("update keeps min", lowered(base, grown)["fn_over_120"], {UNIT: 1})
        bl = root / "baseline.json"
        bl.write_text(dump(base), encoding="utf-8")
        (root / "main" / "frag_c.inc").write_text("std::mutex m;\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):  # the commands' own report is not the verdict here
            expect("update refuses", cmd_update(root, bl, init=False), 1)
            expect("baseline untouched", bl.read_text(encoding="utf-8"), dump(base))
            expect("check fails", cmd_check(root, bl), 1)
            expect("init refuses existing", cmd_update(root, bl, init=True), 1)

    # static_state: non-const statics at block and class scope; namespace scope is mutable_globals.
    statics = (
        "static int ns_static = 0;\n"  # namespace scope: a mutable global, not static_state
        "namespace {\nstatic int anon = 0;\n}\n"
        "void f(int y) {\n"
        "  static int n = 0;\n"  # 1
        "  static const int k = 1;\n  static constexpr int c = 2;\n  static_assert(true);\n"
        "  int x = static_cast<int>(y);\n"
        "  [[maybe_unused]] static bool seen = false;\n"  # 2
        "  static Foo brace{1, 2};\n"  # 3
        "  static int arr[] = {1, 2};\n"  # 4
        "  thread_local int t = 0;\n"  # 5
        "  auto g = [] { static int in_lambda = 0; };\n"  # 6
        "  for (int i = 0; i < 3; ++i) {\n    static\n    int split_line;\n  }\n"  # 7
        "}\n"
        "struct S {\n"
        "  static int shared;\n"  # 8
        "  static void helper();\n  static S &get() { return s_; }\n"
        "  static constexpr int LIMIT = 3;\n  static const char *const NAME;\n"
        "public:\n  static std::function<void()> cb_;\n"  # 9
        "  static inline std::atomic<int> count_{0};\n"  # 10
        "  int member_ = 0;\n"
        "};\n"
        "auto lam = [] { static int in_ns_lambda = 0; };\n"  # 11
    )
    st = scan(blank_preprocessor(strip_code(statics)))
    expect("static_state lines", st.static_lines, [6, 11, 12, 13, 14, 15, 17, 22, 28, 29, 32])
    expect("namespace-scope statics stay mutable_globals", st.globals_, 3)  # ns_static, anon, lam
    expect("static_state counted by measure", measure("components/x/src/x.cpp", statics)["static_state"], 11)
    expect("static_state forbidden in a new path",
           violations({}, {"components/x/src/x.cpp": {**dict.fromkeys(METRICS, 0), "static_state": 1}}),
           ["static_state components/x/src/x.cpp: 1 > 0 (not in baseline: must be clean)"])
    expect("static_state legacy may not grow",
           len(violations({"static_state": {UNIT: 2}}, {UNIT: {**dict.fromkeys(METRICS, 0), "static_state": 3}})), 1)
    expect("static_state legacy may fall",
           violations({"static_state": {UNIT: 2}}, {UNIT: {**dict.fromkeys(METRICS, 0), "static_state": 1}}), [])

    # Fixes found while integrating (2026-10-06).
    zero = {k: 0 for k in METRICS}
    unit = splice_unit('#include "frag_a.inc" // split_main.py\nint z;', {"main/frag_a.inc": "// hdr\nstatic int q;"})
    expect("fragment include with a trailing comment is spliced in place", unit.startswith("// hdr"), True)
    expect("brace-init default argument is not a global",
           measure("x.hpp", "inline bool f(bool c, int x = {}) noexcept { return c; }\n")["mutable_globals"], 0)
    expect("a real braced global still counts", measure("x.cpp", "int g{3};\n")["mutable_globals"], 1)
    expect("locks allowed in the channel helpers",
           violations({}, {"components/fw_core/include/fw_core/port/q.hpp": {**zero, "locks": 2}}), [])
    expect("locks not allowed in the rest of fw_core",
           len(violations({}, {"components/fw_core/src/check.cpp": {**zero, "locks": 1}})), 1)
    expect("locks still forbidden elsewhere",
           len(violations({}, {"components/x/src/q.cpp": {**zero, "locks": 2}})), 1)
    expect("CONFIG_ allowed in a component config header",
           violations({}, {"components/x/include/x/config.hpp": {**zero, "if_config": 1}}), [])
    expect("CONFIG_ still forbidden elsewhere",
           len(violations({}, {"components/x/src/x.cpp": {**zero, "if_config": 1}})), 1)

    _selftest_strict_functions(expect)
    _selftest_app_main(expect)

    for f in failures:
        print(f"selftest FAIL {f}")
    print(f"ratchet selftest: {'FAIL' if failures else 'PASS'} ({len(failures)} failures)")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("command", choices=["check", "update", "selftest"])
    ap.add_argument("--root", type=pathlib.Path, default=ROOT)
    ap.add_argument("--baseline", type=pathlib.Path, default=BASELINE)
    ap.add_argument("--init", action="store_true", help="update: write the first baseline")
    args = ap.parse_args(argv)
    if args.command == "selftest":
        return selftest()
    if args.command == "check":
        return cmd_check(args.root, args.baseline)
    return cmd_update(args.root, args.baseline, args.init)


if __name__ == "__main__":
    sys.exit(main())
