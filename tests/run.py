#!/usr/bin/env python3
"""The one entry point for tests (TS-DEF-03).

    run.py list
    run.py check                                   manifest check (TS-DEF-02), an L0 check
    run.py run <ID>... | --all [--seed N] [--shard i/N] [-j N] [--coverage] [--artifact A]
    run.py report <run-id> | latest

Tests are declared in tests/manifest.d/*.yaml (TS-DEF-01). An L1 entry's `path` is a host
test app with a Makefile (AGENT_BRIEF "host L1 app contract"): `make test` builds and runs it,
and its exit code plus Unity's summary line decide the verdict (TS-PRI-02). An ID may also
be a test-case ID (e.g. JOY-007): the app that declares the case runs that case alone.

Host builds run in WSL (`wsl -e bash -lc ...` from Windows, plain `bash -lc` inside WSL).
spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF `linux` target.

Verdicts (TS-PRI-02): PASS / FAIL from the app; INVALID only when the preflight fails before
any test runs; SKIP only when the manifest declares it. A failure is never retried
(TS-DET-04); `repeat` and `pass_threshold` are declared, not retries.

Each run writes results/<run-id>/: run.json, summary.json, junit.xml, report.txt (by
`report`) and one folder per test with its log. Exit code: 0 all PASS or SKIP, 1 a FAIL,
2 usage or manifest error, 3 INVALID.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import os
import platform
import random
import re
import shlex
import socket
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
MANIFEST_DIR = REPO / "tests" / "manifest.d"
RESULTS_DIR = REPO / "results"
UNITY_DIR = os.environ.get("UNITY_DIR", "/mnt/c/esp/v6.0/esp-idf/components/unity/unity/src")
MAX_MAKE_JOBS = 4  # AGENT_BRIEF rule 2: make -j4 at most on this laptop

LEVELS = {"L0", "L1", "L2", "L3", "L4", "L5"}
ARTIFACTS = {"test", "release", "both"}
RESOURCE_RE = re.compile(r"^(host|board|board:[a-z0-9_-]+|network|power-switch)$")
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
CASE_NAME_RE = re.compile(r"^(?P<id>[A-Z][A-Z0-9]*-\d{3}) \S")
TEST_CASE_RE = re.compile(r'TEST_CASE\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+),')
REQUIRED = {
    "id": str, "title": str, "level": str, "path": str, "requirements": list,
    "resources": list, "timeout_s": (int, float), "expected_s": (int, float),
    "repeat": int, "pass_threshold": int, "safety": bool, "artifact": str,
}
OPTIONAL = {"quarantine": str, "skip": str, "notes": str}

UNITY_SUMMARY_RE = re.compile(r"^(\d+) Tests (\d+) Failures (\d+) Ignored\s*$", re.M)
UNITY_CASE_RE = re.compile(
    r"^(?P<file>[^:\n]+):(?P<line>\d+):(?P<name>[^\n]+?):(?P<res>PASS|FAIL|IGNORE)(?::\s?(?P<msg>[^\n]*))?$",
    re.M)
COVERAGE_RE = re.compile(r"^COVERAGE (\S+) lines (\S+) branches (\S+)\s*$", re.M)
ARTIFACT_RE = re.compile(r"^ARTIFACT ([0-9a-f]{64}) (\S+)\s*$", re.M)
EXIT_TIMEOUT = 124  # coreutils `timeout`


# ---------------------------------------------------------------- shell

IN_WSL = platform.system() == "Linux"


def to_unix(path: Path) -> str:
    """A Windows path as WSL sees it (C:\\w\\x -> /mnt/c/w/x); unchanged inside WSL."""
    if IN_WSL:
        return str(path)
    p = path.resolve()
    drive = p.drive.rstrip(":").lower()
    rest = p.as_posix()[len(p.drive):]
    return f"/mnt/{drive}{rest}"


def bash(cmd: str, timeout: float | None = None) -> tuple[int, str]:
    """Run `cmd` in a WSL login shell; returns (exit code, stdout+stderr)."""
    argv = ["bash", "-lc", cmd] if IN_WSL else ["wsl", "-e", "bash", "-lc", cmd]
    try:
        proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        out = exc.stdout.decode("utf-8", "replace") if exc.stdout else ""
        return EXIT_TIMEOUT, out + "\nrun.py: host timeout expired\n"
    except OSError as exc:
        return 127, f"run.py: cannot start {argv[0]}: {exc}\n"
    return proc.returncode, proc.stdout.decode("utf-8", "replace").replace("\r\n", "\n")


def git(*args: str) -> str:
    try:
        out = subprocess.run(["git", "-c", "gc.auto=0", *args], cwd=REPO, capture_output=True,
                             text=True, check=False)
    except OSError:
        return "unknown"
    return out.stdout.strip() if out.returncode == 0 else "unknown"


def win_to_local(path: str) -> Path:
    """A path written by Windows git (C:/x) as this OS sees it."""
    m = re.match(r"^([A-Za-z]):[/\\](.*)$", path)
    if IN_WSL and m:
        return Path(f"/mnt/{m[1].lower()}/{m[2]}")
    return Path(path)


def head_commit() -> tuple[str, str]:
    """HEAD's (commit, branch). Inside WSL, git cannot follow a worktree whose .git file
    holds a Windows path, so then the refs are read from the files directly."""
    sha, branch = git("rev-parse", "HEAD"), git("rev-parse", "--abbrev-ref", "HEAD")
    if sha != "unknown":
        return sha, branch
    try:
        dot_git = REPO / ".git"
        gitdir = dot_git if dot_git.is_dir() else win_to_local(
            dot_git.read_text(encoding="utf-8").split("gitdir:", 1)[1].strip())
        common = gitdir
        if (gitdir / "commondir").is_file():
            rel = (gitdir / "commondir").read_text(encoding="utf-8").strip()
            common = (gitdir / rel).resolve()
        head = (gitdir / "HEAD").read_text(encoding="utf-8").strip()
        if not head.startswith("ref:"):
            return head, "HEAD"
        ref = head.split(":", 1)[1].strip()
        branch = ref.removeprefix("refs/heads/")
        for base in (gitdir, common):
            if (base / ref).is_file():
                return (base / ref).read_text(encoding="utf-8").strip(), branch
        for line in (common / "packed-refs").read_text(encoding="utf-8").splitlines():
            if line.endswith(" " + ref):
                return line.split()[0], branch
    except (OSError, IndexError):
        pass
    return "unknown", branch


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# ---------------------------------------------------------------- manifest

@dataclass
class Entry:
    data: dict[str, Any]
    source: Path

    @property
    def id(self) -> str:
        return self.data["id"]

    @property
    def app_dir(self) -> Path:
        return REPO / self.data["path"]


def load_manifest() -> tuple[list[Entry], list[str]]:
    entries: list[Entry] = []
    errors: list[str] = []
    files = sorted(MANIFEST_DIR.glob("*.yaml"))
    if not files:
        errors.append(f"no manifest files in {MANIFEST_DIR}")
    for f in files:
        try:
            doc = yaml.safe_load(f.read_text(encoding="utf-8"))
        except yaml.YAMLError as exc:
            errors.append(f"{f.name}: not valid YAML: {exc}")
            continue
        if not isinstance(doc, list):
            errors.append(f"{f.name}: must be a list of entries")
            continue
        for i, item in enumerate(doc):
            if not isinstance(item, dict):
                errors.append(f"{f.name}[{i}]: an entry must be a mapping")
                continue
            entries.append(Entry(item, f))
    return entries, errors


def case_names(app_dir: Path) -> list[tuple[str, Path]]:
    """TEST_CASE names declared in an app's sources (adjacent string literals joined)."""
    names: list[tuple[str, Path]] = []
    if not app_dir.is_dir():
        return names
    for src in sorted(app_dir.rglob("*")):
        if src.suffix not in {".cpp", ".cc", ".c", ".hpp", ".h"} or not src.is_file():
            continue
        text = src.read_text(encoding="utf-8", errors="replace")
        for m in TEST_CASE_RE.finditer(text):
            parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
            names.append(("".join(parts), src))
    return names


