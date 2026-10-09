"""B5''-1..15: hazard fix C1 on the board (docs/plans/hazard-c1-spec.md §6). No motors.

The MCB is the simulated one; the stick is injected (STICK, refreshed every 100 ms) only
after the sole-sim proof (hazard_rig.py). Each step's script drives the rig and records;
its grader decides from the record alone (hazard_grade.Trace), so every grader is tested on
hand-written traces (hazard_selftest.py). The pass criteria below are the spec's, quoted.

Start of every step (C1 §6): sim `ok` (IDLE), every mode off, Locked, the stick centred,
and `PERMIT POST pass` (with C3 in the image: "STATE post = PASS" instead, C3 F6). End:
graded clean-up, Locked. "Forward" = the vertical pot at its calibrated min (y = +1);
"centre" = the calibrated centres; both from STATE's cal.

| Step | Sim | Pass if |
| --- | --- | --- |
| B5''-1 | a | DriveScreen within 3.0 s; no DriveCommand after the mark; no non-zero XYTwist for 3.0 s after Drive; notice CENTRE_FIRST |
| B5''-1b | a | from 1: centre 0.2 s, forward 2 s: no non-zero XYTwist |
| B5''-1c | a | centre 0.5 s, forward: first y > 0 within 0.3 s; >= 90 % non-zero over the next 2 s |
| B5''-2 | i | Locked within 1.5 s; no non-zero XYTwist from mark + 0.6 s; 1 DISABLE, 0 ENABLE in [mark, mark + 2 s] |
| B5''-3 | e, z | as 2, once each |
| B5''-4 | p | Locked within 3.0 s; no non-zero from mark + 2.6 s; >= 1 DISABLE; after r: Locked for 5 s |
| B5''-4b | drop 1, p, r | as 4; DriveScreen within 3.0 s of r; no non-zero for 3.0 s; notice CENTRE_FIRST |
| B5''-5 | - | first DISABLE within 2.2 s; Locked within 1.0 s of it; <= 3 DISABLEs in 4 s; no ENABLE |
| B5''-6 | s | Drive throughout; >= 90 % non-zero; 18-22 DISABLEs in 5 s, gaps <= 0.4 s; STOPPING by 0.5 s, MCB_DID_NOT_STOP at 5.0..5.5 s; gaps 0.85..1.3 s after 5.5 s |
| B5''-6b | s off | Locked within 1.5 s; notice NONE; <= 1 DISABLE after Locked; none from Locked + 2 s |
| B5''-7 | drop 3 | Locked within 2.0 s of the first DISABLE; >= 4 DISABLEs |
| B5''-8 | s | as 6 with the burger key; then s off: Locked with the menu closed (owner decision 2026-10-09: keep the table; C1 §6 said "open") |
| B5''-9 | a | Joystick screen and calibrating for 5 s; no non-zero XYTwist; after the cancel DriveScreen within 3.0 s |
| B5''-10 | a | after CAL UNSAVED: DriveScreen within 3.0 s; notice NOT_CALIBRATED; no non-zero XYTwist |
| B5''-11 | a | PERMIT POST pending: zero, notice POST_NOT_PASSED; zero 1 s after pass; y > 0 within 0.3 s at the end |
| B5''-12 | a | PERMIT STICK fault: zero within 0.2 s, notice STICK_FAULT; zero 1 s after ok; y > 0 at the end |
| B5''-13 | a | exactly 1 DriveCommand in 1 s after the LOW tap: ENABLE, profile LOW |
| B5''-14 | a | from the Seat screen: DriveScreen within 3.0 s |
| B5''-15 | ongone keep, a | after Restart HMI: DriveScreen within 30 s; no non-zero XYTwist until 60 s; no ENABLE |

No C1 step grades a banner (STATE's `banner` is graded in C3 and C2 steps).
Retired by later fixes (hazard_steps.py plans them out): B5''-15 by C3's B5''-18b (C3 F5),
B5''-12 with C2 (C2 E9: the stick monitor is the only writer of stick health).
"""

from __future__ import annotations

import time

