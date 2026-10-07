"""Requirement-to-test matrix (TS-COV-01) and the retired-ID rules, used by tests/run.py.

Where requirements live (TS-COV-01): a table in a component README, one row per requirement,

    | ID | Requirement | Tests |              (other columns, e.g. Rows, may sit between)
    | --- | --- | --- |
    | REQ-FWC-02 | A mailbox returns ... | FWC-001..004, mnc_queue_depth_zero |

The Tests cell names what verifies the requirement: case IDs (FWC-001), ranges (FWC-001..004
or FMT-003..FMT-006, both ends must exist), must-not-compile cases (mnc_x, mnc_message_*) and
free text ("review; L3 not written yet"), which is shown as a note and counts as no test.

A test cites a requirement in three ways, and the matrix collects all three:
  - the README's Tests cell (requirement -> cases);
  - a REQ ID in a TEST_CASE's name or tags, e.g. "[drive][REQ-DRV-03]" (case -> requirement);
  - a manifest entry's `requirements` (app -> requirement, app level only: not a case).
A requirement is covered when at least one existing case or must-not-compile case cites it.

Retired requirements. A superseded requirement is retired by ID, never deleted and never
reused: its row stays, its Requirement cell starts with RETIRED, and it may name its successor:

    | REQ-DRV-07 | RETIRED 2026-10-08, superseded by REQ-DRV-22. <the old text> | – |

Hard errors (`run.py check`, `check()` below):
  - an ID declared twice (a reuse), or a gap in an ID series (a deleted row: retire it);
  - a retired ID cited by a manifest entry or by a TEST_CASE name or tag, or a retired row
    that still lists tests; a successor that is not an active requirement;
  - a manifest entry or TEST_CASE citing an ID that no README declares;
  - a requirement row outside an `| ID | Requirement | ... |` table, or a malformed ID.
Everything else (uncovered requirements, tests with no requirement, README references that
resolve to no test) is reported by `run.py report`, not failed: the report is informational.

Stdlib only. No history is consulted: a row edited in place to mean something else, or the
last row of a series deleted, is left to review.
"""

from __future__ import annotations

import contextlib
import fnmatch
import io
import json
import re
import tempfile
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

SCHEMA = "pace-hmi-fw/reqmatrix/1"
REQ_ID_RE = re.compile(r"\bREQ-[A-Z][A-Z0-9]*-\d{2,}\b")
REQ_ID_FULL_RE = re.compile(r"^REQ-(?P<prefix>[A-Z][A-Z0-9]*)-(?P<num>\d{2,})$")
CASE_ID_RE = re.compile(r"^(?P<prefix>[A-Z][A-Z0-9]*)-(?P<num>\d{3}) \S")
CASE_REF_RE = re.compile(
    r"(?<![A-Za-z0-9-])(?P<p1>[A-Z][A-Z0-9]*)-(?P<n1>\d{3})"
    r"(?:\.\.(?:(?P<p2>[A-Z][A-Z0-9]*)-)?(?P<n2>\d{3}))?(?![0-9])")
MNC_REF_RE = re.compile(r"\bmnc_[A-Za-z0-9_]*\*?")
STR_LIT = r'(?:"(?:[^"\\]|\\.)*"\s*)+'
TEST_CASE_TAGS_RE = re.compile(rf"TEST_CASE\(\s*({STR_LIT})(?:,\s*({STR_LIT}))?\s*\)")
RETIRED_RE = re.compile(r"^RETIRED\b")
SUCCESSOR_RE = re.compile(r"superseded by (REQ-[A-Z][A-Z0-9]*-\d{2,})")
TESTS_HEADERS = {"tests", "test", "verified by"}
SOURCE_SUFFIXES = {".cpp", ".cc", ".c", ".hpp", ".h"}
EMPTY_CELL = {"", "-", "–", "—", "none"}


@dataclass
class Requirement:
    id: str
    component: str
    source: str              # README path:line
    text: str
    retired: bool = False
    successor: str = ""
    tests_cell: str = ""     # the Tests cell as written
    cases: list[str] = field(default_factory=list)       # resolved, from the cell and tags
    mnc: list[str] = field(default_factory=list)          # resolved must-not-compile cases
    entries: list[str] = field(default_factory=list)      # manifest entries citing it
    dangling: list[str] = field(default_factory=list)     # cell references with no test
    notes: str = ""                                        # free text left in the cell


