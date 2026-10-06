#!/usr/bin/env python3
"""G10 task-table guard: the tasks the board runs equal the declared table.

    python tools/guards/task_dump.py fetch --ip 192.168.137.180 --out tasks-run.json [--into selftest.json]
    python tools/guards/task_dump.py check --dump tasks-run.json [--baseline F | --topology topology.hpp]
    python tools/guards/task_dump.py baseline --dump tasks-run.json [--baseline F]   (merge observed tasks)
    python tools/guards/task_dump.py selftest

Spec: docs/plans/app-main-shrink.md §5 G10, V11 ("a runtime task dump (name, prio, core,
stack) in the self-test JSON, compared with topology TASKS"), §6 (copy each task config
literally; "Read ADC" runs on espp defaults: prio 0, unpinned).

The dump: the bench build's remote UI answers `TASKS` with one JSON line,
`OK {"tasks":[{"name","prio","core","stack_bytes","stack_free"}...],"complete":true}`
(core -1 = unpinned; stack_bytes = the created size to within 16 B of alignment;
stack_free = the high-water mark). That command is the firmware patch
tools/guards/firmware/remote_ui_tasks.patch (not applied yet: see README). `fetch` reads
it and, with --into, adds it to the self-test JSON as "tasks". `check --dump` takes that
JSON, a fetch output, or a text log holding the `OK {"tasks":...}` line.

Rules (exit 0 PASS, 1 FAIL, 2 bad input):
  T1 every `boot` task of the table is running (an `on_demand` one may be absent);
  T2 a running task has the table's prio, core and stack_bytes (stack: created size within
     `stack_tolerance_bytes` below the declared size); a null field in the table is not
     compared (and is listed as a note);
  T3 a running task the table does not list fails (a new task is an architecture change,
     CORE ask-first);
  T4 `complete` false (the firmware's list was cut) fails.
Names compare on their first `name_max` characters (FreeRTOS keeps configMAX_TASK_NAME_LEN-1).
stack_free below `stack_free_floor_pct` of the table's (when the table has one) is a note:
stack headroom is G8's verdict, not this one's.

--topology reads `TaskRow{...}` / `ForeignTaskRow{...}` from topology.hpp
(dev_ai_refactor_topology) as the table instead of the baseline JSON: names, stack, prio,
core and start for our tasks; foreign tasks by name only.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import guardlib as gl  # noqa: E402

HERE = Path(__file__).resolve().parent
DEFAULT_BASELINE = HERE / "baselines" / "tasks.json"
PORT = 3333
TIMEOUT_S = 5.0
MAX_LINE = 64 * 1024  # bytes read for one answer: a 64-task table is ~6 KB
FIELDS = ("prio", "core", "stack_bytes")


# ---------------------------------------------------------------- reading dumps

def parse_dump(text: str) -> dict:
    """{"tasks": [...], "complete": bool} from a JSON file's text or a log holding the line."""
    text = text.lstrip("﻿")
    try:
        data = json.loads(text)
        if isinstance(data, dict) and isinstance(data.get("tasks"), list):
            return {"tasks": data["tasks"], "complete": bool(data.get("complete", True))}
    except json.JSONDecodeError:
        pass
    for line in reversed(text.splitlines()):  # the newest answer in a log wins
        i = line.find('{"tasks":')
        if i >= 0:
            data = json.loads(line[i:])
            return {"tasks": data["tasks"], "complete": bool(data.get("complete", False))}
    raise gl.GuardError('no task dump found (want JSON with "tasks", or an `OK {"tasks":...}` line)')


def fetch(ip: str, port: int = PORT) -> dict:
    try:
        with socket.create_connection((ip, port), timeout=TIMEOUT_S) as s:
            s.sendall(b"TASKS\n")
            buf = b""
            while b"\n" not in buf and len(buf) < MAX_LINE:
                chunk = s.recv(4096)
                if not chunk:
                    break
                buf += chunk
    except OSError as exc:
        raise gl.GuardError(f"remote UI at {ip}:{port}: {exc}") from exc
    line = buf.split(b"\n", 1)[0].decode("utf-8", "replace").strip()
    if not line.startswith("OK {"):
        raise gl.GuardError(f"TASKS answered {line[:120]!r} (firmware without the TASKS patch?)")
    return parse_dump(line[3:])


# ---------------------------------------------------------------- the table

TASKROW_RE = re.compile(
    r"TaskRow\{\s*Task::\w+\s*,\s*\"([^\"]*)\"\s*,\s*Role::(\w+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,"
    r"\s*(-?\d+)\s*,\s*(?:true|false)\s*,\s*Start::(\w+)\s*\}")
FOREIGN_RE = re.compile(r"ForeignTaskRow\{\s*\"([^\"]*)\"\s*,\s*Start::(\w+)")