import hazard_grade as hg
from hazard_rig import PROFILE_LOW, PROFILE_NORMAL, HazardStep, NotRun, Rig

DRIVE = "DriveScreen"
LOCKED = "LockedScreen"


# ---------------------------------------------------------------- shared script pieces


def to_drive_by_mcb(rig: Rig, label: str = "enter", within: float = 4.0) -> float:
    """sim `a`: the MCB enables on its own and the HMI follows (M1). Set-up, graded."""
    rig.mark(label)
    rig.sim_cmd("a")
    s = rig.watch(within, until=hg.screen_is(DRIVE))
    if not rig.st.check("set-up: sim a -> DriveScreen", s is not None,
                        f"screen {rig.state().get('screen')}"):
        raise NotRun("bench", "could not reach DriveScreen for the step's set-up")
    return time.monotonic()


def driving_forward(rig: Rig, label: str = "enter") -> None:
    """Drive (sim `a`), centred 0.5 s, then forward with XYTwist y > 0 seen (set-up)."""
    to_drive_by_mcb(rig, label)
    rig.centre()
    time.sleep(0.5)  # C1 G1: centred longer than the 300 ms neutral hold
    rig.forward()
    ok = rig.wait_y(lambda y: y > 0, 1.5)
    if not rig.st.check("set-up: driving forward (XYTwist y > 0)", ok, ""):
        raise NotRun("bench", "the stick did not drive in the step's set-up")
    time.sleep(0.5)  # driving for a while before the step's action


def watch_disables(rig: Rig, after: str, within: float = 3.0) -> float | None:
    """Wait for the sim to log the first DISABLE after mark `after`, polling STATE all the
    while (the notice that comes with that DISABLE is graded against its time); its stamp."""
    deadline = time.monotonic() + within
    while True:
        e = next((e for e in rig.sim.events(after) if e.get("ev") == "drive_command"
                  and e.get("request") == "DISABLE"), None)
        if e is not None:
            return e["mono"]
        if time.monotonic() >= deadline:
            return None
        rig.watch(0.1)  # one STATE poll per period until the DISABLE is in the sim's log


# ---------------------------------------------------------------- graders (pure)


def _drive_after(tr: hg.Trace, t: float) -> tuple[float, dict] | None:
    return hg.first_state(tr.states, hg.screen_is(DRIVE), t)


def _locked_after(tr: hg.Trace, t: float) -> tuple[float, dict] | None:
    return hg.first_state(tr.states, hg.screen_is(LOCKED), t)


def check_within(st: HazardStep, what: str, found: tuple[float, dict] | None, t0: float,
                 limit: float) -> float | None:
    """`what` within `limit` s of t0 (the first STATE poll that shows it, C3 §7)."""
    if found is None:
        st.check(f"{what} within {limit:.1f} s", False, "never seen")
        return None
    dt = found[0] - t0
    st.check(f"{what} within {limit:.1f} s", dt <= limit, f"after {dt:.2f} s")
    return found[0]


def check_no_nonzero(st: HazardStep, what: str, tr: hg.Trace, t0: float, t1: float) -> None:
    bad = hg.nonzero_in(tr.samples, t0, t1)
    n = len(hg.window(tr.samples, t0, t1))
    st.check(what, n > 0 and not bad,
             f"{n} samples in [{t0:.2f}, {t1:.2f}], {len(bad)} non-zero"
             + (f", first {list(bad[0])}" if bad else ""))


# C1 §3.3: GATE_SHUT and CALIBRATING have no notice text (the Drive screen is not up yet,
# or the calibration screen is): with either as the hold reason the notice is NONE.
NO_TEXT_REASONS = ("GATE_SHUT", "CALIBRATING")


def notice_as_spec(notice: str):
    """The Drive notice C1 §2.7/§3.3 asks for: `notice`, or NONE while the hold reason is
    one §3.3 gives no text (e.g. GATE_SHUT until the Drive screen's fade has opened the
    gate, about 0.4 s after STATE first shows DriveScreen)."""
    return lambda s: s.get("notice") == notice or (
        s.get("notice") == "NONE" and s.get("hold_reason") in NO_TEXT_REASONS)


