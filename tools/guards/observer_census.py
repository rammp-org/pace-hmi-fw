#!/usr/bin/env python3
"""G11 observer census: after boot, each subject main registers has the baseline's observer count.

    python tools/guards/observer_census.py fetch --ip 192.168.137.180 --out census.json
    python tools/guards/observer_census.py check --census census.json [--baseline F]
    python tools/guards/observer_census.py baseline --census census.json [--baseline F]
    python tools/guards/observer_census.py selftest

Spec: docs/plans/app-main-shrink.md V7 ("subject init before every bind ... G11, an observer
census: the observer count per subject after boot equals a baseline JSON"). A subject
initialised again after a widget bound to it silently drops that binding; a bind moved to
another fragment can bind twice. Both change a count.

HOST SIDE ONLY. The firmware half (a registry of main's subjects and a remote-UI
`OBSERVERS` command) is parked: see tools/guards/README.md "G11 firmware design". Until it
lands, `fetch` gets `ERR unknown command` and stops with exit 2; no baseline is committed.

Census JSON: {"subjects": [{"name": "drive_speed", "observers": 3}, ...], "complete": true}
(also accepted: {"subjects": {"drive_speed": 3, ...}}), or a log holding the
`OK {"subjects":...}` answer line.
Rules (exit 0 PASS, 1 FAIL, 2 bad input): O1 a baseline subject is missing; O2 a count
differs; O3 a subject the baseline lacks; O4 the firmware's list was cut (complete false).
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import guardlib as gl  # noqa: E402

HERE = Path(__file__).resolve().parent
DEFAULT_BASELINE = HERE / "baselines" / "observers.json"
PORT = 3333
TIMEOUT_S = 5.0
MAX_LINE = 64 * 1024


def parse_census(text: str) -> dict:
    """{"subjects": {name: count}, "complete": bool}."""
    text = text.lstrip("﻿")
    data = None
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        for line in reversed(text.splitlines()):
            i = line.find('{"subjects":')
            if i >= 0:
                data = json.loads(line[i:])
                break
    if not isinstance(data, dict) or "subjects" not in data:
        raise gl.GuardError('no census found (want JSON with "subjects")')
    subs = data["subjects"]
    if isinstance(subs, list):
        counts: dict[str, int] = {}
        for s in subs:
            if s["name"] in counts:
                raise gl.GuardError(f"subject '{s['name']}' is listed twice: names must be unique")
            counts[s["name"]] = int(s["observers"])
        subs = counts
    return {"subjects": {str(k): int(v) for k, v in subs.items()},
            "complete": bool(data.get("complete", True))}


def compare(cur: dict, base: dict) -> list[str]:
    fails: list[str] = []
    if not cur.get("complete", False):
        fails.append("O4 the firmware's subject list was cut short (complete=false)")
    have, want = cur["subjects"], base["subjects"]
    for name, n in sorted(want.items()):
        if name not in have:
            fails.append(f"O1 subject '{name}' is missing (baseline {n} observers)")
        elif have[name] != n:
            fails.append(f"O2 subject '{name}': {have[name]} observers, baseline {n}")
    for name in sorted(set(have) - set(want)):
        fails.append(f"O3 subject '{name}' ({have[name]} observers) is not in the baseline")
    return fails


def fetch(ip: str, port: int) -> dict:
    try:
        with socket.create_connection((ip, port), timeout=TIMEOUT_S) as s:
            s.sendall(b"OBSERVERS\n")
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
        raise gl.GuardError(f"OBSERVERS answered {line[:120]!r} (the firmware half is parked)")
    return parse_census(line[3:])


def read(path: str) -> dict:
    p = gl.local_path(path)
    if not p.is_file():
        raise gl.GuardError(f"no census {p}")
    return parse_census(p.read_text(encoding="utf-8", errors="replace"))


def cmd_fetch(args) -> int:
    census = fetch(args.ip, args.port)
    gl.write_json(Path(args.out), census)
    print(f"wrote {args.out}: {len(census['subjects'])} subjects")
    return 0


def cmd_check(args) -> int:
    f = Path(args.baseline)
    if not f.is_file():
        raise gl.GuardError(f"no baseline {f}: take one with `baseline` from a reviewed boot")
    fails = compare(read(args.census), parse_census(f.read_text(encoding="utf-8")))
    for x in fails:
        print(f"FAIL: {x}")
    print(f"observer_census: {'FAIL' if fails else 'PASS'} ({len(fails)} failures)")
    return 1 if fails else 0


def cmd_baseline(args) -> int:
    census = read(args.census)
    if not census["complete"]:
        raise gl.GuardError("the census was cut short: not a baseline")
    out = {"schema": 1, "about": "G11 observer census baseline (observer_census.py); "
           "take from a reviewed boot only", **census}
    gl.write_json(Path(args.baseline), out)
    print(f"wrote {args.baseline}: {len(census['subjects'])} subjects (review before committing)")
    return 0


# ---------------------------------------------------------------- selftest

SAMPLE = ('I (99) remote_ui: client connected\nOK {"subjects":[{"name":"drive_speed","observers":3},'
          '{"name":"locked","observers":5}],"complete":true}\n')


def t_parse() -> None:
    c = parse_census(SAMPLE)
    gl.expect("log line", c, {"subjects": {"drive_speed": 3, "locked": 5}, "complete": True})
    gl.expect("map form", parse_census('{"subjects":{"a":1}}')["subjects"], {"a": 1})


def t_duplicate_name_invalid() -> None:
    try:
        parse_census('{"subjects":[{"name":"a","observers":1},{"name":"a","observers":2}]}')
    except gl.GuardError:
        return
    raise AssertionError("a repeated subject name was accepted")


def t_equal_passes() -> None:
    c = parse_census(SAMPLE)
    gl.expect("no failures", compare(c, c), [])


def t_rules() -> None:
    base = parse_census(SAMPLE)
    cur = {"subjects": {"drive_speed": 4, "brand_new": 1}, "complete": False}
    got = sorted(f.split(" ", 1)[0] for f in compare(cur, base))
    gl.expect("rules", got, ["O1", "O2", "O3", "O4"])


def cmd_selftest(_args) -> int:
    return gl.run_cases("tools/guards/observer_census.py", [
        ("OBS-001 a census parses from a log line or from JSON", t_parse),
        ("OBS-002 a census naming a subject twice is invalid input", t_duplicate_name_invalid),
        ("OBS-003 a census equal to its baseline passes", t_equal_passes),
        ("OBS-004 missing, changed, new and cut-short each fail", t_rules),
    ])


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("fetch")
    s.add_argument("--ip", required=True)
    s.add_argument("--port", type=int, default=PORT)
    s.add_argument("--out", required=True)
    for name in ("check", "baseline"):
        s = sub.add_parser(name)
        s.add_argument("--census", required=True)
        s.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    sub.add_parser("selftest")
    args = p.parse_args()
    try:
        return {"fetch": cmd_fetch, "check": cmd_check, "baseline": cmd_baseline,
                "selftest": cmd_selftest}[args.cmd](args)
    except gl.GuardError as exc:
        print(f"observer_census: INVALID input: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