def table_from_topology(text: str, name_max: int = 15) -> dict:
    """topology.hpp's TASKS and FOREIGN_TASKS as a baseline-shaped table."""
    code = re.sub(r"//[^\n]*", "", text)
    tasks = []
    for m in TASKROW_RE.finditer(code):
        tasks.append({"name": m[1], "stack_bytes": int(m[3]), "prio": int(m[4]), "core": int(m[5]),
                      "start": "boot" if m[6] == "BOOT" else "on_demand",
                      "source": f"topology.hpp TASKS ({m[2].lower()})"})
    for m in FOREIGN_RE.finditer(code):
        tasks.append({"name": m[1], "prio": None, "core": None, "stack_bytes": None,
                      "start": "boot" if m[2] == "BOOT" else "on_demand",
                      "source": "topology.hpp FOREIGN_TASKS"})
    if not tasks:
        raise gl.GuardError("no TaskRow{...} found in the topology file")
    return {"name_max": name_max, "stack_tolerance_bytes": 16, "stack_free_floor_pct": 80,
            "tasks": tasks}


def load_table(args) -> dict:
    if args.topology:
        return table_from_topology(gl.local_path(args.topology).read_text(encoding="utf-8"))
    f = Path(args.baseline)
    if not f.is_file():
        raise gl.GuardError(f"no baseline {f}")
    return json.loads(f.read_text(encoding="utf-8"))


# ---------------------------------------------------------------- compare

def compare(dump: dict, table: dict) -> tuple[list[str], list[str]]:
    n = int(table.get("name_max", 15))
    tol = int(table.get("stack_tolerance_bytes", 16))
    floor = float(table.get("stack_free_floor_pct", 80))
    fails: list[str] = []
    notes: list[str] = []
    running: dict[str, dict] = {}
    for t in dump["tasks"]:
        key = str(t.get("name", ""))[:n]
        if key in running:
            notes.append(f"two running tasks are named '{key}': compared the first")
            continue
        running[key] = t
    declared: dict[str, dict] = {}
    for d in table.get("tasks", []):
        declared.setdefault(str(d["name"])[:n], d)
    if not dump.get("complete", False):
        fails.append("T4 the firmware's task list was cut short (complete=false)")
    for key, d in declared.items():
        t = running.get(key)
        if t is None:
            if d.get("start", "boot") == "boot":
                fails.append(f"T1 '{key}' is declared ({d.get('source', '?')}) but not running")
            continue
        for fld in FIELDS:
            want, got = d.get(fld), t.get(fld)
            if want is None:
                notes.append(f"'{key}': {fld} not declared (running: {got})")
            elif fld == "stack_bytes":
                if got is None or not want - tol <= int(got) <= want:
                    fails.append(f"T2 '{key}': stack {got} B, declared {want} B (-{tol} B allowed)")
            elif got != want:
                fails.append(f"T2 '{key}': {fld} {got}, declared {want}")
        base_free, free = d.get("stack_free"), t.get("stack_free")
        if base_free and free is not None and free < floor / 100 * base_free:
            notes.append(f"'{key}': stack_free {free} B is under {floor:g}% of {base_free} B (G8)")
    for key, t in running.items():
        if key not in declared:
            fails.append(f"T3 '{key}' is running (prio {t.get('prio')}, core {t.get('core')}, "
                         f"stack {t.get('stack_bytes')} B) but not declared")
    return fails, notes


def merge(dump: dict, table: dict) -> tuple[dict, list[str]]:
    """Add running tasks the table lacks (source "observed"); fill null fields of declared
    ones. A declared value that differs is never overwritten: that is a T2 for review."""
    n = int(table.get("name_max", 15))
    out = json.loads(json.dumps(table))
    by_key = {str(d["name"])[:n]: d for d in out.setdefault("tasks", [])}
    changes: list[str] = []
    for t in dump["tasks"]:
        key = str(t.get("name", ""))[:n]
        d = by_key.get(key)
        if d is None:
            row = {"name": key, **{f: t.get(f) for f in FIELDS}, "stack_free": t.get("stack_free"),
                   "start": "boot", "source": "observed"}
            out["tasks"].append(row)
            by_key[key] = row
            changes.append(f"added '{key}' (observed)")
            continue
        for fld in (*FIELDS, "stack_free"):
            if d.get(fld) is None and t.get(fld) is not None:
                d[fld] = t[fld]
                changes.append(f"'{key}': {fld} = {t[fld]} (observed)")
    return out, changes


# ---------------------------------------------------------------- commands

def cmd_fetch(args) -> int:
    dump = fetch(args.ip, args.port)
    dump["source"] = f"remote_ui TASKS from {args.ip}"
    gl.write_json(Path(args.out), dump)
    print(f"wrote {args.out}: {len(dump['tasks'])} tasks, complete={dump['complete']}")
    if args.into:
        into = Path(args.into)
        data = json.loads(into.read_text(encoding="utf-8")) if into.is_file() else {}
        data["tasks"] = dump["tasks"]
        data["tasks_complete"] = dump["complete"]
        gl.write_json(into, data)
        print(f"added the dump to {into} as \"tasks\"")
    return 0


