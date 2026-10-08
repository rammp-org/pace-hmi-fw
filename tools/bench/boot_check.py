"""B2: grade a boot capture against the baseline boot log.

    python boot_check.py capture.log [--baseline tests/characterisation/baseline-e2047a4/boot-board2.log]
                         [--tree <worktree that built the image>] [--hazard c3,c4]

Exit 0 = PASS, 1 = FAIL. Prints a JSON report; `ip` is the board's address
from `Got IP`.

Required markers, each taken from the baseline log with its text, and required
in the order they appear there (the plan lists them in a different order; the
log's own order is what the firmware does):
- `settings/I ... loaded: <values>`              same values as the baseline, or as
  boot-expected.json beside it (owner-approved: Night theme, theme 0, since 2026-10-06)
- `Adding joystick keypad input device...`        the joystick input is up
- `selftest/I ... ready: <n> checks (...)`        the baseline's text with <n> taken from
  the tree's main/selftest_spec.hpp (data, not a constant: 54 in the baseline, 57 with
  hazard-c4-spec.md's three ctl.* checks, 60 with hazard-c2-spec.md's three joy.* checks).
  A count that boot-expected.json does not declare is a problem; a declared one other than
  the baseline's is a finding (the baseline has no "joystick self test passed" line; these
  two are the nearest boot-time evidence -- see README)
- `joy_cal/I ... loaded /storage/joystick_cal.txt: <mV>`  same mV as the baseline
- `rtps_comms/I ... Network: WiFi`               the bench network's link (common.LINK):
  WiFi on the hotspot, Ethernet with BENCH_NET=lan. On Ethernet the settings line's
  `network` (Connection, N1) is 0 where the baseline has 1: the board's saved choice
- `Got IP a.b.c.d`
Forbidden: `Guru Meditation`, `abort()`, a second boot (a second ROM banner
`ESP-ROM:` or a second `Calling app_main()`).
Task watchdog: the baseline trips the task watchdog at ~8 s (IDLE0 starved
while `main` runs on CPU 0). A trip with the same task lists is reported as a
known finding; a trip with different tasks, or more trips than the baseline,
is a FAIL. When the tree's sdkconfig.defaults turns the idle-task checks off
(hazard-c4-spec.md §4.2), no `task_wdt` line at all is allowed (C4 B2).
Markers added by the hazard fixes (boot-expected.json `added_markers`), required
only for the fixes named with --hazard (run_bench.py passes its own): C3 F4
`POST RESULT PASS ...` and `POST reset reason: <name> (<n>)`.
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
    ("network_link", r"\[rtps_comms/I\]" + TS + r"(Network: \w+)\s*$", True),
    ("got_ip", r"Got IP (\d+\.\d+\.\d+\.\d+)", False),
]
FORBIDDEN = [("guru", r"Guru Meditation"), ("abort", r"abort\(\)")]
SECOND_BOOT = [("rom_banner", r"ESP-ROM:"), ("app_main", r"Calling app_main\(\)")]
# Owner-approved replacements for values in the baseline log, kept beside it with
# the old value (boot-expected.json); the log itself stays the unedited capture.
try:
    _EXPECTED = json.loads((common.BASELINE_DIR / "boot-expected.json").read_text(
        encoding="utf-8"))
except FileNotFoundError:
    _EXPECTED = {}
OVERRIDES = _EXPECTED.get("overrides", {})
# The self-test count marker's declared values (count -> why) and the markers the hazard
# fixes add, each with the fix (--hazard group) that brings it.
SELFTEST_COUNTS = {int(k): v for k, v in
                   _EXPECTED.get("selftest_ready", {}).get("declared", {}).items()}
ADDED_MARKERS = _EXPECTED.get("added_markers", [])
SELFTEST_READY_RE = re.compile(r"^ready: (\d+) checks (\(.*\))$")
IDLE_CHECK_OFF_RE = re.compile(
    r"^(?:CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n|"
    r"# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is not set)\s*$", re.M)
WDT_TRIP = re.compile(r"task_wdt: Task watchdog got triggered")
WDT_TASK = re.compile(r"task_wdt:\s+- (\S+)|task_wdt: CPU (\d): (\S+)")


def for_link(name: str, value: str) -> str:
    """The expected text on this bench's link. The baseline was captured on WiFi
    (Connection setting 1); on Ethernet the setting is 0 and the link line says so."""
    if common.LINK == "WiFi":
        return value
    if name == "network_link":
        return f"Network: {common.LINK}"
    if name == "settings_loaded":
        fixed, n = re.subn(r"\bnetwork 1$", "network 0", value)
        if n != 1:
            raise SystemExit(f"settings_loaded has no trailing 'network 1': {value!r}")
        return fixed
    return value


def selftest_count(tree: pathlib.Path) -> int | None:
    """How many checks the tree's main/selftest_spec.hpp declares (kChecks), parsed the way
    rtps_selftest.py parses it; None when the tree has no such file."""
    path = tree / "main" / "selftest_spec.hpp"
    if not path.exists():
        return None
    sys.path.insert(0, str(common.REPO / "scripts"))
    import rammp_rtps  # the parser only; the file is the tree's
    return len(rammp_rtps.parse_selftest_spec(path.read_text(encoding="utf-8")))


def idle_checks_off(tree: pathlib.Path) -> bool:
    """The tree's sdkconfig.defaults turns the task watchdog's idle-task checks off
    (hazard-c4-spec.md §4.2): then no task_wdt line may appear at boot (C4 B2)."""
    path = tree / "sdkconfig.defaults"
    return path.exists() and IDLE_CHECK_OFF_RE.search(path.read_text(encoding="utf-8")) is not None


def expected_selftest_ready(base_value: str, count: int | None) -> tuple[str, list[str], list[str]]:
    """(expected text, problems, findings) for the selftest_ready marker."""
    m = SELFTEST_READY_RE.match(base_value)
    if count is None or m is None:
        return base_value, [], []
    base_count = int(m.group(1))
    text = f"ready: {count} checks {m.group(2)}"
    if count == base_count:
        return text, [], []
    if count not in SELFTEST_COUNTS:
        return text, [f"selftest_ready: the tree declares {count} checks, a count "
                      f"boot-expected.json does not declare ({sorted(SELFTEST_COUNTS)})"], []
    return text, [], [f"selftest_ready: {count} checks, a declared change from the "
                      f"baseline's {base_count} ({SELFTEST_COUNTS[count]})"]


def added_markers(lines: list[str], groups: set[str]) -> tuple[list[dict], list[str]]:
    """The hazard fixes' boot markers for the fixes in `groups`: (entries, problems)."""
    entries, problems = [], []
    for spec in ADDED_MARKERS:
        if spec["group"] not in groups:
            continue
        hits = find(lines, spec["regex"])
        entry = {"name": spec["name"], "found": bool(hits), "spec": spec["spec"]}
        if not hits:
            problems.append(f"missing marker {spec['name']} ({spec['spec']})")
        else:
            entry.update(line=hits[0][0], value=hits[0][1])
            want = spec.get("value")
            if want is not None and hits[0][1] != want:
                problems.append(f"{spec['name']}: '{hits[0][1]}' != '{want}' ({spec['spec']})")
        entries.append(entry)
    return entries, problems


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