def requirement_ids() -> str:
    """The text of every component README, where requirements live (TS-COV-01)."""
    return "\n".join(p.read_text(encoding="utf-8", errors="replace")
                     for p in sorted((REPO / "components").glob("*/README.md")))


def check_entries(entries: list[Entry]) -> list[str]:
    errors: list[str] = []
    seen: dict[str, Path] = {}
    # Case IDs are unique across ALL entries: select() resolves a case ID to one entry, and a
    # duplicate would silently run the later one (final review finding 16).
    case_owner: dict[str, str] = {}
    readmes: str | None = None
    for e in entries:
        where = f"{e.source.name}:{e.data.get('id', '?')}"
        for key, typ in REQUIRED.items():
            if key not in e.data:
                errors.append(f"{where}: missing field '{key}'")
            elif not isinstance(e.data[key], typ) or (typ is int and isinstance(e.data[key], bool)):
                errors.append(f"{where}: field '{key}' has the wrong type")
        for key in e.data:
            if key not in REQUIRED and key not in OPTIONAL:
                errors.append(f"{where}: unknown field '{key}'")
        for key, typ in OPTIONAL.items():
            if key in e.data and not isinstance(e.data[key], typ):
                errors.append(f"{where}: field '{key}' has the wrong type")
        if not isinstance(e.data.get("id"), str):
            continue
        if not ID_RE.match(e.id):
            errors.append(f"{where}: id must match {ID_RE.pattern}")
        if e.id in seen:
            errors.append(f"{where}: duplicate id (also in {seen[e.id].name})")
        seen[e.id] = e.source
        d = e.data
        if d.get("level") not in LEVELS:
            errors.append(f"{where}: level must be one of {sorted(LEVELS)}")
        if d.get("artifact") not in ARTIFACTS:
            errors.append(f"{where}: artifact must be one of {sorted(ARTIFACTS)}")
        for r in d.get("resources", []) or []:
            if not isinstance(r, str) or not RESOURCE_RE.match(r):
                errors.append(f"{where}: unknown resource {r!r}")
        if isinstance(d.get("timeout_s"), (int, float)) and isinstance(d.get("expected_s"), (int, float)):
            if not 0 < d["expected_s"] <= d["timeout_s"]:
                errors.append(f"{where}: need 0 < expected_s <= timeout_s")
        if isinstance(d.get("repeat"), int) and isinstance(d.get("pass_threshold"), int):
            if not 1 <= d["pass_threshold"] <= d["repeat"]:
                errors.append(f"{where}: need 1 <= pass_threshold <= repeat")
        path = d.get("path")
        if isinstance(path, str):
            if not (REPO / path).exists():
                errors.append(f"{where}: path does not exist: {path}")
            elif d.get("level") == "L1" and not (REPO / path / "Makefile").is_file():
                errors.append(f"{where}: an L1 app needs a Makefile in {path}")
            elif d.get("level") == "L1":
                cases = case_names(REPO / path)
                if not cases:
                    errors.append(f"{where}: no TEST_CASE found under {path}")
                case_ids: set[str] = set()
                for name, src in cases:
                    m = CASE_NAME_RE.match(name)
                    if not m:
                        errors.append(f"{where}: case name must be '<ID> <sentence>' "
                                      f"(TS-UNIT-02): {name!r} in {src.name}")
                    elif m.group("id") in case_ids:
                        errors.append(f"{where}: duplicate case id {m.group('id')}")
                    elif m.group("id") in case_owner:
                        errors.append(f"{where}: case id {m.group('id')} is also in "
                                      f"{case_owner[m.group('id')]}")
                    else:
                        case_ids.add(m.group("id"))
                        case_owner[m.group("id")] = e.id
        for req in d.get("requirements", []) or []:
            readmes = requirement_ids() if readmes is None else readmes
            if not isinstance(req, str) or not re.search(rf"\b{re.escape(req)}\b", readmes):
                errors.append(f"{where}: requirement {req!r} is not in any component README")
    return errors