@dataclass
class Case:
    id: str
    name: str
    tags: str
    entry: str
    file: str
    reqs: list[str] = field(default_factory=list)          # REQ IDs in its name or tags


@dataclass
class ManifestRef:
    """What the matrix needs from one manifest entry (run.py's Entry, or a selftest sample)."""
    id: str
    level: str
    path: str
    requirements: list[Any]
    safety: bool
    source: str


# ---------------------------------------------------------------- parsing

def split_row(line: str) -> list[str]:
    """The cells of a Markdown table row; `\\|` is a literal bar."""
    body = line.strip()
    body = body[1:] if body.startswith("|") else body
    body = body[:-1] if body.endswith("|") and not body.endswith("\\|") else body
    return [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", body)]


def parse_readme(text: str, rel: str, component: str) -> tuple[list[Requirement], list[str]]:
    """Requirement rows of one README. A row is any table line whose first cell starts REQ-."""
    reqs: list[Requirement] = []
    errors: list[str] = []
    header: list[str] | None = None
    for lineno, line in enumerate(text.splitlines(), 1):
        if not line.lstrip().startswith("|"):
            header = None
            continue
        cells = split_row(line)
        first = cells[0].strip("`* ")
        if first.lower() == "id":
            header = [c.lower() for c in cells]
            continue
        if not first.startswith("REQ-"):
            continue
        where = f"{rel}:{lineno}"
        if not REQ_ID_FULL_RE.match(first):
            errors.append(f"{where}: malformed requirement ID {first!r} (REQ-<AREA>-<NN>)")
            continue
        if header is None or "requirement" not in header:
            errors.append(f"{where}: {first} is outside an '| ID | Requirement | ... |' table")
            continue
        req_col = header.index("requirement")
        tests_col = next((i for i, h in enumerate(header) if h in TESTS_HEADERS), None)
        req_text = cells[req_col] if req_col < len(cells) else ""
        tests_cell = cells[tests_col] if tests_col is not None and tests_col < len(cells) else ""
        succ = SUCCESSOR_RE.search(req_text)
        reqs.append(Requirement(first, component, where, req_text,
                                retired=bool(RETIRED_RE.match(req_text)),
                                successor=succ.group(1) if succ else "", tests_cell=tests_cell))
    return reqs, errors


def scan_requirements(root: Path) -> tuple[list[Requirement], list[str]]:
    reqs: list[Requirement] = []
    errors: list[str] = []
    for readme in sorted((root / "components").glob("*/README.md")):
        rel = readme.relative_to(root).as_posix()
        r, e = parse_readme(readme.read_text(encoding="utf-8", errors="replace"), rel,
                            readme.parent.name)
        reqs += r
        errors += e
    return reqs, errors


def parse_cases(text: str) -> list[tuple[str, str]]:
    """(name, tags) of every TEST_CASE in one source; adjacent literals are joined."""
    def join(lits: str | None) -> str:
        return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', lits or ""))
    return [(join(m.group(1)), join(m.group(2))) for m in TEST_CASE_TAGS_RE.finditer(text)]


def scan_cases(root: Path, entries: list[ManifestRef]) -> dict[str, Case]:
    """Every case of every L1 app the manifest declares, by case ID (first one wins; run.py
    check already fails a duplicate)."""
    cases: dict[str, Case] = {}
    for e in entries:
        app = root / e.path
        if e.level != "L1" or not e.path or not app.is_dir():
            continue
        for src in sorted(app.rglob("*")):
            if src.suffix not in SOURCE_SUFFIXES or not src.is_file():
                continue
            for name, tags in parse_cases(src.read_text(encoding="utf-8", errors="replace")):
                m = CASE_ID_RE.match(name)
                cid = f"{m.group('prefix')}-{m.group('num')}" if m else ""
                if cid and cid not in cases:
                    cases[cid] = Case(cid, name, tags, e.id, src.relative_to(root).as_posix(),
                                      sorted(set(REQ_ID_RE.findall(f"{name} {tags}"))))
    return cases


def scan_mnc(root: Path) -> list[str]:
    """Must-not-compile case names (file stems), from every component's test/ and tests/."""
    found: set[str] = set()
    for base in [*sorted((root / "components").glob("*/test")), root / "tests"]:
        if base.is_dir():
            found.update(p.stem for p in base.rglob("mnc_*") if p.suffix in SOURCE_SUFFIXES)
    return sorted(found)


# ---------------------------------------------------------------- resolving

def resolve_cell(cell: str, case_ids: set[str], mnc: list[str]) -> tuple[list[str], list[str],
                                                                          list[str], str]:
    """(cases, mnc cases, dangling references, free-text note) of one Tests cell."""
    cases: list[str] = []
    dangling: list[str] = []
    for m in CASE_REF_RE.finditer(cell):
        p1, n1, p2, n2 = m.group("p1"), int(m.group("n1")), m.group("p2"), m.group("n2")
        if n2 is None:
            (cases if f"{p1}-{n1:03d}" in case_ids else dangling).append(f"{p1}-{n1:03d}")
            continue
        lo, hi = f"{p1}-{n1:03d}", f"{p1}-{int(n2):03d}"
        if (p2 and p2 != p1) or int(n2) < n1 or lo not in case_ids or hi not in case_ids:
            dangling.append(m.group(0))
            continue
        cases += [c for c in sorted(case_ids)
                  if c.startswith(p1 + "-") and n1 <= int(c.split("-")[-1]) <= int(n2)]
    found_mnc: list[str] = []
    for m in MNC_REF_RE.finditer(cell):
        hits = [n for n in mnc if fnmatch.fnmatchcase(n, m.group(0))]
        found_mnc += hits
        if not hits:
            dangling.append(m.group(0))
    note = MNC_REF_RE.sub("", CASE_REF_RE.sub("", cell))
    note = note.strip(" ,;:.()") if re.search(r"[A-Za-z]{2}", note) else ""
    return cases, found_mnc, dangling, note


def compress(ids: list[str]) -> str:
    """FWC-001, FWC-002, FWC-003, FWC-005 -> 'FWC-001..FWC-003, FWC-005' (runs of 3 or more)."""
    out: list[str] = []
    run: list[str] = []

    def flush() -> None:
        if run:
            out.append(", ".join(run) if len(run) <= 2 else f"{run[0]}..{run[-1]}")
            run.clear()
    for cid in sorted(set(ids)):
        if run and (cid.rsplit("-", 1)[0] == run[-1].rsplit("-", 1)[0]
                    and cid.rsplit("-", 1)[-1].isdigit()
                    and int(cid.rsplit("-", 1)[-1]) == int(run[-1].rsplit("-", 1)[-1]) + 1):
            run.append(cid)
            continue
        flush()
        run.append(cid)
    flush()
    return ", ".join(out)


def build(root: Path, entries: list[ManifestRef], verdicts: dict[str, str] | None = None,
          run_id: str = "") -> dict[str, Any]:
    """The matrix as plain data (what the JSON holds). `verdicts`: case or entry ID -> the
    verdict of run `run_id`; without a run every case is "not run"."""
    reqs, errors = scan_requirements(root)
    cases = scan_cases(root, entries)
    mnc = scan_mnc(root)
    by_id = {r.id: r for r in reqs}
    for r in reqs:
        r.cases, r.mnc, r.dangling, r.notes = resolve_cell(r.tests_cell, set(cases), mnc)
    for c in cases.values():
        for rid in c.reqs:
            if rid in by_id and c.id not in by_id[rid].cases:
                by_id[rid].cases.append(c.id)
    for e in entries:
        for rid in e.requirements:
            if isinstance(rid, str) and rid in by_id:
                by_id[rid].entries.append(e.id)
    mapped = {cid for r in reqs if not r.retired for cid in r.cases}
    verdicts = verdicts or {}
    rows = []
    for r in reqs:
        r.cases = sorted(set(r.cases))
        status = ("retired" if r.retired else "covered" if r.cases or r.mnc
                  else "app-level only" if r.entries else "uncovered")
        row = asdict(r)
        row.update(status=status, case_verdicts={c: verdicts.get(c, "not run") for c in r.cases})
        rows.append(row)
    no_req = [asdict(c) for cid, c in sorted(cases.items()) if cid not in mapped]
    return {
        "schema": SCHEMA,
        "run_id": run_id,
        "requirements": rows,
        "uncovered": [r["id"] for r in rows if r["status"] in ("uncovered", "app-level only")],
        "cases_without_requirement": no_req,
        "entries_without_requirement": [e.id for e in entries if not e.requirements],
        "dangling": [{"requirement": r.id, "source": r.source, "refs": r.dangling}
                     for r in reqs if r.dangling],
        "counts": {
            "requirements": len(rows),
            "active": sum(not r.retired for r in reqs),
            "retired": sum(r.retired for r in reqs),
            "covered": sum(r["status"] == "covered" for r in rows),
            "app_level_only": sum(r["status"] == "app-level only" for r in rows),
            "uncovered": sum(r["status"] == "uncovered" for r in rows),
            "cases": len(cases), "cases_without_requirement": len(no_req), "mnc": len(mnc),
        },
        "entry_verdicts": {e.id: verdicts.get(e.id, "not run") for e in entries},
        "parse_errors": errors,
    }


# ---------------------------------------------------------------- the hard check

def check(root: Path, entries: list[ManifestRef]) -> list[str]:
    """The errors `run.py check` fails on (see the module doc)."""
    reqs, errors = scan_requirements(root)
    declared: dict[str, Requirement] = {}
    series: dict[str, set[int]] = {}
    for r in reqs:
        if r.id in declared:
            errors.append(f"{r.source}: {r.id} is declared again (first at "
                          f"{declared[r.id].source}); a requirement ID is never reused")
            continue
        declared[r.id] = r
        m = REQ_ID_FULL_RE.match(r.id)  # parse_readme keeps only well-formed IDs
        if m:
            series.setdefault(m.group("prefix"), set()).add(int(m.group("num")))
    for prefix, nums in sorted(series.items()):
        missing = sorted(set(range(1, max(nums) + 1)) - nums)
        if missing:
            errors.append(f"REQ-{prefix}: " + ", ".join(f"REQ-{prefix}-{n:02d}" for n in missing)
                          + " missing from the series; retire a requirement, never delete it")
    for r in declared.values():
        if r.retired and r.tests_cell.strip().lower() not in EMPTY_CELL:
            errors.append(f"{r.source}: retired {r.id} still lists tests "
                          f"({r.tests_cell!r}); move them to its successor")
        if r.successor and (r.successor not in declared or declared[r.successor].retired):
            errors.append(f"{r.source}: {r.id} is superseded by {r.successor}, "
                          f"which is not an active requirement")
    for e in entries:
        for rid in e.requirements:
            where = f"{e.source}:{e.id}"
            if not isinstance(rid, str) or rid not in declared:
                errors.append(f"{where}: requirement {rid!r} is not declared in any component "
                              f"README requirement table")
            elif declared[rid].retired:
                errors.append(f"{where}: cites retired {rid} ({declared[rid].source})")
    for c in scan_cases(root, entries).values():
        for rid in c.reqs:
            if rid not in declared:
                errors.append(f"{c.file}: case {c.id} cites {rid}, which no README declares")
            elif declared[rid].retired:
                errors.append(f"{c.file}: case {c.id} cites retired {rid} "
                              f"({declared[rid].source})")
    return errors


# ---------------------------------------------------------------- output

def _cell(s: str) -> str:
    return s.replace("|", "\\|").replace("\n", " ")


def _verdict_summary(cv: dict[str, str]) -> str:
    if not cv:
        return ""
    counts: dict[str, int] = {}
    for v in cv.values():
        counts[v] = counts.get(v, 0) + 1
    bad = sorted(c for c, v in cv.items() if v not in ("PASS", "not run"))
    return ", ".join(f"{n} {v}" for v, n in sorted(counts.items())) + (
        f" ({compress(bad)})" if bad else "")


def render_md(m: dict[str, Any], header: list[str]) -> str:
    n = m["counts"]
    with_run = bool(m["run_id"])
    lines = ["# Requirement matrix (TS-COV-01)", "", *header, "",
             f"{n['requirements']} requirements ({n['active']} active, {n['retired']} retired): "
             f"{n['covered']} covered by a case, {n['app_level_only']} cited by a manifest entry "
             f"only, {n['uncovered']} with no test. {n['cases']} cases, "
             f"{n['cases_without_requirement']} cite no requirement; {n['mnc']} must-not-compile "
             f"cases. Informational: gaps are listed, not failed (run.py check fails only the "
             f"ID rules).", "", "## Requirements -> tests", "",
             "| Requirement | Component | Status | Cases | Must-not-compile | Manifest |"
             + (" Verdicts |" if with_run else "") + " Notes |",
             "| --- | --- | --- | --- | --- | --- |" + (" --- |" if with_run else "") + " --- |"]
    for r in m["requirements"]:
        note = r["notes"]
        if r["retired"]:
            note = "retired" + (f", superseded by {r['successor']}" if r["successor"] else "")
        if r["dangling"]:
            note = (note + "; " if note else "") + "no such test: " + ", ".join(r["dangling"])
        lines.append(f"| {r['id']} | {r['component']} | {r['status']} | {compress(r['cases'])} | "
                     f"{', '.join(r['mnc'])} | {', '.join(r['entries'])} |"
                     + (f" {_verdict_summary(r['case_verdicts'])} |" if with_run else "")
                     + f" {_cell(note)} |")
    lines += ["", "## Requirements with no test", ""]
    gaps = [r for r in m["requirements"] if r["id"] in m["uncovered"]]
    lines += (["| Requirement | Component | Why | Tests cell |", "| --- | --- | --- | --- |"]
              + [f"| {r['id']} | {r['component']} | "
                 f"{'only a manifest entry cites it, no case' if r['entries'] else 'nothing cites it'}"
                 f" | {_cell(r['tests_cell'])} |" for r in gaps]) if gaps else ["None."]
    lines += ["", "## Tests with no requirement", ""]
    by_entry: dict[str, list[str]] = {}
    for c in m["cases_without_requirement"]:
        by_entry.setdefault(c["entry"], []).append(c["id"])
    lines += (["| Manifest entry | Cases | Count |", "| --- | --- | ---: |"]
              + [f"| {e} | {compress(ids)} | {len(ids)} |" for e, ids in sorted(by_entry.items())]
              ) if by_entry else ["None."]
    if m["entries_without_requirement"]:
        lines += ["", "Manifest entries with `requirements: []`: "
                  + ", ".join(m["entries_without_requirement"]) + "."]
    lines += ["", "## README references that resolve to no test", ""]
    lines += (["| Requirement | Where | References |", "| --- | --- | --- |"]
              + [f"| {d['requirement']} | {d['source']} | {', '.join(d['refs'])} |"
                 for d in m["dangling"]]) if m["dangling"] else ["None."]
    if m["parse_errors"]:
        lines += ["", "## README errors (run.py check fails these)", ""]
        lines += [f"- {e}" for e in m["parse_errors"]]
    return "\n".join(lines) + "\n"


def write(m: dict[str, Any], header: list[str], out_dir: Path) -> tuple[Path, Path, str]:
    out_dir.mkdir(parents=True, exist_ok=True)
    md = render_md(m, header)
    md_path, json_path = out_dir / "requirements.md", out_dir / "requirements.json"
    md_path.write_text(md, encoding="utf-8")
    json_path.write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8")
    return md_path, json_path, md


# ---------------------------------------------------------------- self test

SAMPLE_README = """# demo

| ID | Requirement | Rows | Tests |
| --- | --- | --- | --- |
| REQ-DEM-01 | A thing \\| with a bar | 1 | DEM-001..002, mnc_bad_* |
| REQ-DEM-02 | RETIRED 2026-10-06, superseded by REQ-DEM-03. The old thing | – | – |
| REQ-DEM-03 | The new thing | 2 | DEM-005 |
| REQ-DEM-04 | Only the manifest cites it | – | review; L3 not written yet |
| REQ-DEM-05 | Points at nothing | – | DEM-009, DEM-001..DEM-008, mnc_gone |

Text.
"""
SAMPLE_TEST = """
TEST_CASE("DEM-001 one", "[demo]") {}
TEST_CASE("DEM-002 two"
          " joined", "[demo]") {}
TEST_CASE("DEM-004 cites by tag", "[demo][REQ-DEM-03]") {}
TEST_CASE("DEM-005 five", "[demo]") {}
TEST_CASE("DEM-006 orphan", "[demo]") {}
"""


def _sample_tree(root: Path, readme: str = SAMPLE_README, test: str = SAMPLE_TEST) -> None:
    (root / "components" / "demo" / "test" / "mnc").mkdir(parents=True, exist_ok=True)
    (root / "tests").mkdir(exist_ok=True)
    (root / "components" / "demo" / "README.md").write_text(readme, encoding="utf-8")
    (root / "components" / "demo" / "test" / "test_demo.cpp").write_text(test, encoding="utf-8")
    for n in ("mnc_bad_one", "mnc_bad_two"):
        (root / "components" / "demo" / "test" / "mnc" / f"{n}.cpp").write_text("", "utf-8")


def _entries(reqs: list[Any]) -> list[ManifestRef]:
    return [ManifestRef("L1-DEM", "L1", "components/demo/test", reqs, True, "demo.yaml"),
            ManifestRef("L1-NONE", "L1", "components/none/test", [], False, "none.yaml")]


def selftest() -> int:
    failures: list[str] = []

    def expect(what: str, got: object, want: object) -> None:
        if got != want:
            failures.append(f"{what}: got {got!r}, want {want!r}")

    expect("split escaped bar", split_row("| a \\| b | c |"), ["a | b", "c"])
    expect("tags parsed", parse_cases(SAMPLE_TEST)[1], ("DEM-002 two joined", "[demo]"))
    expect("compress", compress(["A-001", "A-002", "A-003", "A-005", "A-006", "B-001"]),
           "A-001..A-003, A-005, A-006, B-001")
    ids = {"DEM-001", "DEM-002", "DEM-004", "DEM-005"}
    expect("range short form", resolve_cell("DEM-001..004", ids, [])[0],
           ["DEM-001", "DEM-002", "DEM-004"])
    expect("range long form", resolve_cell("DEM-002..DEM-005", ids, [])[0],
           ["DEM-002", "DEM-004", "DEM-005"])
    expect("range end missing", resolve_cell("DEM-001..003", ids, [])[2], ["DEM-001..003"])
    expect("range prefix mismatch", resolve_cell("DEM-001..ABC-004", ids, [])[2],
           ["DEM-001..ABC-004"])
    expect("note kept", resolve_cell("review; L3 not written yet", ids, [])[3],
           "review; L3 not written yet")
    expect("refs are not a note", resolve_cell("DEM-001, DEM-002", ids, [])[3], "")
    expect("mnc glob", resolve_cell("mnc_bad_*", ids, ["mnc_bad_a", "mnc_ok"])[1], ["mnc_bad_a"])
    expect("REQ inside a cell is no case", resolve_cell("see REQ-DEM-01", ids, [])[:3],
           ([], [], []))
    reqs, errs = parse_readme("| REQ-DEM-01 | outside | x |\n", "R.md", "demo")
    expect("row outside a table", (len(reqs), len(errs)), (0, 1))
    reqs, errs = parse_readme("| ID | Requirement | Tests |\n| --- | --- | --- |\n"
                              "| REQ-dem-1 | bad id | x |\n", "R.md", "demo")
    expect("malformed id", (len(reqs), len(errs)), (0, 1))

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        _sample_tree(root)
        good = ["REQ-DEM-01", "REQ-DEM-03", "REQ-DEM-04", "REQ-DEM-05"]
        m = build(root, _entries(good), {"DEM-001": "PASS", "DEM-002": "FAIL"},
                  "sample-run")
        rows = {r["id"]: r for r in m["requirements"]}
        expect("cell range + bar text", (rows["REQ-DEM-01"]["cases"], rows["REQ-DEM-01"]["text"]),
               (["DEM-001", "DEM-002"], "A thing | with a bar"))
        expect("mnc resolved", rows["REQ-DEM-01"]["mnc"], ["mnc_bad_one", "mnc_bad_two"])
        expect("tag + cell", rows["REQ-DEM-03"]["cases"], ["DEM-004", "DEM-005"])
        expect("retired", (rows["REQ-DEM-02"]["status"], rows["REQ-DEM-02"]["successor"]),
               ("retired", "REQ-DEM-03"))
        expect("app level only", rows["REQ-DEM-04"]["status"], "app-level only")
        expect("dangling", rows["REQ-DEM-05"]["dangling"],
               ["DEM-009", "DEM-001..DEM-008", "mnc_gone"])
        expect("uncovered", m["uncovered"], ["REQ-DEM-04", "REQ-DEM-05"])
        expect("orphan cases", [c["id"] for c in m["cases_without_requirement"]], ["DEM-006"])
        expect("entries without", m["entries_without_requirement"], ["L1-NONE"])
        expect("verdicts", rows["REQ-DEM-01"]["case_verdicts"],
               {"DEM-001": "PASS", "DEM-002": "FAIL"})
        md = render_md(m, ["sample"])
        expect("md verdict column", "1 FAIL, 1 PASS (DEM-002)" in md, True)
        expect("md orphans", "| L1-DEM | DEM-006 | 1 |" in md, True)
        expect("json round trip", json.loads(json.dumps(m))["counts"]["retired"], 1)
        expect("clean check", check(root, _entries(good)), [])

        def errors_for(readme: str = SAMPLE_README, test: str = SAMPLE_TEST,
                       reqs: list[Any] | None = None) -> list[str]:
            _sample_tree(root, readme, test)
            return check(root, _entries(good if reqs is None else reqs))
        one = lambda errs, text: len(errs) == 1 and text in errs[0]  # noqa: E731
        expect("manifest cites retired",
               one(errors_for(reqs=[*good, "REQ-DEM-02"]), "cites retired REQ-DEM-02"), True)
        expect("manifest cites undeclared",
               one(errors_for(reqs=[*good, "REQ-DEM-77"]), "not declared"), True)
        expect("case tag cites retired",
               one(errors_for(test=SAMPLE_TEST + 'TEST_CASE("DEM-007 x", "[REQ-DEM-02]") {}\n'),
                   "case DEM-007 cites retired REQ-DEM-02"), True)
        expect("case name cites retired",
               one(errors_for(test=SAMPLE_TEST + 'TEST_CASE("DEM-008 per REQ-DEM-02", "") {}\n'),
                   "cites retired REQ-DEM-02"), True)
        expect("case cites undeclared",
               one(errors_for(test=SAMPLE_TEST + 'TEST_CASE("DEM-009 x", "[REQ-DEM-99]") {}\n'),
                   "no README declares"), True)
        reuse = SAMPLE_README.replace("Text.", "| ID | Requirement | Tests |\n| --- | --- | --- |\n"
                                      "| REQ-DEM-02 | Reused for something else | DEM-006 |\n")
        expect("reused id", one(errors_for(readme=reuse), "declared again"), True)
        gap = errors_for(readme=SAMPLE_README.replace(
            "| REQ-DEM-04 | Only the manifest cites it | – | review; L3 not written yet |\n", ""),
            reqs=["REQ-DEM-01", "REQ-DEM-03", "REQ-DEM-05"])
        expect("deleted row leaves a gap", one(gap, "REQ-DEM-04 missing from the series"), True)
        expect("retired row lists tests", one(errors_for(readme=SAMPLE_README.replace(
            "The old thing | – | – |", "The old thing | – | DEM-006 |")), "still lists tests"), True)
        expect("successor retired", one(errors_for(readme=SAMPLE_README.replace(
            "superseded by REQ-DEM-03", "superseded by REQ-DEM-02")), "not an active"), True)
        expect("successor unknown", one(errors_for(readme=SAMPLE_README.replace(
            "superseded by REQ-DEM-03", "superseded by REQ-DEM-42")), "not an active"), True)
        with contextlib.redirect_stdout(io.StringIO()):
            md_path, json_path, _ = write(build(root, _entries(good)), ["x"], root / "out")
        expect("files written", (md_path.is_file(), json_path.is_file()), (True, True))

    for f in failures:
        print(f"selftest FAIL {f}")
    print(f"reqmatrix selftest: {'PASS' if not failures else f'FAIL ({len(failures)})'}")
    return 0 if not failures else 1