def analyse(capture: str, baseline: str, tree: pathlib.Path | None = None,
            groups: tuple[str, ...] | set[str] = ()) -> dict:
    """Grade a boot capture. `tree` is the worktree that built the image (data for the
    self-test count and the watchdog rule; default this one); `groups` are the hazard fixes
    the image has (run_bench.py --hazard), whose added markers are required."""
    tree = common.REPO if tree is None else pathlib.Path(tree)
    lines, base = capture.splitlines(), baseline.splitlines()
    problems, findings, markers = [], [], []

    # Expected text and order from the baseline.
    expected = []
    for name, regex, compare in MARKERS:
        hits = find(base, regex)
        if not hits:
            raise SystemExit(f"baseline has no {name} line: the baseline is not usable")
        value = OVERRIDES.get(name, {}).get("value", hits[0][1])
        value = for_link(name, value)
        if name == "selftest_ready":
            value, why_not, why = expected_selftest_ready(value, selftest_count(tree))
            problems += why_not
            findings += why
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

    entries, missing = added_markers(lines, set(groups))
    markers += entries
    problems += missing

    got_trips, got_tasks = wdt_signature(lines)
    base_trips, base_tasks = wdt_signature(base)
    wdt_lines = [i for i, line in enumerate(lines) if "task_wdt" in line]
    if idle_checks_off(tree):
        if wdt_lines:
            problems.append(f"task_wdt at lines {wdt_lines[:5]}: the tree turns the idle-task "
                            "checks off, so no task_wdt line is allowed (hazard-c4-spec.md B2)")
    elif got_trips:
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
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO,
                   help="the worktree that built the image (self-test count, watchdog rule)")
    p.add_argument("--hazard", default="",
                   help="hazard fixes in the image (c1,c3,c4,c2): their boot markers are required")
    a = p.parse_args()
    report = analyse(a.capture.read_text(encoding="utf-8", errors="replace"),
                     a.baseline.read_text(encoding="utf-8", errors="replace"), a.tree,
                     {g.strip().lower() for g in a.hazard.split(",") if g.strip()})
    print(json.dumps(report, indent=2))
    return 0 if report["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
