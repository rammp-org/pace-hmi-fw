"""B2: grade a boot capture against the baseline boot log.

    python boot_check.py capture.log [--baseline tests/characterisation/baseline-e2047a4/boot-board2.log]

Exit 0 = PASS, 1 = FAIL. Prints a JSON report; `ip` is the board's address
from `Got IP`.

Required markers, each taken from the baseline log with its text, and required
in the order they appear there (the plan lists them in a different order; the
log's own order is what the firmware does):
- `settings/I ... loaded: <values>`              same values as the baseline, or as
  boot-expected.json beside it (owner-approved: Night theme, theme 0, since 2026-10-06)
- `Adding joystick keypad input device...`        the joystick input is up
- `selftest/I ... ready: <n> checks (...)`        same text as the baseline
  (the baseline has no "joystick self test passed" line; these two are the
  nearest boot-time evidence -- see README)
- `joy_cal/I ... loaded /storage/joystick_cal.txt: <mV>`  same mV as the baseline
- `rtps_comms/I ... Network: WiFi`
- `Got IP a.b.c.d`
Forbidden: `Guru Meditation`, `abort()`, a second boot (a second ROM banner
`ESP-ROM:` or a second `Calling app_main()`).
Task watchdog: the baseline trips the task watchdog at ~8 s (IDLE0 starved
while `main` runs on CPU 0). A trip with the same task lists is reported as a
known finding; a trip with different tasks, or more trips than the baseline,
is a FAIL.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

TS = r"\[[\d.]+\]: "
MARKERS = [  # (name, regex, compare the captured group with the baseline's?)
    ("settings_loaded", r"\[settings/I\]" + TS + r"(loaded: .*)$", True),
    ("joystick_input", r"(Adding joystick keypad input device\.\.\.)", True),
    ("selftest_ready", r"\[selftest/I\]" + TS + r"(ready: .*)$", True),
    ("joy_cal_loaded", r"\[joy_cal/I\]" + TS + r"(loaded /storage/joystick_cal\.txt: .*)$", True),
    ("network_wifi", r"\[rtps_comms/I\]" + TS + r"(Network: WiFi)\s*$", True),
    ("got_ip", r"Got IP (\d+\.\d+\.\d+\.\d+)", False),
]
FORBIDDEN = [("guru", r"Guru Meditation"), ("abort", r"abort\(\)")]
SECOND_BOOT = [("rom_banner", r"ESP-ROM:"), ("app_main", r"Calling app_main\(\)")]
# Owner-approved replacements for values in the baseline log, kept beside it with
# the old value (boot-expected.json); the log itself stays the unedited capture.
try:
    OVERRIDES = json.loads((common.BASELINE_DIR / "boot-expected.json").read_text(
        encoding="utf-8"))["overrides"]
except FileNotFoundError:
    OVERRIDES = {}
WDT_TRIP = re.compile(r"task_wdt: Task watchdog got triggered")
WDT_TASK = re.compile(r"task_wdt:\s+- (\S+)|task_wdt: CPU (\d): (\S+)")


def find(lines: list[str], regex: str) -> list[tuple[int, str]]:
    r = re.compile(regex)
    out = []
    for i, line in enumerate(lines):
        m = r.search(line)
        if m:
            out.append((i, m.group(1)))
    return out


def wdt_signature(lines: list[str]) -> tuple[int, list[str]]:
    trips = sum(1 for line in lines if WDT_TRIP.search(line))
    tasks = []
    for line in lines:
        m = WDT_TASK.search(line)
        if m:
            tasks.append(m.group(1) if m.group(1) else f"CPU{m.group(2)}:{m.group(3)}")
    return trips, tasks


def analyse(capture: str, baseline: str) -> dict:
    lines, base = capture.splitlines(), baseline.splitlines()
    problems, findings, markers = [], [], []

    # Expected text and order from the baseline.
    expected = []
    for name, regex, compare in MARKERS:
        hits = find(base, regex)
        if not hits:
            raise SystemExit(f"baseline has no {name} line: the baseline is not usable")
        value = OVERRIDES.get(name, {}).get("value", hits[0][1])
        expected.append((hits[0][0], name, regex, compare, value))
    expected.sort()

    last_index, ip = -1, None
    for _, name, regex, compare, base_value in expected:
        hits = find(lines, regex)
        entry = {"name": name, "found": bool(hits)}
        if not hits:
            problems.append(f"missing marker {name}")
        else:
            index, value = hits[0]
            entry.update(line=index, value=value)
            if compare and value != base_value:
                problems.append(f"{name}: '{value}' != baseline '{base_value}'")
                entry["baseline"] = base_value
            if index < last_index:
                problems.append(f"{name} out of order (line {index} before line {last_index})")
            last_index = max(last_index, index)
            if name == "got_ip":
                ip = value
        markers.append(entry)

    for name, regex in FORBIDDEN:
        hits = find(lines, "(" + regex + ")")
        if hits:
            problems.append(f"forbidden {name} at line {hits[0][0]}: {lines[hits[0][0]][:120]}")
    for name, regex in SECOND_BOOT:
        hits = find(lines, "(" + regex + ")")
        if len(hits) > 1:
            problems.append(f"second boot: {len(hits)} x {name} (lines {[h[0] for h in hits]})")

    got_trips, got_tasks = wdt_signature(lines)
    base_trips, base_tasks = wdt_signature(base)
    if got_trips:
        if got_trips <= base_trips and got_tasks == base_tasks[:len(got_tasks)] and got_tasks:
            findings.append(f"known: task watchdog trip x{got_trips} ({', '.join(got_tasks)}), "
                            "same as the baseline (pre-existing)")
        else:
            problems.append(f"task watchdog: {got_trips} trip(s) with {got_tasks}; baseline "
                            f"{base_trips} with {base_tasks}")
    elif base_trips:
        findings.append("the baseline's task-watchdog trip did not happen this boot")

    return {"verdict": "PASS" if not problems else "FAIL", "ip": ip, "markers": markers,
            "problems": problems, "findings": findings, "lines": len(lines)}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("capture", type=pathlib.Path)
    p.add_argument("--baseline", type=pathlib.Path,
                   default=common.BASELINE_DIR / "boot-board2.log")
    a = p.parse_args()
    report = analyse(a.capture.read_text(encoding="utf-8", errors="replace"),
                     a.baseline.read_text(encoding="utf-8", errors="replace"))
    print(json.dumps(report, indent=2))
    return 0 if report["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