def read_dump(path: str) -> dict:
    p = gl.local_path(path)
    if not p.is_file():
        raise gl.GuardError(f"no dump {p}")
    data = json.loads(p.read_text(encoding="utf-8")) if p.suffix == ".json" else None
    if isinstance(data, dict) and "tasks_complete" in data:  # a self-test JSON with --into
        return {"tasks": data["tasks"], "complete": bool(data["tasks_complete"])}
    return parse_dump(p.read_text(encoding="utf-8", errors="replace"))


def cmd_check(args) -> int:
    fails, notes = compare(read_dump(args.dump), load_table(args))
    for n in notes:
        print(f"note: {n}")
    for f in fails:
        print(f"FAIL: {f}")
    verdict = "FAIL" if fails else "PASS"
    print(f"task_dump: {verdict} ({len(fails)} failures, {len(notes)} notes)")
    if args.json:
        gl.write_json(Path(args.json), {"verdict": verdict, "failures": fails, "notes": notes})
    return 0 if not fails else 1


def cmd_baseline(args) -> int:
    f = Path(args.baseline)
    table = json.loads(f.read_text(encoding="utf-8")) if f.is_file() else {
        "schema": 1, "name_max": 15, "stack_tolerance_bytes": 16, "stack_free_floor_pct": 80,
        "tasks": []}
    out, changes = merge(read_dump(args.dump), table)
    for c in changes:
        print(c)
    gl.write_json(f, out)
    print(f"wrote {f}: {len(changes)} changes (review the diff before committing)")
    return 0


# ---------------------------------------------------------------- selftest

SAMPLE_TABLE = {
    "name_max": 15, "stack_tolerance_bytes": 16, "stack_free_floor_pct": 80,
    "tasks": [
        {"name": "lv_task", "prio": 20, "core": 1, "stack_bytes": 16384, "stack_free": 5000,
         "start": "boot", "source": "main.cpp"},
        {"name": "Data Display Task", "prio": 10, "core": 1, "stack_bytes": 6144, "start": "boot"},
        {"name": "Read ADC", "prio": 0, "core": -1, "stack_bytes": 4096, "start": "boot"},
        {"name": "selftest", "prio": 3, "core": 0, "stack_bytes": 12288, "start": "on_demand"},
        {"name": "IDLE0", "prio": None, "core": None, "stack_bytes": None, "start": "boot"},
    ]}
SAMPLE_LINE = ('I (1234) remote_ui: whatever\nOK {"tasks":['
               '{"name":"lv_task","prio":20,"core":1,"stack_bytes":16384,"stack_free":4900},'
               '{"name":"Data Display Ta","prio":10,"core":1,"stack_bytes":6136,"stack_free":900},'
               '{"name":"Read ADC","prio":0,"core":-1,"stack_bytes":4096,"stack_free":1200},'
               '{"name":"IDLE0","prio":0,"core":0,"stack_bytes":1536,"stack_free":800}'
               '],"complete":true}\n')
SAMPLE_TOPOLOGY = """
inline constexpr std::array TASKS{
  TaskRow{Task::UI,           "ui",           Role::ISLAND,  16384, 20,  1, false, Start::BOOT},
  TaskRow{Task::SELFTEST,     "selftest",     Role::ISLAND,   8192,  4, -1, true,  Start::ON_DEMAND},
  // TaskRow{Task::OLD, "old", Role::ISLAND, 1, 1, 1, false, Start::BOOT},
};
inline constexpr std::array FOREIGN_TASKS{
  ForeignTaskRow{"rtps_worker", Start::BOOT, Task::RTPS_RX, true},
  ForeignTaskRow{ANY_TASK,      Start::BOOT, Task::LOG_HOOK},
  ForeignTaskRow{"IDLE0",       Start::BOOT},
};
"""


def t_parse() -> None:
    d = parse_dump(SAMPLE_LINE)
    gl.expect("tasks", len(d["tasks"]), 4)
    gl.expect("complete", d["complete"], True)
    j = parse_dump(json.dumps({"tasks": [{"name": "x"}], "complete": False}))
    gl.expect("json form", j, {"tasks": [{"name": "x"}], "complete": False})


def t_pass() -> None:
    fails, notes = compare(parse_dump(SAMPLE_LINE), SAMPLE_TABLE)
    gl.expect("fails", fails, [])
    gl.expect("IDLE0 fields noted", sum("IDLE0" in n for n in notes), 3)


