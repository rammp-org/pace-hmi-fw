"""B5a..B5e: the drive path against the simulated MCB's fault modes. Opt-in, no motors.

    python scenario_hazards.py --ip 192.168.137.218 --out C:\\b\\bench\\results\\x\\hazards
    python scenario_hazards.py --ip ... --steps B5c,B5e
    run_bench.py ... --sim-mode-steps            (adds them after B5; see run_bench.py)

What B5 does not cover (docs/plans/hazard-fixes.md §3 B5, §5 C1). Each step runs
its own simulated MCB (sim_child.py around rtps_mcb_sim.py, with --event-log), so
it starts from IDLE, and drives the board over ONE remote-UI connection with the
existing verbs only (SCREEN, TAP, BTN, SHOT), through ui_client.py (a timeout per
command, one retry for idempotent verbs, every command logged to <step>/remote-ui.jsonl
and summed up in the step's `remote_ui`). Nothing here moves a stick: XYTwist
is only observed, and no chair exists. The sim's JSONL event log is the evidence:
each UI action is preceded by a mark in it, and only what follows the mark counts.

Verdicts (scripts decide, TS-PRI-02): a step is FAIL if any graded check fails,
else PASS; B5e is a characterisation and gives RECORD (its graded checks are only
the set-up and the clean-up). Every `records` entry is data, never graded: today's
behaviour, hazards included, so a fix shows up as a diff (docs/plans/hazard-fixes.md).

B5a exit hold (row 9): ok; hold -> Drive. Mark; hold the stick button 2 s on Drive
    -> graded: LockedScreen within 3 s, the sim got a DISABLE and applied it,
       XYTwist exactly 0 for 3 s after. Recorded: every DriveCommand after the mark.
B5b burger-key exit (row 10): ok; hold -> Drive. Mark; tap the burger key
    -> graded: LockedScreen within 3 s (the menu opens over it), DISABLE applied,
       XYTwist 0. Recorded: DriveCommands after the mark. Leaves with the DRIVE band.
B5c relock on link loss (row 8): ok; hold -> Drive. Mark; sim `p` (no MibStatus)
    -> graded: LockedScreen within MIB_STATUS_TIMEOUT_MS + 2 s, XYTwist 0 for 3 s
       while the link is down.
       Recorded: DriveCommands while the link was down (today none: H5), then `r`
       with the sim still ENABLED: the screens for 5 s and the DriveCommands
       (today the HMI unlocks without asking: H1). Clean-up `ok` -> Locked (graded).
B5d profile click after relock (rows 8, 13): as B5c to Locked; sim `x` (refuse
    ENABLE, so nothing sent from here can grant driving); `r` with the sim ENABLED.
       If the HMI comes back on DriveScreen (today, H1): mark; tap the LOW profile
       button -> graded: a DriveCommand reaches the sim within 2 s; recorded: its
       request and profile (today ENABLE, the stale drive_request: H5). The
       NORMAL button is tapped after it to put the profile back (recorded).
       If it stays locked (the C1 fix), the profile buttons are not reachable:
       recorded, not graded. Clean-up: `x` off, `ok` -> Locked (graded).
B5e ignored and dropped DISABLE (H5/H6), twice: once with `ign 50`, once with
    `drop 50`. ok; hold -> Drive (graded); arm the mode; mark; exit hold; watch 7 s.
       Recorded: every DriveCommand in the window (count, action, gaps; today one
       DISABLE and no re-send: H6), the screens seen (today Drive stays up with the
       exit-refused banner), XYTwist non-zero samples. Then the mode is cleared
       (`ign 0` / `drop 0`), mark, the burger key is tapped (row 10 from
       EXIT_REFUSED), and the DriveCommands and screens for 3 s are recorded.
       Clean-up `ok` -> Locked (graded). Verdict RECORD.
       7 s covers hazard-fixes C1's re-send at 250 ms and its 5 s "MIB did not stop".
Not graded: banner texts (the remote UI reports only the screen name); screenshots
are saved for review. A step that ends with the board not on LockedScreen fails its
clean-up check, so the next step's state is never assumed.
"""

from __future__ import annotations

import argparse
import importlib
import json
import os
import pathlib
import sys
import time
from typing import Callable, NamedTuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import peers  # noqa: E402
import ui_client  # noqa: E402

STEPS = ["B5a", "B5b", "B5c", "B5d", "B5e"]