# ---------------------------------------------------------------- run

@dataclass
class Selected:
    key: str            # the ID asked for: an entry id, or a case id
    entry: Entry
    case_filter: str = ""


@dataclass
class Outcome:
    key: str
    entry_id: str
    verdict: str
    reason: str = ""
    duration_s: float = 0.0
    attempts: list[dict[str, Any]] = field(default_factory=list)
    cases: list[dict[str, Any]] = field(default_factory=list)
    coverage: list[dict[str, Any]] = field(default_factory=list)
    artifact_sha256: str = ""
    case_seed: int = 0


def select(entries: list[Entry], ids: list[str], all_: bool) -> list[Selected]:
    by_id = {e.id: e for e in entries}
    if all_:
        return [Selected(e.id, e) for e in entries]
    case_owner: dict[str, Entry] = {}
    for e in entries:
        if e.data.get("level") == "L1":
            for name, _ in case_names(e.app_dir):
                m = CASE_NAME_RE.match(name)
                if m:
                    case_owner[m.group("id")] = e
    out: list[Selected] = []
    for i in ids:
        if i in by_id:
            out.append(Selected(i, by_id[i]))
        elif i in case_owner:
            out.append(Selected(i, case_owner[i], case_filter=i))
        else:
            raise SystemExit(f"run.py: unknown test id {i!r} (see `run.py list`)")
    return out