def check_notice_all(st: HazardStep, tr: hg.Trace, notice: str, t0: float, t1: float) -> None:
    ok, n, bad = hg.all_states(tr.states, notice_as_spec(notice), t0, t1)
    shown = sum(1 for t, s in hg.states_in(tr.states, t0, t1) if s.get("notice") == notice)
    st.check(f"STATE notice {notice}", ok and shown > 0,
             f"{n} polls in [{t0:.2f}, {t1:.2f}], {shown} show it"
             + (f"; first other {bad[0]}" if bad else ""))


def grade_b1(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    m = tr.mark("enter")
    d = check_within(st, "DriveScreen after sim a", _drive_after(tr, m), m, 3.0)
    st.check("no DriveCommand after the mark", not hg.drive_commands(tr.events, m),
             str([c["request"] for c in hg.drive_commands(tr.events, m)]))
    if d is not None:
        check_no_nonzero(st, "no non-zero XYTwist for 3.0 s after Drive", tr, d, d + 3.0)
        check_notice_all(st, tr, "CENTRE_FIRST", d + 0.25, d + 3.0)


def grade_b1b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    c, f = tr.mark("centre"), tr.mark("forward")
    st.check("centred for less than 300 ms", f - c < 0.3, f"{f - c:.3f} s")
    check_no_nonzero(st, "no non-zero XYTwist (centre 0.2 s < 300 ms, then forward 2 s)",
                     tr, c, f + 2.0)


def check_resume(st: HazardStep, tr: hg.Trace, f: float, within: float = 0.3) -> Sample | None:
    s = hg.first_sample(tr.samples, hg.y_positive, f)
    st.check(f"first y > 0 within {within:.1f} s of forward", s is not None and s[0] - f <= within,
             "never" if s is None else f"after {s[0] - f:.3f} s")
    return s


Sample = hg.Sample


def grade_b1c(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    f = tr.mark("forward")
    s = check_resume(st, tr, f)
    if s is not None:
        win = hg.window(tr.samples, s[0], s[0] + 2.0)
        frac = hg.fraction(win, hg.is_nonzero)
        st.check(">= 90 % of samples non-zero over the next 2 s", frac >= 0.9,
                 f"{frac:.1%} of {len(win)}")


def grade_mcb_stops(st: HazardStep, tr: hg.Trace, label: str) -> None:
    m = tr.mark(label)
    check_within(st, f"{label}: Locked", _locked_after(tr, m), m, 1.5)
    check_no_nonzero(st, f"{label}: no non-zero XYTwist from mark + 0.6 s", tr, m + 0.6, m + 2.5)
    dis = hg.drive_commands(tr.events, m, m + 2.0, "DISABLE")
    en = hg.drive_commands(tr.events, m, m + 2.0, "ENABLE")
    st.check(f"{label}: exactly 1 DISABLE and 0 ENABLE in [mark, mark + 2 s]",
             len(dis) == 1 and not en, f"{len(dis)} DISABLE, {len(en)} ENABLE")


def grade_b2(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    grade_mcb_stops(st, tr, "stop-i")


def grade_b3(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    grade_mcb_stops(st, tr, "stop-e")
    grade_mcb_stops(st, tr, "stop-z")


def grade_stale_relock(st: HazardStep, tr: hg.Trace, until: float) -> None:
    m = tr.mark("pause")
    check_within(st, "Locked after sim p", _locked_after(tr, m), m, 3.0)
    check_no_nonzero(st, "no non-zero XYTwist from mark + 2.6 s", tr, m + 2.6, until)


def grade_b4(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    r = tr.mark("resume")
    grade_stale_relock(st, tr, r + 5.0)
    dis = hg.drive_commands(tr.events, tr.mark("pause"), r, "DISABLE")
    st.check(">= 1 DISABLE after the mark", len(dis) >= 1, f"{len(dis)}")
    ok, n, bad = hg.all_states(tr.states, hg.screen_is(LOCKED), r, r + 5.0)
    st.check("after r (sim IDLE): Locked for 5 s", ok, f"{n} polls; first other {bad}")


def grade_b4b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    r = tr.mark("resume")
    grade_stale_relock(st, tr, r)
    d = check_within(st, "DriveScreen after r (sim still ENABLED)", _drive_after(tr, r), r, 3.0)
    if d is not None:
        check_no_nonzero(st, "no non-zero XYTwist for 3.0 s", tr, d, d + 3.0)
        check_notice_all(st, tr, "CENTRE_FIRST", d + 0.25, d + 3.0)


def grade_b5(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    x = tr.mark("exit")
    dis = hg.drive_commands(tr.events, x, x + 4.0, "DISABLE")
    if not st.check("first DISABLE within 2.2 s of the mark", bool(dis)
                    and dis[0]["mono"] - x <= 2.2,
                    f"after {dis[0]['mono'] - x:.2f} s" if dis else "none"):
        return
    t1 = dis[0]["mono"]
    check_within(st, "Locked after the first DISABLE", _locked_after(tr, t1), t1, 1.0)
    st.check("<= 3 DISABLEs in [mark, mark + 4 s]", len(dis) <= 3, f"{len(dis)}")
    en = hg.drive_commands(tr.events, x, x + 4.0, "ENABLE")
    st.check("no ENABLE", not en, f"{len(en)}")


def grade_ignored_stop(st: HazardStep, tr: hg.Trace, label: str, output: str) -> float | None:
    """B5''-6's criteria from the first DISABLE t1 after mark `label`. `output`: "drives"
    (G4: >= 90 % non-zero) or "zero" (C2-9: G3 over G4, every sample 0 after mark + 0.2)."""
    x = tr.mark(label)
    first = hg.drive_commands(tr.events, x, x + 4.0, "DISABLE")
    if not st.check("a DISABLE after the stop", bool(first), "none"):
        return None
    t1 = first[0]["mono"]
    ok, n, bad = hg.all_states(tr.states, hg.screen_is(DRIVE), t1, t1 + 7.0)
    st.check("DriveScreen the whole time (7 s from the first DISABLE)", ok,
             f"{n} polls; first other {bad}")
    win = hg.window(tr.samples, t1, t1 + 7.0)
    if output == "drives":
        frac = hg.fraction(win, hg.is_nonzero)
        st.check(">= 90 % of XYTwist samples non-zero (owner-accepted risk, G4)", frac >= 0.9,
                 f"{frac:.1%} of {len(win)}")
    else:
        check_no_nonzero(st, "every XYTwist after mark + 0.2 s is 0 (G3 over G4)", tr, x + 0.2,
                         t1 + 7.0)
    fast = [e["mono"] for e in hg.drive_commands(tr.events, t1, t1 + 5.0, "DISABLE")]
    st.check("18-22 DISABLEs in [t1, t1 + 5.0 s]", 18 <= len(fast) <= 22, f"{len(fast)}")
    g = hg.gaps(fast)
    st.check("every gap <= 0.4 s", bool(g) and max(g) <= 0.4, f"max {max(g) if g else None}")
    stopping = hg.first_state(tr.states, hg.field_is("notice", "STOPPING"), x)
    st.check("notice STOPPING by t1 + 0.5 s", stopping is not None and stopping[0] <= t1 + 0.5,
             "never" if stopping is None else f"at t1 + {stopping[0] - t1:.2f} s")
    fault = hg.first_state(tr.states, hg.field_is("notice", "MCB_DID_NOT_STOP"), x)
    st.check("notice MCB_DID_NOT_STOP at t1 + 5.0..5.5 s",
             fault is not None and 5.0 <= fault[0] - t1 <= 5.5,
             "never" if fault is None else f"at t1 + {fault[0] - t1:.2f} s")
    every = [e["mono"] for e in hg.drive_commands(tr.events, t1, t1 + 7.0, "DISABLE")]
    before = [t for t in every if t < t1 + 5.5]
    slow = ([before[-1]] if before else []) + [t for t in every if t >= t1 + 5.5]
    g = hg.gaps(slow)
    st.check("after t1 + 5.5 s DISABLE gaps 0.85..1.3 s",
             bool(g) and all(0.85 <= gap <= 1.3 for gap in g), f"{g}")
    return t1


def grade_b6(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    grade_ignored_stop(st, tr, "exit", "drives")


def grade_b6b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    x = tr.mark("exit")
    st.check("set-up (6): a DISABLE re-sent while the MCB ignored it",
             len(hg.drive_commands(tr.events, x, tr.mark("s-off"), "DISABLE")) >= 2, "")
    off = tr.mark("s-off")
    lk = check_within(st, "Locked after s off", _locked_after(tr, off), off, 1.5)
    if lk is None:
        return
    after = hg.first_state(tr.states, lambda s: True, lk + 0.25)
    st.check("notice NONE", after is not None and after[1].get("notice") == "NONE",
             str(after[1].get("notice")) if after else "no poll")
    dis = hg.drive_commands(tr.events, lk, float("inf"), "DISABLE")
    st.check("<= 1 DISABLE after Locked", len(dis) <= 1, f"{len(dis)}")
    late = hg.drive_commands(tr.events, lk + 2.0, float("inf"), "DISABLE")
    st.check("none from Locked + 2 s", not late, f"{len(late)}")


def grade_b7(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    x = tr.mark("exit")
    dis = hg.drive_commands(tr.events, x, float("inf"), "DISABLE")
    if not st.check("a DISABLE after the exit hold", bool(dis), "none"):
        return
    t1 = dis[0]["mono"]
    check_within(st, "Locked after the first DISABLE", _locked_after(tr, t1), t1, 2.0)
    st.check(">= 4 DISABLEs", len(dis) >= 4,
             f"{len(dis)} ({[d.get('action') for d in dis]})")


def grade_b8(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    grade_ignored_stop(st, tr, "exit", "drives")
    # Owner decision 2026-10-09, hazard-decisions.md (keep the table): after a refused
    # burger-key stop the session ends on Locked with the menu CLOSED (C1 §2.3 row 9,
    # F2 -> F2 + SEND_DISABLE, CLEAR_STOP_FAULT: no menu-on-arrival from EXIT_REFUSED; the
    # TABLE.md invariant). C1 §6's "Locked with the menu open" is superseded.
    off = tr.mark("s-off")
    s = hg.first_state(tr.states, hg.screen_is(LOCKED), off)
    if not st.check("then Locked (STATE)", s is not None,
                    "never" if s is None else f"after {s[0] - off:.2f} s"):
        return
    ok, n, bad = hg.all_states(tr.states, lambda x: x.get("screen") == LOCKED
                               and x.get("menu_open") is False, s[0], float("inf"))
    st.check("with the menu closed (owner decision 2026-10-09)", ok,
             f"{n} polls; first other {bad}")


def grade_b9(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    a, c = tr.mark("enable"), tr.mark("cancelled")
    ok, n, bad = hg.all_states(tr.states, lambda s: s.get("screen") == "JoystickScreen"
                               and s.get("calibrating") is True, a, a + 5.0)
    st.check("screen stays Joystick and calibrating stays 1 for 5 s", ok,
             f"{n} polls; first other {bad}")
    check_no_nonzero(st, "no non-zero XYTwist", tr, a, c)
    check_within(st, "after the cancel, DriveScreen", _drive_after(tr, c), c, 3.0)


def grade_b10(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    a, f = tr.mark("enter"), tr.mark("forward")
    d = check_within(st, "DriveScreen after sim a", _drive_after(tr, a), a, 3.0)
    if d is not None:
        check_notice_all(st, tr, "NOT_CALIBRATED", d + 0.25, f + 2.0)
    check_no_nonzero(st, "no non-zero XYTwist", tr, a, f + 2.0)


def grade_b11(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    a, ps = tr.mark("enter"), tr.mark("pass")
    d = _drive_after(tr, a)
    st.check("set-up: DriveScreen while POST pending", d is not None, "")
    check_no_nonzero(st, "zero while pending", tr, a, ps)
    if d is not None:
        check_notice_all(st, tr, "POST_NOT_PASSED", d[0] + 0.25, ps)
    check_no_nonzero(st, "zero for the 1 s after pass (latch)", tr, ps, ps + 1.0)
    check_resume(st, tr, tr.mark("forward"))


def grade_b12(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    k, o = tr.mark("fault"), tr.mark("ok")
    check_no_nonzero(st, "XYTwist zero within 0.2 s of fault", tr, k + 0.2, o)
    check_notice_all(st, tr, "STICK_FAULT", k + 0.25, o)
    check_no_nonzero(st, "zero 1 s after ok", tr, o, o + 1.0)
    f = tr.mark("forward")
    s = hg.first_sample(tr.samples, hg.y_positive, f)
    st.check("y > 0 at the end", s is not None and s[0] - f <= 1.0,
             "never" if s is None else f"after {s[0] - f:.2f} s")


def grade_b13(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("tap")
    cmds = hg.drive_commands(tr.events, t, t + 1.0)
    st.check("exactly 1 DriveCommand in 1 s: ENABLE, profile LOW",
             len(cmds) == 1 and cmds[0]["request"] == "ENABLE" and cmds[0]["profile"] == "LOW",
             str([(c["request"], c["profile"]) for c in cmds]))


def grade_b14(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    a = tr.mark("enter")
    # the mark follows the poll that showed SeatScreen; the two clocks tick in 16 ms steps
    seat = hg.first_state(tr.states, hg.screen_is("SeatScreen"), a - 30.0, a + 0.3)
    st.check("set-up: on the Seat screen before sim a", seat is not None, "")
    check_within(st, "DriveScreen after sim a from the Seat screen", _drive_after(tr, a), a, 3.0)


def grade_b15(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    r, b = tr.mark("restart"), tr.mark("back")
    check_within(st, "DriveScreen after boot", _drive_after(tr, b), r, 30.0)
    check_no_nonzero(st, "no non-zero XYTwist until 60 s (POST gate NOT_RUN after reset)", tr,
                     r, r + 60.0)
    en = hg.drive_commands(tr.events, r, float("inf"), "ENABLE")
    st.check("no ENABLE from the HMI", not en, f"{len(en)}")


# ---------------------------------------------------------------- scripts


def s_b1(rig: Rig) -> dict:
    rig.begin()
    rig.forward()
    time.sleep(0.5)  # forward held while locked
    rig.mark("enter")
    rig.sim_cmd("a")
    rig.watch(3.5, until=hg.screen_is(DRIVE))
    rig.watch(3.2)
    return {}


def _forward_entry(rig: Rig) -> None:
    rig.begin()
    rig.forward()
    time.sleep(0.5)  # forward held while locked
    rig.mark("enter")
    rig.sim_cmd("a")
    s = rig.watch(4.0, until=hg.screen_is(DRIVE))
    if not rig.st.check("set-up: sim a -> DriveScreen (forward held)", s is not None, ""):
        raise NotRun("bench", "no DriveScreen in the set-up")
    rig.watch(1.0)


def s_b1b(rig: Rig) -> dict:
    _forward_entry(rig)
    rig.mark("centre")
    rig.centre()
    time.sleep(0.2)  # shorter than the 300 ms neutral hold
    rig.mark("forward")
    rig.forward()
    rig.watch(2.2)
    return {}


def s_b1c(rig: Rig) -> dict:
    _forward_entry(rig)
    rig.mark("centre")
    rig.centre()
    time.sleep(0.5)
    rig.mark("forward")
    rig.forward()
    rig.watch(2.5)
    return {}


def _mcb_stops(rig: Rig, mode: str) -> None:
    driving_forward(rig, f"enter-{mode}")
    rig.mark(f"stop-{mode}")
    rig.sim_cmd(mode)
    rig.watch(2.6)


def s_b2(rig: Rig) -> dict:
    rig.begin()
    _mcb_stops(rig, "i")
    return {}


def s_b3(rig: Rig) -> dict:
    rig.begin()
    _mcb_stops(rig, "e")
    rig.begin()
    _mcb_stops(rig, "z")
    return {}


def s_b4(rig: Rig) -> dict:
    rig.begin()
    driving_forward(rig)
    rig.mark("pause")
    rig.sim_cmd("p")
    rig.watch(3.5, until=hg.screen_is(LOCKED))
    rig.watch(1.0)
    rig.mark("resume")
    rig.sim_cmd("r")
    rig.watch(5.2)
    return {}


def s_b4b(rig: Rig) -> dict:
    rig.begin()
    driving_forward(rig)
    rig.sim_count("drop", 1)
    rig.mark("pause")
    rig.sim_cmd("p")
    rig.watch(3.5, until=hg.screen_is(LOCKED))
    rig.watch(1.0)
    rig.mark("resume")
    rig.sim_cmd("r")
    rig.watch(3.5, until=hg.screen_is(DRIVE))
    rig.watch(3.2)
    return {}


def s_b5(rig: Rig) -> dict:
    rig.begin()
    to_drive_by_mcb(rig)
    rig.watch(0.5)
    rig.mark("exit")
    rig.hold_button()
    rig.watch(2.2)
    return {}


def _ignored_stop(rig: Rig, how: str) -> None:
    driving_forward(rig)
    rig.sim_toggle("s", True)
    rig.mark("exit")
    if how == "hold":
        rig.hold_button()
    else:
        rig.burger()
    t1 = watch_disables(rig, "exit")
    if t1 is None:
        raise NotRun("bench", "no DISABLE reached the sim after the stop")
    rig.watch(max(0.0, t1 + 7.2 - time.monotonic()))


def s_b6(rig: Rig) -> dict:
    rig.begin()
    _ignored_stop(rig, "hold")
    return {}


def s_b6b(rig: Rig) -> dict:
    rig.begin()
    _ignored_stop(rig, "hold")
    rig.mark("s-off")
    rig.sim_toggle("s", False)
    rig.watch(4.0)
    return {}


def s_b7(rig: Rig) -> dict:
    rig.begin()
    to_drive_by_mcb(rig)
    rig.sim_count("drop", 3)
    rig.mark("exit")
    rig.hold_button()
    rig.watch(3.5)
    return {}


def s_b8(rig: Rig) -> dict:
    rig.begin()
    _ignored_stop(rig, "burger")
    rig.mark("s-off")
    rig.sim_toggle("s", False)
    rig.watch(4.0, until=hg.screen_is(LOCKED))
    rig.watch(1.0)  # it stays Locked with the menu closed
    return {}


def s_b9(rig: Rig) -> dict:
    rig.begin()
    rig.go("Joystick")
    if rig.watch(3.0, until=hg.screen_is("JoystickScreen")) is None:
        raise NotRun("bench", "could not open the Joystick screen")
    rig.hold_button()
    s = rig.watch(2.0, until=hg.field_is("calibrating", True))
    if not rig.st.check("set-up: calibrating = 1 after the 2 s hold", s is not None, ""):
        raise NotRun("bench", "the calibration did not start")
    rig.mark("enable")
    rig.sim_cmd("a")
    rig.watch(5.0)
    rig.hold_button()  # cancels the run
    rig.mark("cancelled")
    rig.watch(3.5, until=hg.screen_is(DRIVE))
    return {}


def s_b10(rig: Rig) -> dict:
    rig.begin()
    rig.bench_verb(rig.hmi.cal_unsaved, "CAL UNSAVED")
    rig.mark("enter")
    rig.sim_cmd("a")
    rig.watch(3.5, until=hg.screen_is(DRIVE))
    rig.watch(0.5)  # centred
    rig.mark("forward")
    rig.forward()
    rig.watch(2.1)
    return {}


def s_b11(rig: Rig) -> dict:
    rig.begin()
    rig.permit("post", "pending")
    rig.mark("enter")
    rig.sim_cmd("a")
    rig.watch(4.0, until=hg.screen_is(DRIVE))
    rig.watch(0.5)  # centred
    rig.forward()
    rig.watch(1.0)
    rig.mark("pass")
    rig.permit("post", "pass")
    rig.watch(1.1)  # forward held
    rig.centre()
    time.sleep(0.5)
    rig.mark("forward")
    rig.forward()
    rig.watch(1.0)
    return {}


def s_b12(rig: Rig) -> dict:
    rig.begin()
    driving_forward(rig)
    rig.mark("fault")
    rig.permit("stick", "fault")
    rig.watch(1.0)
    rig.mark("ok")
    rig.permit("stick", "ok")
    rig.watch(1.1)  # forward held
    rig.centre()
    time.sleep(0.5)
    rig.mark("forward")
    rig.forward()
    rig.watch(1.2)
    return {}


def s_b13(rig: Rig) -> dict:
    rig.begin()
    to_drive_by_mcb(rig)
    rig.watch(1.0)
    rig.mark("tap")
    rig.tap(*PROFILE_LOW)
    rig.watch(1.2)
    rig.mark("restore")
    rig.tap(*PROFILE_NORMAL)
    rig.watch(0.5)
    return {}


def s_b14(rig: Rig) -> dict:
    rig.begin()
    rig.go("Seat Functions")
    rig.watch(3.0, until=hg.screen_is("SeatScreen"))
    rig.mark("enter")
    rig.sim_cmd("a")
    rig.watch(3.5, until=hg.screen_is(DRIVE))
    return {}


# B5''-15 restarts from DriveScreen: through the port (hazard_rig.Rig.restart_hmi).
NEEDS_SERIAL = {"B5''-15"}


def s_b15(rig: Rig) -> dict:
    rig.begin()
    rig.sim_ongone("keep")
    to_drive_by_mcb(rig)
    rig.mark("restart")
    t_r = rig.restart_hmi()
    rig.wait_back()
    rig.forward()  # forward held from the remote UI's return
    rig.mark("back")
    rig.watch(max(0.0, t_r + 61.0 - time.monotonic()))
    return {}


STEPS = {
    "B5''-1": (s_b1, grade_b1, "entry, G1: forward held, sim a"),
    "B5''-1b": (s_b1b, grade_b1b, "G1 timing: centre 0.2 s, forward 2 s"),
    "B5''-1c": (s_b1c, grade_b1c, "centre 0.5 s, then forward"),
    "B5''-2": (s_b2, grade_b2, "MCB stops on its own (i)"),
    "B5''-3": (s_b3, grade_b3, "MCB goes ERROR, then INITIALIZING (e, z)"),
    "B5''-4": (s_b4, grade_b4, "stale MibStatus, DISABLE obeyed (p)"),
    "B5''-4b": (s_b4b, grade_b4b, "stale, DISABLE lost (drop 1, p, r)"),
    "B5''-5": (s_b5, grade_b5, "stop obeyed (exit hold)"),
    "B5''-6": (s_b6, grade_b6, "MCB ignores DISABLE (s)"),
    "B5''-6b": (s_b6b, grade_b6b, "s off after 6"),
    "B5''-7": (s_b7, grade_b7, "lost DISABLEs (drop 3)"),
    "B5''-8": (s_b8, grade_b8, "burger key, ignored (s), then s off"),
    "B5''-9": (s_b9, grade_b9, "calibration running, sim a"),
    "B5''-10": (s_b10, grade_b10, "never calibrated (CAL UNSAVED)"),
    "B5''-11": (s_b11, grade_b11, "POST hook (PERMIT POST)"),
    "B5''-12": (s_b12, grade_b12, "stick-fault hook (PERMIT STICK)"),
    "B5''-13": (s_b13, grade_b13, "profile tap"),
    "B5''-14": (s_b14, grade_b14, "entry from another screen"),
    "B5''-15": (s_b15, grade_b15, "HMI reset, MCB ENABLED"),
}
# Steps after which the board is restarted (CAL UNSAVED is RAM only: C1 §6 "reboot after").
RESTART_AFTER = {"B5''-10"}