HOLD_MS = 2000            # the unlock and the exit hold: 500 ms grace + 1000 ms fill, plus margin
DRIVE_WITHIN_S = 6.0      # as B5
LOCKED_WITHIN_S = 3.0     # as B5
COMMAND_WITHIN_S = 2.0    # a DriveCommand reaches the sim after the UI action
XYT_RECORD_S = 3.0        # XYTwist measurement window, as B5
RESUME_WATCH_S = 5.0      # unlock advance 1 s + fade + ticks, with margin
RECORD_WINDOW_S = 7.0     # C1: re-send every 250 ms, "MIB did not stop" at 5 s
SCREEN_POLL_S = 0.25
FAULT_COUNT = 50          # > 7 s x 4 re-sends/s: every DISABLE in the window meets the mode

# DriveScreen's profile buttons (components/ui/screens/ui_DriveScreen.c): DriveBody at
# y 195, the buttons 207x162 at y 675 inside it; ui_ModeAssist (NORMAL) x 257,
# ui_ModeAuto (LOW) x 484 (main.cpp binds Manual/Assist/Auto to HIGH/NORMAL/LOW).
PROFILE_Y = 195 + 675 + 162 // 2
PROFILE_NORMAL = (257 + 207 // 2, PROFILE_Y)
PROFILE_LOW = (484 + 207 // 2, PROFILE_Y)


class Ctx(NamedTuple):
    spec: object                                   # scripts/rammp_rtps.py of --tree
    shot: Callable[[pathlib.Path], pathlib.Path]   # a screenshot of the panel


class Step:
    """Graded checks and recorded data for one step."""

    def __init__(self, name: str, out: pathlib.Path):
        self.name = name
        self.out = out
        self.checks: list[dict] = []
        self.problems: list[str] = []
        self.records: dict = {}
        self.not_verified = ["banner texts (the remote UI reports the screen name only)"]

    def check(self, what: str, ok: bool, detail: str) -> bool:
        self.checks.append({"check": what, "ok": ok, "detail": detail})
        if not ok:
            self.problems.append(f"{what}: {detail}")
        common.log(f"{self.name} {what}: {'ok' if ok else 'FAIL'} {detail}")
        return ok

    def record(self, key: str, value: object) -> None:
        self.records[key] = value
        common.log(f"{self.name} record {key}: {json.dumps(value, default=str)[:300]}")

    def result(self, characterisation: bool = False) -> dict:
        verdict = "FAIL" if self.problems else ("RECORD" if characterisation else "PASS")
        return {"verdict": verdict, "checks": self.checks, "problems": self.problems,
                "records": self.records, "not_verified": self.not_verified}


# ---------------------------------------------------------------- helpers


def wait_screen(hmi, want: str, within: float) -> tuple[bool, str, float]:
    t0 = time.monotonic()
    name = ""
    while time.monotonic() - t0 <= within:
        name = hmi.screen()
        if name == want:
            return True, name, time.monotonic() - t0
        time.sleep(0.1)  # poll period
    return False, name, time.monotonic() - t0


def watch_screens(hmi, seconds: float) -> list[list]:
    """[t, name] at each change of screen over the window."""
    t0 = time.monotonic()
    seen: list[list] = []
    while time.monotonic() - t0 < seconds:
        name = hmi.screen()
        if not seen or seen[-1][1] != name:
            seen.append([round(time.monotonic() - t0, 2), name])
        time.sleep(SCREEN_POLL_S)  # poll period over a measurement window
    return seen


def drive_commands(events: list[dict]) -> list[dict]:
    """The DriveCommands in a slice of the event log, with their time from the first."""
    rows = [e for e in events if e.get("ev") == "drive_command"]
    t0 = rows[0]["mono"] if rows else 0.0
    return [{"request": e["request"], "profile": e["profile"], "action": e["action"],
             "state_after": e["state_after"], "dt_s": round(e["mono"] - t0, 3)} for e in rows]


def until_mark(events: list[dict], label: str) -> list[dict]:
    """The records before mark `label` (all of them if it is not there)."""
    for i, e in enumerate(events):
        if e.get("ev") == "mark" and e.get("label") == label:
            return events[:i]
    return events


def is_disable(e: dict) -> bool:
    return e.get("ev") == "drive_command" and e.get("request") == "DISABLE"


def is_drive_command(e: dict) -> bool:
    return e.get("ev") == "drive_command"


def nonzero_xytwist(events: list[dict]) -> int:
    return sum(1 for e in events if e.get("ev") == "xytwist"
               and any(e.get(k) != 0 for k in ("x", "y", "twist")))


def xytwist_window(sim, seconds: float) -> dict | None:
    """sim_child's record of every XYTwist over the window (as B5 step 3)."""
    sim.send("jstart")
    sim.wait_for(r"^JSTART", 5.0)
    time.sleep(seconds)  # a measurement window, not a wait for a condition
    at = sim.mark()
    sim.send("jstop")
    m = sim.wait_for(r"^XYT (.*)$", 5.0, at)
    return json.loads(m.group(1)) if m else None


def check_xytwist_zero(st: Step, sim, hmi, what: str) -> None:
    xyt = xytwist_window(sim, XYT_RECORD_S)
    still_locked = hmi.screen() == "LockedScreen"
    if xyt is None:
        st.check(what, False, "no XYTwist record from the sim")
        return
    zero = (xyt["count"] > 0 and xyt["max_abs_x"] == 0 and xyt["max_abs_y"] == 0
            and xyt["max_abs_twist"] == 0)
    st.check(what, zero and still_locked, f"{json.dumps(xyt)}; still locked: {still_locked}")


def set_refuse_drive(sim, want: bool) -> bool:
    """`x` toggles; send it until the sim says it is in the wanted state (2 tries)."""
    for _ in range(2):
        m = sim.reply("x", r"refusing drive requests: (True|False)")
        if m and (m.group(1) == "True") == want:
            return True
    return False


def to_drive(st: Step, sim, hmi, label: str) -> bool:
    """ok; home; hold -> DriveScreen (graded). The sim logs the ENABLE after `label`."""
    sim.command("ok")
    hmi.home()
    start = hmi.screen()
    sim.event_mark(label)
    hmi.hold(HOLD_MS)  # BTN 1, 2 s, BTN 0 on this one connection
    ok, name, t = wait_screen(hmi, "DriveScreen", DRIVE_WITHIN_S)
    enable = sim.wait_event(lambda e: is_drive_command(e) and e["request"] == "ENABLE",
                            0.5, label)
    return st.check("set-up: hold -> Drive", ok,
                    f"from {start}: {name} after {t:.1f}s; sim saw "
                    f"{'ENABLE ' + enable['action'] if enable else 'no ENABLE'}")


def clean_up(st: Step, sim, hmi) -> None:
    """Every fault mode off, the sim IDLE and publishing, the board on LockedScreen."""
    sim.reply("ign 0", r"ignoring the next 0")
    sim.reply("drop 0", r"dropping the next 0")
    sim.command("r")
    sim.command("ok")
    ok, name, t = wait_screen(hmi, "LockedScreen", LOCKED_WITHIN_S)
    hmi.home()  # closes a menu left open over Locked
    ok2, name2, _ = wait_screen(hmi, "LockedScreen", LOCKED_WITHIN_S)
    st.check("clean-up: sim IDLE -> LockedScreen", ok and ok2,
             f"{name} after {t:.1f}s; after the DRIVE band: {name2}")


def expect_exit(st: Step, sim, hmi, label: str, how: str) -> None:
    """After an exit action: LockedScreen within 3 s and a DISABLE the sim applied."""
    ok, name, t = wait_screen(hmi, "LockedScreen", LOCKED_WITHIN_S)
    st.check(f"{how} -> Locked", ok, f"{name} after {t:.1f}s")
    dis = sim.wait_event(is_disable, COMMAND_WITHIN_S, label)
    st.check(f"{how} sends DISABLE, applied", dis is not None and dis["action"] == "applied",
             json.dumps(dis) if dis else "no DISABLE in the sim log")


# ---------------------------------------------------------------- the steps


def b5a(st: Step, sim, hmi, ctx: Ctx) -> dict:
    if to_drive(st, sim, hmi, "b5a-unlock"):
        sim.event_mark("b5a-exit")
        hmi.hold(HOLD_MS)  # on DriveScreen: the exit hold
        expect_exit(st, sim, hmi, "b5a-exit", "exit hold")
        ctx.shot(st.out / "b5a-after-exit-hold.png")
        check_xytwist_zero(st, sim, hmi, "XYTwist zero after the exit hold")
        st.record("drive_commands_after_exit_hold", drive_commands(sim.events("b5a-exit")))
    clean_up(st, sim, hmi)
    return st.result()


def b5b(st: Step, sim, hmi, ctx: Ctx) -> dict:
    if to_drive(st, sim, hmi, "b5b-unlock"):
        sim.event_mark("b5b-key")
        hmi.open_menu()  # the burger key on DriveScreen asks to stop
        expect_exit(st, sim, hmi, "b5b-key", "burger key")
        ctx.shot(st.out / "b5b-menu-over-locked.png")
        hmi.home()
        check_xytwist_zero(st, sim, hmi, "XYTwist zero after the burger-key exit")
        st.record("drive_commands_after_burger_key", drive_commands(sim.events("b5b-key")))
    clean_up(st, sim, hmi)
    return st.result()


def relock_by_link_loss(st: Step, sim, hmi, ctx: Ctx, label: str) -> bool:
    """Drive -> sim `p` -> LockedScreen within the HMI's MibStatus timeout + 2 s."""
    within = ctx.spec.MIB_STATUS_TIMEOUT_MS / 1000.0 + 2.0
    sim.event_mark(label)
    sim.command("p")
    ok, name, t = wait_screen(hmi, "LockedScreen", within)
    return st.check("link loss (sim p) -> Locked", ok,
                    f"{name} after {t:.1f}s (limit {within:.1f}s)")


def b5c(st: Step, sim, hmi, ctx: Ctx) -> dict:
    if to_drive(st, sim, hmi, "b5c-unlock") and relock_by_link_loss(st, sim, hmi, ctx,
                                                                     "b5c-pause"):
        ctx.shot(st.out / "b5c-locked-link-lost.png")
        check_xytwist_zero(st, sim, hmi, "XYTwist zero while the link is down")
        sim.event_mark("b5c-resume")
        during = until_mark(sim.events("b5c-pause"), "b5c-resume")
        st.record("drive_commands_while_link_down", drive_commands(during))
        sim.command("r")  # the sim is still ENABLED: it never got a DISABLE
        seen = watch_screens(hmi, RESUME_WATCH_S)
        after = sim.events("b5c-resume")
        st.record("resume_with_sim_enabled", {
            "screens": seen,
            "unlocked_without_asking": (any(name == "DriveScreen" for _, name in seen)
                                        and not any(e["request"] == "ENABLE"
                                                    for e in drive_commands(after))),
            "drive_commands": drive_commands(after),
            "xytwist_nonzero_samples": nonzero_xytwist(after)})
        ctx.shot(st.out / "b5c-after-resume.png")
    clean_up(st, sim, hmi)
    return st.result()


def b5d(st: Step, sim, hmi, ctx: Ctx) -> dict:
    refusing = False
    if to_drive(st, sim, hmi, "b5d-unlock") and relock_by_link_loss(st, sim, hmi, ctx,
                                                                     "b5d-pause"):
        refusing = st.check("sim refuses ENABLE (x)", set_refuse_drive(sim, True),
                            "so nothing sent from here can grant driving")
        if refusing:
            sim.event_mark("b5d-resume")
            sim.command("r")
            on_drive, name, t = wait_screen(hmi, "DriveScreen", RESUME_WATCH_S)
            st.record("screen_after_resume_with_sim_enabled",
                      {"screen": name, "after_s": round(t, 2)})
            if on_drive:
                sim.event_mark("b5d-profile")
                hmi.tap(*PROFILE_LOW)
                dc = sim.wait_event(is_drive_command, COMMAND_WITHIN_S, "b5d-profile")
                st.check("profile tap reaches the sim as a DriveCommand", dc is not None,
                         json.dumps(dc) if dc else "nothing in the sim log within "
                         f"{COMMAND_WITHIN_S:.0f}s of the tap")
                st.record("profile_click_after_relock", drive_commands(
                    until_mark(sim.events("b5d-profile"), "b5d-restore")))
                ctx.shot(st.out / "b5d-after-profile-tap.png")
                sim.event_mark("b5d-restore")
                hmi.tap(*PROFILE_NORMAL)
                sim.wait_event(is_drive_command, COMMAND_WITHIN_S, "b5d-restore")
                st.record("profile_restore_to_normal", drive_commands(sim.events("b5d-restore")))
            else:
                st.record("profile_click_after_relock",
                          f"not reachable: the HMI stayed on {name} after the link came "
                          "back with the sim ENABLED (no profile button off DriveScreen)")
    if refusing:
        st.check("sim stops refusing ENABLE (x)", set_refuse_drive(sim, False), "clean-up")
    clean_up(st, sim, hmi)
    return st.result()


def b5e_one(st: Step, sim, hmi, ctx: Ctx, mode: str) -> None:
    tag = f"b5e-{mode}"
    if not to_drive(st, sim, hmi, f"{tag}-unlock"):
        return
    word = "ignoring" if mode == "ign" else "dropping"
    armed = sim.reply(f"{mode} {FAULT_COUNT}", rf"{word} the next {FAULT_COUNT}")
    if not st.check(f"set-up: sim armed '{mode} {FAULT_COUNT}'", armed is not None, ""):
        return
    sim.event_mark(f"{tag}-exit")
    hmi.hold(HOLD_MS)  # the exit hold: DISABLE, which the sim ignores or drops
    seen = watch_screens(hmi, RECORD_WINDOW_S)
    ctx.shot(st.out / f"{tag}-after-window.png")
    sim.reply(f"{mode} 0", rf"{word} the next 0")
    sim.event_mark(f"{tag}-key")
    window = until_mark(sim.events(f"{tag}-exit"), f"{tag}-key")
    commands = drive_commands(window)
    disables = [c for c in commands if c["request"] == "DISABLE"]
    gaps = [round(b["dt_s"] - a["dt_s"], 3) for a, b in zip(disables, disables[1:])]
    st.record(f"{mode}_exit_hold_window", {
        "window_s": RECORD_WINDOW_S, "disable_count": len(disables),
        "disable_gaps_s": gaps, "drive_commands": commands, "screens": seen,
        "xytwist_nonzero_samples": nonzero_xytwist(window)})
    hmi.open_menu()  # the burger key: from EXIT_REFUSED it asks to stop again
    seen = watch_screens(hmi, LOCKED_WITHIN_S)
    st.record(f"{mode}_burger_key_after", {
        "drive_commands": drive_commands(sim.events(f"{tag}-key")), "screens": seen})


def b5e(st: Step, sim, hmi, ctx: Ctx) -> dict:
    for mode in ("ign", "drop"):
        b5e_one(st, sim, hmi, ctx, mode)
        clean_up(st, sim, hmi)
    return st.result(characterisation=True)


RUNNERS: dict[str, Callable] = {"B5a": b5a, "B5b": b5b, "B5c": b5c, "B5d": b5d, "B5e": b5e}


def run_step(name: str, ip: str, out: pathlib.Path, tree: pathlib.Path) -> dict:
    """One step with its own simulated MCB and remote-UI connection."""
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(tree / "scripts"))
    hmi_ui = importlib.import_module("hmi_ui")
    spec = importlib.import_module("rammp_rtps")
    st = Step(name, out)
    with peers.SimChild(ip, tree, out / "sim.log", event_log=out / "sim-events.jsonl") as sim:
        if not sim.wait_ready():
            st.check("set-up: sim ready", False, sim.not_ready_reason())
            return st.result()
        with ui_client.open_hmi(hmi_ui, ip, out / "remote-ui.jsonl") as hmi:
            ctx = Ctx(spec=spec, shot=lambda path: hmi_ui.capture(hmi, path, False))
            result = RUNNERS[name](st, sim, hmi, ctx)
            result["remote_ui"] = hmi.stats()
            return result


def scenario(ip: str, out: pathlib.Path, tree: pathlib.Path, steps: list[str]) -> dict:
    return {name: run_step(name, ip, out / name.lower(), tree) for name in steps}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ip", required=True)
    p.add_argument("--out", type=pathlib.Path,
                   default=common.BENCH_HOME / f"hazards-{common.stamp()}")
    p.add_argument("--tree", type=pathlib.Path, default=common.REPO)
    p.add_argument("--steps", default=",".join(STEPS))
    a = p.parse_args()
    by_upper = {s.upper(): s for s in STEPS}
    steps = [by_upper.get(s.strip().upper(), s.strip()) for s in a.steps.split(",") if s.strip()]
    unknown = [s for s in steps if s not in STEPS]
    if unknown:
        p.error(f"unknown steps {unknown}; known: {STEPS}")
    strays = common.stray_peers()
    if strays:
        print(json.dumps({"verdict": "INVALID", "processes": strays}, indent=2))
        return 2
    import lease
    with lease.held():
        report = scenario(a.ip, a.out, a.tree, steps)
    print(json.dumps(report, indent=2, default=str))
    return 1 if any(r["verdict"] == "FAIL" for r in report.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