def preflight() -> tuple[bool, list[dict[str, Any]], dict[str, str]]:
    """Checks the host before any test runs; a failure makes every test INVALID (TS-DET-06)."""
    checks: list[dict[str, Any]] = []
    tools: dict[str, str] = {}
    rc, out = bash("echo ok", timeout=60)
    checks.append({"check": "wsl/bash reachable", "ok": rc == 0 and "ok" in out,
                   "detail": out.strip()[-200:]})
    if not checks[-1]["ok"]:
        return False, checks, tools
    for tool, cmd in [("g++", "g++ --version"), ("gcc", "gcc --version"),
                      ("make", "make --version"), ("gcov", "gcov --version"),
                      ("timeout", "timeout --version"), ("python3", "python3 --version"),
                      ("os", ". /etc/os-release && echo $PRETTY_NAME")]:
        rc, out = bash(cmd, timeout=60)
        first = out.strip().splitlines()[0] if out.strip() else ""
        tools[tool] = first
        if tool != "os":
            checks.append({"check": f"{tool} present", "ok": rc == 0, "detail": first})
    for what, path in [("Unity sources", f"{UNITY_DIR}/unity.c"),
                       ("managed_components copied into the worktree",
                        f"{to_unix(REPO)}/managed_components/espp__logger/include/logger.hpp")]:
        rc, out = bash(f"test -f {shlex.quote(path)}", timeout=60)
        checks.append({"check": what, "ok": rc == 0, "detail": path})
    rc, out = bash('mkdir -p "$HOME/hmi-build" && test -w "$HOME/hmi-build" && echo "$HOME"',
                   timeout=60)
    checks.append({"check": "build root writable", "ok": rc == 0, "detail": out.strip()})
    rc, out = bash(f"sha256sum {shlex.quote(UNITY_DIR)}/unity.c", timeout=60)
    tools["unity.c sha256"] = out.split()[0] if rc == 0 and out.split() else "unknown"
    return all(c["ok"] for c in checks), checks, tools


def judge(rc: int, text: str) -> dict[str, Any]:
    """One attempt's verdict from the app's exit code and Unity's summary line (TS-PRI-02).

    PASS needs all of: exit code 0, a summary line, at least one case, no failure and no
    ignored case (SKIP is declared in the manifest, never decided at run time).
    """
    summ = UNITY_SUMMARY_RE.findall(text)
    tests, fails, ignored = (int(x) for x in summ[-1]) if summ else (0, 0, 0)
    ok = rc == 0 and bool(summ) and tests > 0 and fails == 0 and ignored == 0
    reason = ("" if ok else "timeout" if rc == EXIT_TIMEOUT
              else "no Unity summary line (build failure or crash)" if not summ
              else f"{fails} of {tests} cases failed" if fails
              else f"{ignored} cases ignored at run time" if ignored
              else "no case ran" if tests == 0 else f"exit code {rc}")
    return {"exit_code": rc, "tests": tests, "failures": fails, "ignored": ignored,
            "pass": ok, "reason": reason}