def t_rules_fail() -> None:
    d = parse_dump(SAMPLE_LINE)
    d["tasks"][0]["prio"] = 19                       # T2 prio
    d["tasks"][1]["stack_bytes"] = 6100              # T2 stack beyond tolerance
    d["tasks"].pop(2)                                # T1 Read ADC missing
    d["tasks"].append({"name": "new_task", "prio": 5, "core": -1, "stack_bytes": 4096})  # T3
    d["complete"] = False                            # T4
    fails, _ = compare(d, SAMPLE_TABLE)
    got = sorted(f.split(" ", 1)[0] for f in fails)
    gl.expect("rules", got, ["T1", "T2", "T2", "T3", "T4"])


def t_on_demand_absent_ok() -> None:
    fails, _ = compare(parse_dump(SAMPLE_LINE), SAMPLE_TABLE)
    gl.expect("selftest absent is fine", any("selftest" in f for f in fails), False)


def t_stack_free_is_note() -> None:
    d = parse_dump(SAMPLE_LINE)
    d["tasks"][0]["stack_free"] = 100
    fails, notes = compare(d, SAMPLE_TABLE)
    gl.expect("no failure", fails, [])
    gl.expect("note", any("G8" in n for n in notes), True)


def t_topology() -> None:
    t = table_from_topology(SAMPLE_TOPOLOGY)
    names = [x["name"] for x in t["tasks"]]
    gl.expect("names (comment and ANY_TASK skipped)", names, ["ui", "selftest", "rtps_worker", "IDLE0"])
    gl.expect("ui row", {k: t["tasks"][0][k] for k in ("stack_bytes", "prio", "core", "start")},
              {"stack_bytes": 16384, "prio": 20, "core": 1, "start": "boot"})
    gl.expect("on demand", t["tasks"][1]["start"], "on_demand")


def t_merge() -> None:
    d = parse_dump(SAMPLE_LINE)
    d["tasks"].append({"name": "tiT", "prio": 18, "core": -1, "stack_bytes": 3072, "stack_free": 1000})
    d["tasks"][0]["prio"] = 1  # a declared mismatch is never overwritten
    out, changes = merge(d, SAMPLE_TABLE)
    by = {x["name"]: x for x in out["tasks"]}
    gl.expect("added", by["tiT"]["source"], "observed")
    gl.expect("filled", by["IDLE0"]["stack_bytes"], 1536)
    gl.expect("declared kept", by["lv_task"]["prio"], 20)
    gl.expect("input untouched", SAMPLE_TABLE["tasks"][4]["stack_bytes"], None)
    # tiT added; IDLE0 prio, core, stack_bytes, stack_free; stack_free of Data Display and Read ADC
    gl.expect("changes", len(changes), 7)


def t_committed_baseline_valid() -> None:
    table = json.loads(DEFAULT_BASELINE.read_text(encoding="utf-8"))
    names = [str(t["name"])[: table["name_max"]] for t in table["tasks"]]
    gl.expect("unique names", len(names), len(set(names)))
    for t in table["tasks"]:
        gl.expect(f"{t['name']} start", t["start"] in ("boot", "on_demand"), True)
        gl.expect(f"{t['name']} source", bool(t.get("source")), True)


def cmd_selftest(_args) -> int:
    return gl.run_cases("tools/guards/task_dump.py", [
        ("TSK-001 a dump parses from a log line or from JSON", t_parse),
        ("TSK-002 a dump that matches the table passes and unset fields are notes", t_pass),
        ("TSK-003 missing, mismatched, undeclared and cut-short tasks each fail", t_rules_fail),
        ("TSK-004 an on-demand task may be absent", t_on_demand_absent_ok),
        ("TSK-005 low stack headroom is a note for G8, not a failure", t_stack_free_is_note),
        ("TSK-006 topology TaskRow and ForeignTaskRow lines become the table", t_topology),
        ("TSK-007 merge adds observed tasks and never overwrites a declared value", t_merge),
        ("TSK-008 the committed baseline has unique names, a start and a source each", t_committed_baseline_valid),
    ])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("fetch")
    s.add_argument("--ip", required=True)
    s.add_argument("--port", type=int, default=PORT)
    s.add_argument("--out", required=True)
    s.add_argument("--into", help="also add the dump to this self-test JSON as \"tasks\"")
    s = sub.add_parser("check")
    s.add_argument("--dump", required=True)
    s.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    s.add_argument("--topology", help="compare with topology.hpp TASKS instead of the baseline")
    s.add_argument("--json")
    s = sub.add_parser("baseline")
    s.add_argument("--dump", required=True)
    s.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    sub.add_parser("selftest")
    args = p.parse_args()
    try:
        return {"fetch": cmd_fetch, "check": cmd_check, "baseline": cmd_baseline,
                "selftest": cmd_selftest}[args.cmd](args)
    except gl.GuardError as exc:
        print(f"task_dump: INVALID input: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
