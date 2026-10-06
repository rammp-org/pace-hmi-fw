"""B4b: the bench PIN pad and the Seat Functions cursor walk on the board, navigation only.

    python ui_models_check.py --ip 192.168.137.180 --out C:\\b\\bench\\results\\x\\b4b

Runs with the simulated MCB (Seat Functions needs a live, IDLE MCB) and drives
the board over ONE remote-UI connection (hmi_ui.Hmi). Exit 0 PASS, 1 FAIL.

PIN pad (BenchGateScreen; the pad's model is components/hmi_models pin.hpp):
 P1 the menu's Bench row opens BenchGateScreen; focus is on a key
 P2 four ENTERs on "1" (cursor clamped to the top-left first: UP x4, LEFT x3)
    -> still BenchGateScreen, and the screen below the top bar differs from
       the fresh-load shot (the rejection notice replaces the prompt; dots
       back to none). The diff box is recorded as data.
 P3 1, RIGHT 2, RIGHT 3, DOWN LEFT LEFT 4 -> SettingsScreen (the actuators
    page, setting_page_open(kActuatorsPage)) within 3 s. Nothing on that page
    is pressed; the run leaves with the DRIVE band (home).
Seat Functions grid (hmi_models goldens: SEAT_FUNCTIONS is 3 rows of 2,
arrow keys clamp at the edges, DOWN from the bottom row goes to the burger key):
 S1 the menu's Seat row opens SeatScreen
 S2 UP x3, LEFT x2 -> (0,0); then each move below, with FOCUS's x/y of the
    focused object compared with what the goldens predict:
    RIGHT (0,1) new x, same y; RIGHT clamped (same cell); DOWN (1,1) same x,
    new y; DOWN (2,1); LEFT (2,0); LEFT clamped; UP (1,0); UP (0,0) = start;
    DOWN DOWN (2,0); DOWN -> off the grid to the burger key (focus below the
    grid, centred at x≈360).
    The six cells must be six distinct positions on 2 columns x 3 rows.
Only arrow keys and ENTER on the PIN pad are sent: no seat '+'/'-' or preset
is ever pressed, so no SeatCommand is published by this check.
"""

from __future__ import annotations

import argparse
import importlib
import json
import os
import pathlib
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import peers  # noqa: E402
import walk_check  # noqa: E402

PACE_S = 0.25        # between injected keys: one keypad read each (LVGL reads every 33 ms)
SCREEN_WITHIN_S = 3.0
FOCUS_RE = re.compile(r"keypad:(-?\d+),(-?\d+),(\d+)x(\d+)")


def focus(hmi) -> tuple[int, int] | None:
    """Centre of the keypad-focused object, or None."""
    m = FOCUS_RE.search(hmi.command("FOCUS"))
    if not m:
        return None
    x, y, w, h = (int(v) for v in m.groups())
    return x + w // 2, y + h // 2


def nudge(hmi, key: str) -> tuple[int, int] | None:
    hmi.nudge(key)
    time.sleep(PACE_S)
    return focus(hmi)


def enter(hmi) -> None:
    hmi.key("ENTER")
    time.sleep(PACE_S)


def wait_screen(hmi, want: str, within: float = SCREEN_WITHIN_S) -> str:
    t0, name = time.monotonic(), ""
    while time.monotonic() - t0 <= within:
        name = hmi.screen()
        if name == want:
            return name
        time.sleep(0.1)
    return name


class Checks:
    def __init__(self) -> None:
        self.rows: list[dict] = []

    def add(self, name: str, ok: bool, detail: str) -> bool:
        self.rows.append({"check": name, "ok": ok, "detail": detail})
        common.log(f"B4b {name}: {'ok' if ok else 'FAIL'} {detail}")
        return ok


def pin_checks(hmi, hmi_ui, out: pathlib.Path, c: Checks) -> None:
    hmi.home()
    hmi.go("Bench")
    name = wait_screen(hmi, "BenchGateScreen")
    f0 = focus(hmi)
    if not c.add("P1 Bench opens the PIN gate", name == "BenchGateScreen" and f0 is not None,
                 f"screen {name}, focus {f0}"):
        return
    fresh = hmi_ui.capture(hmi, out / "pin-0-fresh.png", False)
    for key, times in (("UP", 4), ("LEFT", 3)):
        for _ in range(times):
            nudge(hmi, key)
    one = focus(hmi)
    for _ in range(4):
        enter(hmi)
    name = hmi.screen()
    wrong = hmi_ui.capture(hmi, out / "pin-1-after-1111.png", False)
    diff = walk_check.compare_png(wrong, fresh)
    c.add("P2 wrong PIN 1111 rejected with a notice",
          name == "BenchGateScreen" and not diff["equal"] and focus(hmi) == one,
          f"screen {name}; vs fresh load: {diff['detail']}; focus stayed on '1': {focus(hmi) == one}")
    # 1 2 3 4 by the stick: from '1' at (0,0).
    seq = [("ENTER", None), ("RIGHT", "x+"), ("ENTER", None), ("RIGHT", "x+"), ("ENTER", None),
           ("DOWN", "y+"), ("LEFT", "x-"), ("LEFT", "x-"), ("ENTER", None)]
    pos, moves_ok, trace = one, True, []
    for key, expect in seq:
        if key == "ENTER":
            enter(hmi)
            continue
        new = nudge(hmi, key)
        ok = (new is not None and pos is not None and
              {"x+": new[0] > pos[0] and new[1] == pos[1], "x-": new[0] < pos[0] and new[1] == pos[1],
               "y+": new[1] > pos[1]}[expect])
        trace.append(f"{key}->{new}{'' if ok else ' !'}")
        moves_ok &= bool(ok)
        pos = new
    name = wait_screen(hmi, "SettingsScreen")
    hmi_ui.capture(hmi, out / "pin-2-after-1234.png", False)
    c.add("P3 PIN 1234 opens the actuators page", name == "SettingsScreen" and moves_ok,
          f"screen {name}; cursor {', '.join(trace)}")
    hmi.home()


