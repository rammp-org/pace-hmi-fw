"""B5: the as-is drive scenario, against the simulated MCB. No motors exist.

    python scenario_drive.py --ip 192.168.137.180 --out C:\\b\\bench\\results\\x\\drive

Steps, each graded by this script (exit 0 PASS, 1 FAIL):
 1. sim `ok` (IDLE); `home`; hold the stick button 2 s on ONE remote-UI
    connection (BTN 1 ... BTN 0)          -> DriveScreen within 6 s of release
 2. sim `e` (ERROR)                       -> LockedScreen within 3 s
 3. record the XYTwist the sim receives for 3 s while locked
                                          -> x, y and twist all exactly 0
                                             (the button bit is not graded)
 4. sim `ok`, then `x` (refuse ENABLE); hold 2 s again
                                          -> the screen is never DriveScreen for 8 s
Not graded (the remote UI reports only the screen's name): the DRIVE_STOPPED
and NOT_GRANTED banners. Screenshots after steps 2 and 4 are saved for review.
The sim's refusal is switched off again (`x`) before it quits.
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
DRIVE_WITHIN_S = 6.0
LOCKED_WITHIN_S = 3.0
LOCKED_RECORD_S = 3.0
REFUSED_WATCH_S = 8.0


def wait_screen(hmi, want: str, within: float) -> tuple[bool, str, float]:
    t0 = time.monotonic()
    name = ""
    while time.monotonic() - t0 <= within:
        name = hmi.screen()
        if name == want:
            return True, name, time.monotonic() - t0
        time.sleep(0.1)  # poll period
    return False, name, time.monotonic() - t0


def scenario(ip: str, out: pathlib.Path, tree: pathlib.Path) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(tree / "scripts"))
    hmi_ui = importlib.import_module("hmi_ui")
    steps, problems, not_verified = [], [], [
        "DRIVE_STOPPED banner text on LockedScreen (remote UI reports the screen name only)",
        "NOT_GRANTED banner text (same reason)"]

    def step(name: str, ok: bool, detail: str) -> None:
        steps.append({"step": name, "ok": ok, "detail": detail})
        if not ok:
            problems.append(f"{name}: {detail}")
        common.log(f"B5 {name}: {'ok' if ok else 'FAIL'} {detail}")

    with peers.SimChild(ip, tree, out / "sim.log") as sim:
        if not sim.wait_ready():
            return {"verdict": "FAIL", "problems": ["the simulated MCB got no XYTwist in 45 s"],
                    "steps": steps}
        with hmi_ui.Hmi(ip) as hmi:
            # 1. ok -> hold -> Drive
            sim.command("ok")
            hmi.home()
            start = hmi.screen()
            mark = sim.mark()
            hmi.hold(HOLD_MS)  # BTN 1, 2 s, BTN 0 on this one connection
            ok, name, t = wait_screen(hmi, "DriveScreen", DRIVE_WITHIN_S)
            enable = sim.wait_for(r"\[drive\] ENABLE", 0.5, mark)
            step("1 hold->Drive", ok, f"from {start}: {name} after {t:.1f}s; "
                 f"sim saw {'ENABLE' if enable else 'no ENABLE'}")
            hmi_ui.capture(hmi, out / "1-drive.png", False)

            # 2. e -> Locked within 3 s
            sim.command("e")
            ok, name, t = wait_screen(hmi, "LockedScreen", LOCKED_WITHIN_S)
            step("2 e->Locked", ok, f"{name} after {t:.1f}s")
            hmi_ui.capture(hmi, out / "2-locked-after-e.png", False)

            # 3. XYTwist while locked
            if ok:
                sim.send("jstart")
                sim.wait_for(r"^JSTART", 5.0)
                time.sleep(LOCKED_RECORD_S)  # a measurement window, not a wait for a condition
                at = sim.mark()
                sim.send("jstop")
                m = sim.wait_for(r"^XYT (.*)$", 5.0, at)
                xyt = json.loads(m.group(1)) if m else None
                still_locked = hmi.screen() == "LockedScreen"
                if xyt is None:
                    not_verified.append("XYTwist while locked: the sim gave no record")
                    step("3 XYTwist zero while locked", False, "no record from the sim")
                else:
                    zero = (xyt["count"] > 0 and xyt["max_abs_x"] == 0 and xyt["max_abs_y"] == 0
                            and xyt["max_abs_twist"] == 0)
                    step("3 XYTwist zero while locked", zero and still_locked,
                         f"{json.dumps(xyt)}; still locked: {still_locked}")
            else:
                step("3 XYTwist zero while locked", False, "not run: not locked")

            # 4. refuse -> never Drive
            sim.command("ok")
            sim.command("x")
            refusing = sim.wait_for(r"refusing drive requests: True", 3.0)
            mark = sim.mark()
            hmi.hold(HOLD_MS)
            seen = set()
            t0 = time.monotonic()
            while time.monotonic() - t0 < REFUSED_WATCH_S:
                seen.add(hmi.screen())
                time.sleep(0.25)  # poll period over the watch window
            enable = sim.wait_for(r"\[drive\] ENABLE.*refusing", 0.5, mark)
            step("4 refused->not Drive", bool(refusing) and "DriveScreen" not in seen,
                 f"screens seen {sorted(seen)}; sim refusing={bool(refusing)}; "
                 f"sim saw {'a refused ENABLE' if enable else 'no ENABLE'}")
            hmi_ui.capture(hmi, out / "4-after-refused-hold.png", False)
            sim.command("x")  # stop refusing: leave the sim as found
            sim.command("ok")
    return {"verdict": "PASS" if not problems else "FAIL", "steps": steps,
            "problems": problems, "not_verified": not_verified}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ip", required=True)
    p.add_argument("--out", type=pathlib.Path, default=common.BENCH_HOME / f"drive-{common.stamp()}")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO)
    a = p.parse_args()
    strays = common.stray_peers()
    if strays:
        print(json.dumps({"verdict": "INVALID", "processes": strays}, indent=2))
        return 2
    import lease
    with lease.held():
        report = scenario(a.ip, a.out, a.tree)
    print(json.dumps(report, indent=2))
    return 0 if report["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
