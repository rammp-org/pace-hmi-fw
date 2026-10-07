"""Drive timing: one fixed scenario against the simulated MCB, then the DRIVETIME report.

    python scenario_drivetime.py --ip 192.168.137.218 --out C:\\b\\bench\\results\\dt-ref

For an image built with CONFIG_HMI_DEBUG_DRIVE_TIMING (main/drive_timing.hpp). Run the
same command, same arguments, on each image to be compared. No motors exist: the sim
plays the MIB and the stick stays at rest.

  0. DRIVETIME RESET (the report before it is saved as drivetime-before-reset.json)
  per cycle, --cycles times (default 6):
  1. Locked screen, idle, --idle-s (30 s)
  2. sim `ok`; the stick button held 2 s (BTN 1 ... BTN 0)  -> DriveScreen within 6 s
  3. Drive screen, --drive-s (30 s); a third of the way in, one tap on the middle
     drive-profile button (a DriveCommand with the NORMAL profile)
  4. exit: the exit hold (2 s) on odd cycles, the burger key on even ones
                                                            -> LockedScreen within 6 s
     (the burger key opens the menu over the Locked screen once driving stops; a
     second tap on the key closes it)
  5. DRIVETIME, saved as drivetime.json, with run.json beside it

--plain leaves out the profile tap and the burger key: every exit is the exit hold.

A measurement, not a test: nothing here grades the timing. Exit 0 when every step
reached its screen, 1 when one did not (the numbers then cover a different scenario
and are not comparable), 2 when the bench was not ready (INVALID).
"""

from __future__ import annotations

import argparse
import importlib
import json
import os
import pathlib
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import peers  # noqa: E402

HOLD_MS = 2000
SCREEN_WITHIN_S = 6.0
# The middle drive-profile button (ui_ModeAssist, NORMAL): DriveContent sits at the
# body's top (y 195); the button is 207x162 at (257, 675) in it.
PROFILE_TAP = (360, 951)


def wait_screen(hmi, want: str, within: float) -> tuple[bool, str, float]:
    t0 = time.monotonic()
    name = ""
    while time.monotonic() - t0 <= within:
        name = hmi.screen()
        if name == want:
            return True, name, time.monotonic() - t0
        time.sleep(0.1)  # poll period
    return False, name, time.monotonic() - t0


def scenario(ip: str, out: pathlib.Path, tree: pathlib.Path, a: argparse.Namespace) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(tree / "scripts"))
    hmi_ui = importlib.import_module("hmi_ui")
    steps: list[dict] = []

    def step(name: str, ok: bool, detail: str) -> bool:
        steps.append({"step": name, "ok": ok, "detail": detail,
                      "at": round(time.monotonic() - t_start, 1)})
        common.log(f"drivetime {name}: {'ok' if ok else 'FAIL'} {detail}")
        return ok

    t_start = time.monotonic()
    with peers.SimChild(ip, tree, out / "sim.log") as sim:
        if not sim.wait_ready():
            return {"verdict": "INVALID", "problems": ["the simulated MCB got no XYTwist in 45 s"],
                    "steps": steps}
        with hmi_ui.Hmi(ip) as hmi:
            sim.command("ok")
            hmi.home()
            if not step("start on Locked", *wait_screen(hmi, "LockedScreen", SCREEN_WITHIN_S)[:2]):
                return {"verdict": "INVALID", "problems": ["not on the Locked screen at the start"],
                        "steps": steps}
            common.write_json(out / "drivetime-before-reset.json", hmi.drivetime(reset=True))
            ran = True
            for cycle in range(1, a.cycles + 1):
                time.sleep(a.idle_s)  # 1. the idle window being measured, not a wait
                sim.command("ok")
                hmi.hold(HOLD_MS)  # 2. unlock
                ok, name, t = wait_screen(hmi, "DriveScreen", SCREEN_WITHIN_S)
                if not step(f"{cycle} unlock->Drive", ok, f"{name} after {t:.1f}s"):
                    ran = False
                    break
                if a.plain:
                    time.sleep(a.drive_s)  # 3. the driving window being measured
                else:
                    time.sleep(a.drive_s / 3)
                    hmi.tap(*PROFILE_TAP)
                    time.sleep(a.drive_s * 2 / 3)
                burger = not a.plain and cycle % 2 == 0
                if burger:  # 4. exit
                    hmi.open_menu()
                else:
                    hmi.hold(HOLD_MS)
                ok, name, t = wait_screen(hmi, "LockedScreen", SCREEN_WITHIN_S)
                if burger and ok:
                    time.sleep(0.4)  # the menu's 280 ms slide in, plus a little
                    hmi.open_menu()  # the key again: the menu closes
                if not step(f"{cycle} {'burger key' if burger else 'exit hold'}->Locked", ok,
                            f"{name} after {t:.1f}s"):
                    ran = False
                    break
            report = hmi.drivetime()
            common.write_json(out / "drivetime.json", report)
            print(hmi_ui.drivetime_table(report))
    return {"verdict": "PASS" if ran else "FAIL", "steps": steps,
            "problems": [] if ran else ["a step did not reach its screen: see steps"]}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ip", required=True)
    p.add_argument("--out", type=pathlib.Path,
                   default=common.BENCH_HOME / f"drivetime-{common.stamp()}")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO)
    p.add_argument("--cycles", type=int, default=6)
    p.add_argument("--idle-s", type=float, default=30.0)
    p.add_argument("--drive-s", type=float, default=30.0)
    p.add_argument("--plain", action="store_true",
                   help="no profile tap and no burger key: every exit is the exit hold")
    a = p.parse_args()
    strays = common.stray_peers()
    if strays:
        print(json.dumps({"verdict": "INVALID", "processes": strays}, indent=2))
        return 2
    import lease
    with lease.held():
        report = scenario(a.ip, a.out, a.tree, a)
    report["args"] = {k: (str(v) if isinstance(v, pathlib.Path) else v) for k, v in vars(a).items()}
    common.write_json(a.out / "run.json", report)
    print(json.dumps({k: report[k] for k in ("verdict", "problems")}, indent=2))
    return {"PASS": 0, "FAIL": 1}.get(report["verdict"], 2)


if __name__ == "__main__":
    sys.exit(main())