def seat_checks(hmi, hmi_ui, out: pathlib.Path, c: Checks) -> None:
    hmi.home()
    hmi.go("Seat")
    name = wait_screen(hmi, "SeatScreen")
    if not c.add("S1 Seat Functions opens", name == "SeatScreen", f"screen {name}"):
        return
    for key, times in (("UP", 3), ("LEFT", 2)):
        for _ in range(times):
            nudge(hmi, key)
    cells: dict[tuple[int, int], tuple[int, int] | None] = {(0, 0): focus(hmi)}
    hmi_ui.capture(hmi, out / "seat-0-cursor-00.png", False)
    # (key, expected cell afterwards) per the SEAT_FUNCTIONS goldens.
    walk = [("RIGHT", (0, 1)), ("RIGHT", (0, 1)), ("DOWN", (1, 1)), ("DOWN", (2, 1)),
            ("LEFT", (2, 0)), ("LEFT", (2, 0)), ("UP", (1, 0)), ("UP", (0, 0)),
            ("DOWN", (1, 0)), ("DOWN", (2, 0))]
    cell, problems, trace = (0, 0), [], []
    for key, want in walk:
        got = nudge(hmi, key)
        trace.append(f"{key}->{got}")
        if want in cells:
            if got != cells[want]:
                problems.append(f"{key} from {cell}: focus {got}, expected cell {want} at {cells[want]}")
        else:
            cells[want] = got
        cell = want
    pos = {k: v for k, v in cells.items() if v is not None}
    xs = {v[0] for v in pos.values()}
    ys = {v[1] for v in pos.values()}
    shape_ok = len(pos) == 6 and len(set(pos.values())) == 6 and len(xs) == 2 and len(ys) == 3
    cols_ok = shape_ok and all(pos[(r, 0)][0] < pos[(r, 1)][0] for r in range(3))
    rows_ok = shape_ok and all(pos[(r, k)][1] < pos[(r + 1, k)][1] for r in range(2) for k in range(2))
    if not (shape_ok and cols_ok and rows_ok):
        problems.append(f"cells are not a 3x2 grid: {pos}")
    c.add("S2 cursor walk follows the goldens (clamped, 3 rows of 2)", not problems,
          "; ".join(problems) or f"cells {pos}")
    bottom = max(ys) if ys else 0
    off = nudge(hmi, "DOWN")
    hmi_ui.capture(hmi, out / "seat-1-off-bottom.png", False)
    c.add("S3 DOWN from the bottom row goes to the burger key",
          off is not None and off[1] > bottom and abs(off[0] - 360) <= 40,
          f"focus {off} (bottom row y={bottom}; trace {', '.join(trace)})")
    hmi.home()


def check(ip: str, out: pathlib.Path, tree: pathlib.Path) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(tree / "scripts"))
    hmi_ui = importlib.import_module("hmi_ui")
    c = Checks()
    with peers.SimChild(ip, tree, out / "sim.log") as sim:
        if not sim.wait_ready():
            return {"verdict": "FAIL", "problems": ["the simulated MCB got no XYTwist in 45 s"]}
        sim.command("ok")  # IDLE: Seat Functions is open to a live, idle MCB
        with hmi_ui.Hmi(ip) as hmi:
            pin_checks(hmi, hmi_ui, out, c)
            seat_checks(hmi, hmi_ui, out, c)
        seat_cmds = sim.wait_for(r"\[seat\]", 0.5)
    if seat_cmds:
        c.add("no seat command published", False, seat_cmds.string)
    problems = [f"{r['check']}: {r['detail']}" for r in c.rows if not r["ok"]]
    expected = 6
    if len(c.rows) < expected:
        problems.append(f"only {len(c.rows)} of {expected} checks ran")
    return {"verdict": "PASS" if not problems else "FAIL", "checks": c.rows, "problems": problems}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ip", required=True)
    p.add_argument("--out", type=pathlib.Path, default=common.BENCH_HOME / f"b4b-{common.stamp()}")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO)
    a = p.parse_args()
    strays = common.stray_peers()
    if strays:
        print(json.dumps({"verdict": "INVALID", "processes": strays}, indent=2))
        return 2
    import lease
    with lease.held():
        report = check(a.ip, a.out, a.tree)
    print(json.dumps(report, indent=2))
    return 0 if report["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