def run_one(sel: Selected, run_id: str, run_dir: Path, args: argparse.Namespace,
            make_jobs: int, case_seed: int) -> Outcome:
    e = sel.entry
    d = e.data
    out = Outcome(sel.key, e.id, "FAIL", case_seed=case_seed)
    test_dir = run_dir / sel.key
    test_dir.mkdir(parents=True, exist_ok=True)
    log = test_dir / "log.txt"
    skip = d.get("skip") or (
        "" if d["artifact"] in (args.artifact, "both")
        else f"declared for artifact '{d['artifact']}', this run is '{args.artifact}'")
    if skip:
        out.verdict, out.reason = "SKIP", skip
        log.write_text(f"SKIP: {skip}\n", encoding="utf-8")
        return out
    if d["level"] != "L1":
        out.reason = f"run.py has no runner for level {d['level']} yet"
        log.write_text(f"FAIL: {out.reason}\n", encoding="utf-8")
        return out

    build_dir = f"$HOME/hmi-build/run/{run_id}/{sel.key}"
    target = "coverage" if args.coverage else "test"
    timeout_s = int(d["timeout_s"])
    make = (f"cd {shlex.quote(to_unix(e.app_dir))} && timeout -k 10 {timeout_s} "
            f"make --no-print-directory -j{make_jobs} {target} "
            f"REPO={shlex.quote(to_unix(REPO))} BUILD_DIR=\"{build_dir}\" "
            f"UNITY_DIR={shlex.quote(UNITY_DIR)} HOST_TEST_SEED={case_seed}"
            + (f" TEST_FILTER={shlex.quote(sel.case_filter)}" if sel.case_filter else ""))
    passes = 0
    started = time.monotonic()
    with log.open("w", encoding="utf-8", newline="\n") as fh:
        for attempt in range(1, int(d["repeat"]) + 1):
            fh.write(f"$ {make}\n")
            fh.flush()
            t0 = time.monotonic()
            rc, text = bash(make, timeout=timeout_s + 60)
            dur = time.monotonic() - t0
            fh.write(text)
            fh.write(f"\n# attempt {attempt}: exit {rc}, {dur:.1f} s\n")
            verdict = judge(rc, text)
            verdict.update({"attempt": attempt, "duration_s": round(dur, 2)})
            out.attempts.append(verdict)
            passes += verdict["pass"]
            if attempt == 1:
                out.cases = [{"name": m["name"], "result": m["res"], "file": m["file"],
                              "line": int(m["line"]), "message": m["msg"] or ""}
                             for m in UNITY_CASE_RE.finditer(text)]
                out.coverage = [{"file": f, "lines_pct": ln, "branches_pct": br}
                                for f, ln, br in COVERAGE_RE.findall(text)]
                arts = ARTIFACT_RE.findall(text)
                out.artifact_sha256 = arts[-1][0] if arts else ""
    out.duration_s = round(time.monotonic() - started, 2)
    if passes >= int(d["pass_threshold"]):
        out.verdict = "PASS"
    else:
        out.reason = "; ".join(a["reason"] for a in out.attempts if not a["pass"])
    if not args.keep_build:
        bash(f'rm -rf "{build_dir}"', timeout=120)
    return out


def write_junit(path: Path, outcomes: list[Outcome]) -> None:
    root = ET.Element("testsuites")
    for o in outcomes:
        suite = ET.SubElement(root, "testsuite", name=o.key, time=f"{o.duration_s:.2f}")
        cases = o.cases or [{"name": o.key, "result": None, "message": ""}]
        n_fail = n_skip = n_err = 0
        for c in cases:
            tc = ET.SubElement(suite, "testcase", classname=o.entry_id, name=c["name"])
            res = c["result"]
            if o.verdict == "INVALID":
                ET.SubElement(tc, "error", message=o.reason)
                n_err += 1
            elif o.verdict == "SKIP" or res == "IGNORE":
                ET.SubElement(tc, "skipped", message=o.reason or c["message"])
                n_skip += 1
            elif res == "FAIL" or (res is None and o.verdict == "FAIL"):
                ET.SubElement(tc, "failure", message=c["message"] or o.reason)
                n_fail += 1
        if o.cases and o.verdict == "FAIL" and n_fail == 0:
            tc = ET.SubElement(suite, "testcase", classname=o.entry_id, name=f"{o.key} app verdict")
            ET.SubElement(tc, "failure", message=o.reason)
            n_fail += 1
        suite.set("tests", str(len(suite)))
        suite.set("failures", str(n_fail))
        suite.set("skipped", str(n_skip))
        suite.set("errors", str(n_err))
    ET.indent(root)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def cmd_run(args: argparse.Namespace) -> int:
    entries, errors = load_manifest()
    errors += check_entries(entries)
    if errors:
        print("\n".join(errors))
        print("run.py: the manifest has errors; run `run.py check`")
        return 2
    if args.shard and not args.ids:
        args.all = True  # a shard is a slice of everything unless IDs are given
    if not args.all and not args.ids:
        print("run.py: give test IDs, --all or --shard i/N")
        return 2
    selected = select(entries, args.ids, args.all)
    selected.sort(key=lambda s: s.key)
    if args.shard:
        m = re.fullmatch(r"(\d+)/(\d+)", args.shard)
        if not m or not 1 <= int(m[1]) <= int(m[2]):
            print("run.py: --shard takes i/N with 1 <= i <= N")
            return 2
        i, n = int(m[1]), int(m[2])
        selected = [s for k, s in enumerate(selected) if k % n == i - 1]
    seed = args.seed if args.seed is not None else random.SystemRandom().randrange(1, 2**31)
    order = list(selected)
    random.Random(seed).shuffle(order)  # TS-DET-02: shuffled with a logged seed

    commit, branch = head_commit()
    started = dt.datetime.now().astimezone()
    run_id = f"{started:%Y%m%dT%H%M%S}-{commit[:7]}-{os.getpid()}"
    run_dir = RESULTS_DIR / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    print(f"run {run_id}  seed {seed}  tests {len(order)}"
          + (f"  shard {args.shard}" if args.shard else ""))
    t0 = time.monotonic()
    ok, checks, tools = preflight()
    for c in checks:
        print(f"  preflight {'ok  ' if c['ok'] else 'FAIL'} {c['check']}: {c['detail']}")
    jobs = max(1, args.jobs)
    make_jobs = max(1, MAX_MAKE_JOBS // jobs)
    outcomes: list[Outcome] = []
    if not ok:
        reason = "preflight failed: " + ", ".join(c["check"] for c in checks if not c["ok"])
        for s in order:
            (run_dir / s.key).mkdir(exist_ok=True)
            (run_dir / s.key / "log.txt").write_text(f"INVALID: {reason}\n", encoding="utf-8")
            outcomes.append(Outcome(s.key, s.entry.id, "INVALID", reason))
    else:
        def seed_for(s: Selected) -> int:
            return random.Random(f"{seed}:{s.key}").randrange(1, 2**31)
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futs = [pool.submit(run_one, s, run_id, run_dir, args, make_jobs, seed_for(s))
                    for s in order]
            for f in futs:  # report in run order
                o = f.result()
                outcomes.append(o)
                extra = f" ({o.reason})" if o.reason else ""
                print(f"  {o.verdict:<7} {o.key:<12} {o.duration_s:7.1f} s{extra}")
                for c in o.coverage:
                    print(f"          COVERAGE {c['file']} lines {c['lines_pct']} "
                          f"branches {c['branches_pct']}")
        if not args.keep_build:
            bash(f'rm -rf "$HOME/hmi-build/run/{run_id}"', timeout=120)
    wall = round(time.monotonic() - t0, 2)
    for o in outcomes:
        (run_dir / o.key / "result.json").write_text(json.dumps(o.__dict__, indent=2),
                                                     encoding="utf-8")
    finished = dt.datetime.now().astimezone()

    counts = {v: sum(o.verdict == v for o in outcomes) for v in ("PASS", "FAIL", "INVALID", "SKIP")}
    overall = ("INVALID" if counts["INVALID"] else "FAIL" if counts["FAIL"] else "PASS")
    status = git("status", "--porcelain")
    run_info = {
        "run_id": run_id, "commit": commit, "branch": branch,
        "dirty": None if status == "unknown" else bool(status),
        "argv": sys.argv[1:], "seed": seed, "shard": args.shard, "jobs": jobs,
        "make_jobs": make_jobs, "coverage": args.coverage, "artifact": args.artifact,
        "started": started.isoformat(timespec="seconds"),
        "finished": finished.isoformat(timespec="seconds"), "wall_s": wall,
        "order": [s.key for s in order],
        "env": {"host": socket.gethostname(), "platform": platform.platform(),
                "python": sys.version.split()[0], "runner": "bash" if IN_WSL else "wsl",
                "unity_dir": UNITY_DIR, "tools": tools},
        "manifests": {str(p.relative_to(REPO)).replace("\\", "/"): sha256_file(p)
                      for p in sorted(MANIFEST_DIR.glob("*.yaml"))},
        "preflight": checks,
        "deviation": "TS-UNIT-01: L1 runs as host-native g++ in WSL, not the IDF linux target",
    }
    (run_dir / "run.json").write_text(json.dumps(run_info, indent=2), encoding="utf-8")
    summary = {"run_id": run_id, "verdict": overall, "counts": counts, "wall_s": wall,
               "tests": [o.__dict__ for o in outcomes]}
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    write_junit(run_dir / "junit.xml", outcomes)
    print(f"{overall}  {counts}  wall {wall:.1f} s  -> {run_dir.relative_to(REPO)}")
    return {"PASS": 0, "FAIL": 1, "INVALID": 3}[overall]


# ---------------------------------------------------------------- list / check / report

def cmd_list(_: argparse.Namespace) -> int:
    entries, errors = load_manifest()
    for e in sorted(entries, key=lambda x: str(x.data.get("id"))):
        d = e.data
        print(f"{d.get('id', '?'):<12} {d.get('level', '?'):<3} "
              f"{'safety ' if d.get('safety') else '       '}{d.get('path', '?'):<32} {d.get('title', '')}")
        if d.get("level") == "L1":
            for name, _ in case_names(e.app_dir):
                print(f"    {name}")
    for err in errors:
        print(f"error: {err}")
    return 2 if errors else 0


def cmd_check(_: argparse.Namespace) -> int:
    entries, errors = load_manifest()
    errors += check_entries(entries)
    for err in errors:
        print(f"error: {err}")
    n_cases = sum(len(case_names(e.app_dir)) for e in entries if e.data.get("level") == "L1")
    print(f"check: {len(entries)} entries, {n_cases} L1 cases, {len(errors)} errors "
          f"-> {'FAIL' if errors else 'PASS'}")
    return 1 if errors else 0


def cmd_report(args: argparse.Namespace) -> int:
    if args.run_id == "latest":
        runs = sorted(p for p in RESULTS_DIR.glob("*") if (p / "summary.json").is_file())
        if not runs:
            print("run.py: no runs in results/")
            return 2
        run_dir = runs[-1]
    else:
        run_dir = RESULTS_DIR / args.run_id
    try:
        summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
        info = json.loads((run_dir / "run.json").read_text(encoding="utf-8"))
    except OSError as exc:
        print(f"run.py: cannot read run {args.run_id}: {exc}")
        return 2
    lines = [f"# Run {info['run_id']}",
             f"commit {info['commit']} ({info['branch']}{', dirty' if info['dirty'] else ''})  "
             f"seed {info['seed']}  shard {info['shard'] or '-'}  wall {info['wall_s']} s",
             f"verdict {summary['verdict']}  {summary['counts']}", "",
             f"| {'Test':<12} | {'Verdict':<7} | {'Cases':>5} | {'Time s':>7} | Reason |",
             f"| {'-' * 12} | {'-' * 7} | {'-' * 5}:| {'-' * 7}:| ------ |"]
    for t in summary["tests"]:
        lines.append(f"| {t['key']:<12} | {t['verdict']:<7} | {len(t['cases']):>5} | "
                     f"{t['duration_s']:>7.1f} | {t['reason']} |")
    cov = [(t["key"], c) for t in summary["tests"] for c in t["coverage"]]
    if cov:
        lines += ["", "Coverage (TS-COV-02: logic >= 80% lines, >= 70% branches):"]
        lines += [f"  {k}: COVERAGE {c['file']} lines {c['lines_pct']} branches {c['branches_pct']}"
                  for k, c in cov]
    entries, _ = load_manifest()
    matrix: dict[str, list[str]] = {}
    verdicts = {t["entry_id"]: t["verdict"] for t in summary["tests"]}
    for e in entries:
        for r in e.data.get("requirements", []) or []:
            matrix.setdefault(r, []).append(f"{e.id}={verdicts.get(e.id, 'not run')}")
    lines += ["", "Requirement -> tests (TS-COV-01):"]
    lines += [f"  {r}: {', '.join(ts)}" for r, ts in sorted(matrix.items())] or ["  (no requirement is cited yet)"]
    text = "\n".join(lines) + "\n"
    (run_dir / "report.txt").write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


SELFTEST_FIXTURE = REPO / "tests" / "host" / "fixtures" / "verdicts"
# (case filter, expected pass, text the reason must contain)
SELFTEST_EXPECT = [
    ("FIX-001", True, ""),
    ("FIX-002", False, "1 of 1 cases failed"),
    ("FIX-003", False, "no Unity summary line"),   # UBSan aborts the app
    ("FIX-004", False, "no Unity summary line"),   # ASan aborts the app
    ("FIX-005", False, "ignored at run time"),
    ("FIX-999", False, "no Unity summary line"),   # a filter that selects nothing
]


def cmd_selftest(_: argparse.Namespace) -> int:
    """Checks the runner's verdict paths against a fixture app (not a manifest test)."""
    ok, checks, _tools = preflight()
    if not ok:
        print("INVALID: preflight failed: " + ", ".join(c["check"] for c in checks if not c["ok"]))
        return 3
    build_dir = f"$HOME/hmi-build/run/selftest-{os.getpid()}"
    failures = 0
    for case, want_pass, want_reason in SELFTEST_EXPECT:
        rc, text = bash(f"cd {shlex.quote(to_unix(SELFTEST_FIXTURE))} && timeout -k 10 300 "
                        f"make --no-print-directory -j{MAX_MAKE_JOBS} test "
                        f"REPO={shlex.quote(to_unix(REPO))} BUILD_DIR=\"{build_dir}\" "
                        f"TEST_FILTER={case}", timeout=360)
        v = judge(rc, text)
        good = v["pass"] == want_pass and want_reason in v["reason"]
        failures += not good
        print(f"  {'ok  ' if good else 'BAD '} {case}: pass={v['pass']} exit={rc} "
              f"reason={v['reason']!r}")
    bash(f'rm -rf "{build_dir}"', timeout=120)
    print(f"selftest: {len(SELFTEST_EXPECT) - failures}/{len(SELFTEST_EXPECT)} verdict paths "
          f"as expected -> {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(prog="run.py", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="list the declared tests and their cases")
    sub.add_parser("check", help="check the manifest (TS-DEF-02)")
    r = sub.add_parser("run", help="run tests by ID, or --all")
    r.add_argument("ids", nargs="*")
    r.add_argument("--all", action="store_true")
    r.add_argument("--seed", type=int, default=None, help="order seed (default: random, logged)")
    r.add_argument("--shard", default=None, help="i/N: every N-th of the sorted IDs from i")
    r.add_argument("-j", "--jobs", type=int, default=1, help="tests run in parallel")
    r.add_argument("--coverage", action="store_true", help="`make coverage` instead of `make test`")
    r.add_argument("--artifact", default="test", choices=["test", "release"])
    r.add_argument("--keep-build", action="store_true", help="keep $HOME/hmi-build/run/<run-id>")
    p = sub.add_parser("report", help="print and save the report of a run")
    p.add_argument("run_id")
    sub.add_parser("selftest", help="check the runner's verdict paths on a fixture app")
    args = ap.parse_args()
    return {"list": cmd_list, "check": cmd_check, "run": cmd_run, "report": cmd_report,
            "selftest": cmd_selftest}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
