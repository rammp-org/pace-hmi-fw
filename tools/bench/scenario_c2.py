"""C2-1..C2-16: hazard fix C2 (stick plausibility, the stick monitor) on the board
(docs/plans/hazard-c2-spec.md §9). No motors: the sim plays the MCB, the stick is injected
(stick-injection build, ci/sdkconfig.stick_inject) after the sole-sim proof.

Start of every step (C2 §9): Locked, sim `ok`, POST passed (stick injected at rest), STATE
stick OK. End: Locked with the stick OK (graded). A latched FAULT is cleared by a reboot
(the Restart HMI tile; the latch is RAM only), never by a calibration: the owner's standing
rule forbids rewriting the stored calibration on the bench, even with the same values
(coordinator, 2026-10-08; C2 §9 said "an injected calibration"). t = the time the step's
first changed STICK message was sent (its mark).
"Forward" = the vertical read at the calibrated min; "centre" = the calibrated centres.

| Step | Script | Pass if |
| --- | --- | --- |
| C2-1 | a, Drive; centre 0.5 s; vertical 6 mV 2 s | stick OK throughout; >= 90 % of XYTwist in [t + 0.3, t + 2] s have y >= 0.99 |
| C2-2 | as C2-1 with 0 mV | the same (RESIDUAL-D2 confirmed) |
| C2-3 | forward 1 s; horizontal 3300 mV; centre 3 s | every XYTwist after t + 200 ms exactly 0; FAULT within t + 500 ms, HIGH horizontal; still FAULT and 0 after centring; notice STICK_FAULT |
| C2-3b | vertical at its limit - 20 mV 2 s, then + 20 mV | first OK, full reverse (y <= -0.99); then FAULT HIGH vertical |
| C2-4a | forward; one message mask 0x81; held 2 s; centre 0.5 s; forward | never FAULT; >= 1 zero after t; 0 while forward held; y > 0 within 0.3 s; suspect onsets +1 |
| C2-4b | forward; mask 0x81 every 200 ms for 1 s | FAULT within t + 1.0 s; 0 from t + 200 ms |
| C2-4c | centre; mask 0x81 every 1.2 s for 12 s | never FAULT; suspect onsets +10 |
| C2-5 | forward; vertical read fails (0x02) 2 s | >= 25 Hz; all 0 after t + 200 ms; FAULT MISSING vertical by t + 500 ms |
| C2-6 | forward; X/Y frozen (0x40) 2 s | a y > 0 in [t, t + 250 ms]; all 0 after t + 550 ms; FAULT STALE by t + 800 ms |
| C2-7 | forward; vertical NaN (0x10) 2 s | as C2-5, NAN vertical |
| C2-8 | sim normal; forward; rail | 1 DISABLE in [t, t + 600 ms], none before; Locked by t + 1.5 s; banner STICK_FAULT; no ENABLE; 0 after t + 200 ms |
| C2-9 | sim s; as C2-8; 7 s | Drive throughout; every XYTwist after t + 200 ms 0; B5''-6's DISABLE timing |
| C2-10 | after C2-3: centre 5 s, forward 2 s, sim a, centre 0.5 s, forward 2 s | no non-zero XYTwist; FAULT throughout; notice STICK_FAULT |
| C2-11 | OK then FAULT; Settings; down, up, right, left 0.5 s each | FAULT: focus and screen unchanged, joy_key 0; OK (control): the focus moves |
| C2-12 | FAULT; Home, Settings, Joystick, Drive 15 s each | the fault indicator on every screen; FAULT after 60 s |
| C2-13 | FAULT; Calibrate by touch; inject the run | NOT_RUN: a completed run saves the record (REQ-CAL-06), which the bench may not do; CAL UNSAVED cannot stand in (it moves no generation counter) |
| C2-14 | Restart HMI; centre from the start | stick OK within 1.5 s of the first STATE; never FAULT; xy_age_max_ms <= 200 |
| C2-15 | 30 min on Drive, centred; then the self test | joy.bad_samples 0, joy.xy_age_max <= 200, joy.health OK; time.adc_avg 34.5..35.5; time.adc_max <= 40; mem.stk_adc >= 2048 |
| C2-16 | STALL CADC 1000 | FAULT STALE; output 0 by 340 ms after the stall start (+ transport) |

Banners: STATE's `banner` (owner, 2026-10-08). Not graded: the indicator in a SHOT (C2-12:
STATE grades it).
"""

from __future__ import annotations

import time

import hazard_grade as hg
from hazard_rig import HazardStep, NotRun, Rig
from scenario_c1 import (DRIVE, LOCKED, check_no_nonzero, check_resume, check_within,
                         driving_forward, grade_ignored_stop, to_drive_by_mcb, watch_disables)

HIGH_RAIL_MV = 3150      # C2 §4
CAL_OVERSHOOT_MV = 100
SOAK_S = 30 * 60.0       # C2-15
# Steps the bench may not run, and why (checked before the board is touched).
NOT_RUNNABLE = {
    "C2-13": "the way out is a completed calibration run, which saves the record "
             "(REQ-CAL-06); the owner's standing rule forbids rewriting the stored calibration "
             "on the bench, and CAL UNSAVED does not stand in for a run (it hands over no "
             "validated record, so the monitor's generation counter does not move)",
}


def _state(name: str):
    return lambda s: (s.get("stick") or {}).get("state") == name


def _stick(s: dict, key: str):
    return (s.get("stick") or {}).get(key)


def limit_mv(cal_max: int) -> int:
    return min(HIGH_RAIL_MV, cal_max + CAL_OVERSHOOT_MV)


# ---------------------------------------------------------------- graders


def grade_mv_drives(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("change")
    ok, n, bad = hg.all_states(tr.states, _state("OK"), t, t + 2.0)
    st.check("STATE stick OK throughout", ok, f"{n} polls; first other {bad}")
    win = hg.window(tr.samples, t + 0.3, t + 2.0)
    frac = hg.fraction(win, lambda s: s[2] >= 0.99)
    passed = st.check(">= 90 % of XYTwist in [t + 0.3 s, t + 2 s] have y >= 0.99", frac >= 0.9,
                      f"{frac:.1%} of {len(win)}")
    if p.get("residual") and passed and ok:
        st.record("residual", "RESIDUAL-D2 confirmed: 0 mV drives full forward (D2, documented)")


def check_fault(st: HazardStep, tr: hg.Trace, t: float, within: float, reason: str,
                axis: str | None) -> float | None:
    f = hg.first_state(tr.states, _state("FAULT"), t)
    what = f"STATE FAULT within t + {within * 1000:.0f} ms, reason {reason}" + (
        f" {axis}" if axis else "")
    ok = (f is not None and f[0] - t <= within and _stick(f[1], "fault_reason") == reason
          and (axis is None or _stick(f[1], "fault_axis") == axis))
    st.check(what, ok, "never" if f is None else
             f"at +{f[0] - t:.2f} s: {_stick(f[1], 'fault_reason')} {_stick(f[1], 'fault_axis')}")
    return None if f is None else f[0]


def check_exact_zero_after(st: HazardStep, tr: hg.Trace, t0: float, t1: float, what: str) -> None:
    win = hg.window(tr.samples, t0, t1)
    bad = [s for s in win if not hg.is_exact_zero(s)]
    st.check(what, bool(win) and not bad, f"{len(win)} samples, {len(bad)} not 0")


def grade_c2_3(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t, c = tr.mark("change"), tr.mark("centred")
    check_exact_zero_after(st, tr, t + 0.2, c + 3.0, "every XYTwist after t + 200 ms exactly 0")
    f = check_fault(st, tr, t, 0.5, "HIGH", "H")
    ok, n, bad = hg.all_states(tr.states, _state("FAULT"), c, c + 3.0)
    st.check("still FAULT after centring", ok, f"{n} polls; first other {bad}")
    if f is not None:
        drive = [s for tt, s in hg.states_in(tr.states, f, c + 3.0) if s.get("screen") == DRIVE]
        st.check("notice STICK_FAULT (on Drive)", bool(drive)
                 and all(s.get("notice") == "STICK_FAULT" for s in drive),
                 f"{len(drive)} Drive polls; {[s.get('notice') for s in drive[:2]]}")


def grade_c2_3b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t1, t2 = tr.mark("change"), tr.mark("over")
    ok, n, bad = hg.all_states(tr.states, _state("OK"), t1, t2)
    st.check("limit - 20 mV: OK", ok, f"{n} polls; first other {bad}")
    win = hg.window(tr.samples, t1 + 0.3, t2)
    frac = hg.fraction(win, lambda s: s[2] <= -0.99)
    st.check("full reverse (y <= -0.99) after neutral", frac >= 0.9, f"{frac:.1%} of {len(win)}")
    check_fault(st, tr, t2, 1.0, "HIGH", "V")


def _onsets(tr: hg.Trace, t0: float, t1: float) -> int | None:
    polls = [s for t, s in tr.states if _stick(s, "suspect_onsets") is not None]
    before = [s for t, s in tr.states if t < t0 and _stick(s, "suspect_onsets") is not None]
    after = [s for t, s in tr.states if t >= t1 and _stick(s, "suspect_onsets") is not None]
    if not polls or not before or not after:
        return None
    return _stick(after[0], "suspect_onsets") - _stick(before[-1], "suspect_onsets")


def grade_c2_4a(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t, c, f = tr.mark("change"), tr.mark("centred"), tr.mark("forward")
    fault = hg.first_state(tr.states, _state("FAULT"), t)
    st.check("never FAULT", fault is None, "" if fault is None else f"at +{fault[0] - t:.2f} s")
    z = hg.first_sample(tr.samples, hg.is_exact_zero, t)
    st.check(">= 1 zero XYTwist after t", z is not None and z[0] < c, "")
    if z is not None:
        check_no_nonzero(st, "0 while forward stays held (C1 latch)", tr, z[0], c)
    check_resume(st, tr, f)
    d = _onsets(tr, t, f + 1.0)
    st.check("suspect onsets +1", d == 1, str(d))


def grade_c2_4b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("change")
    f = hg.first_state(tr.states, _state("FAULT"), t)
    st.check("FAULT within t + 1.0 s", f is not None and f[0] - t <= 1.0,
             "never" if f is None else f"at +{f[0] - t:.2f} s")
    check_exact_zero_after(st, tr, t + 0.2, tr.mark("end"), "0 from t + 200 ms")


def grade_c2_4c(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t, e = tr.mark("change"), tr.mark("end")
    f = hg.first_state(tr.states, _state("FAULT"), t)
    st.check("never FAULT (N of M, not lifetime)", f is None,
             "" if f is None else f"at +{f[0] - t:.2f} s")
    d = _onsets(tr, t, e)
    st.check("suspect onsets +10", d == 10, str(d))


def _grade_failed_read(st: HazardStep, tr: hg.Trace, reason: str) -> None:
    t = tr.mark("change")
    hz = hg.rate_hz(tr.samples, t, t + 2.0)
    st.check("XYTwist keeps arriving at >= 25 Hz (H9: neutral, not silence)", hz >= 25.0,
             f"{hz:.1f} Hz")
    check_exact_zero_after(st, tr, t + 0.2, t + 2.0, "all 0 after t + 200 ms")
    check_fault(st, tr, t, 0.5, reason, "V")


def grade_c2_5(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    _grade_failed_read(st, tr, "MISSING")


def grade_c2_7(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    _grade_failed_read(st, tr, "NAN")


def grade_c2_6(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("change")
    early = [s for s in hg.window(tr.samples, t, t + 0.25) if s[2] > 0]
    st.check("at least one y > 0 in [t, t + 250 ms]", bool(early), f"{len(early)}")
    check_exact_zero_after(st, tr, t + 0.55, t + 2.0, "all 0 after t + 550 ms")
    check_fault(st, tr, t, 0.8, "STALE", None)


def grade_c2_8(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("change")
    before = hg.drive_commands(tr.events, tr.mark("enter"), t, "DISABLE")
    dis = hg.drive_commands(tr.events, t, t + 0.6, "DISABLE")
    st.check("exactly one DISABLE in [t, t + 600 ms], none before", len(dis) == 1 and not before,
             f"{len(dis)} in the window, {len(before)} before")
    check_within(st, "Locked", hg.first_state(tr.states, hg.screen_is(LOCKED), t), t, 1.5)
    seen = hg.first_state(tr.states, hg.field_is("banner", "STICK_FAULT"), t, t + 2.0)
    st.check("banner STICK_FAULT", seen is not None,
             "never" if seen is None else f"at +{seen[0] - t:.2f} s")
    st.check("no ENABLE", not hg.drive_commands(tr.events, t, float("inf"), "ENABLE"), "")
    check_exact_zero_after(st, tr, t + 0.2, t + 2.0, "no non-zero XYTwist after t + 200 ms")


def grade_c2_9(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    grade_ignored_stop(st, tr, "change", "zero")


def grade_c2_10(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    c0, e = tr.mark("release"), tr.mark("end")
    check_no_nonzero_strict = hg.nonzero_in(tr.samples, c0, e)
    st.check("no non-zero XYTwist at all", not check_no_nonzero_strict,
             f"{len(check_no_nonzero_strict)} non-zero")
    ok, n, bad = hg.all_states(tr.states, _state("FAULT"), c0, e)
    st.check("STATE FAULT throughout", ok, f"{n} polls; first other {bad}")
    drive = [s for t, s in hg.states_in(tr.states, c0, e) if s.get("screen") == DRIVE]
    st.check("Drive notice STICK_FAULT", bool(drive)
             and all(s.get("notice") == "STICK_FAULT" for s in drive), f"{len(drive)} Drive polls")


def grade_c2_11(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    st.check("control (stick OK): the injection moves the focus", p["control_moved"],
             f"{p['control_focus']}")
    st.check("FAULT: focus unchanged", not p["fault_moved"], f"{p['fault_focus']}")
    a, e = tr.mark("keys-fault"), tr.mark("keys-fault-end")
    ok, n, bad = hg.all_states(tr.states, lambda s: s.get("screen") == p["screen"]
                               and _stick(s, "joy_key") == 0 and _stick(s, "state") == "FAULT",
                               a, e)
    st.check("FAULT: screen unchanged and joy_key 0 throughout", ok,
             f"{n} polls; first other {bad}")


def grade_c2_12(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("fault")
    for name in p["screens"]:
        a, b = tr.mark(f"on-{name}"), tr.mark(f"off-{name}")
        ok, n, bad = hg.all_states(
            tr.states, lambda s: s.get("indicator") not in (None, "NONE")
            and (s.get("indicator_text") or "").startswith("Joystick fault"), a, b)
        st.check(f"the fault indicator on {name}", ok, f"{n} polls; first other {bad}")
    last = tr.states[-1] if tr.states else None
    st.check("still FAULT after 60 s", last is not None and last[0] - t >= 60.0
             and _stick(last[1], "state") == "FAULT",
             "" if last is None else f"at +{last[0] - t:.0f} s: {_stick(last[1], 'state')}")


def grade_c2_14(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    b = tr.mark("back")
    first = hg.first_state(tr.states, lambda s: True, b)
    if not st.check("a STATE reply after the restart", first is not None, ""):
        return
    ok_at = hg.first_state(tr.states, _state("OK"), first[0])
    st.check("stick OK within 1.5 s of the first STATE reply",
             ok_at is not None and ok_at[0] - first[0] <= 1.5,
             "never" if ok_at is None else f"after {ok_at[0] - first[0]:.2f} s")
    fault = hg.first_state(tr.states, _state("FAULT"), b)
    st.check("never FAULT", fault is None, "")
    last = tr.states[-1][1]
    age = _stick(last, "xy_age_max_ms")
    st.check("xy_age_max_ms <= 200", age is not None and age <= 200, str(age))


SOAK_BANDS = {  # C2-15, in the spec's units (ms, B, counts)
    "joy.bad_samples": lambda v: v == 0,
    "joy.xy_age_max": lambda v: v <= 200,
    "time.adc_avg": lambda v: 34.5 <= v <= 35.5,
    "time.adc_max": lambda v: v <= 40,
    "mem.stk_adc": lambda v: v >= 2048,
}


def selftest_value(row: dict | None) -> float | None:
    """A self-test result's value in ms when its unit is us (time.*), else as reported."""
    if not row or row.get("value") is None:
        return None
    v = row["value"]
    return v / 1000.0 if row.get("unit") == "us" else v


def grade_c2_15(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    rows = p.get("selftest", {})
    if not st.check("the self test ran after the soak", bool(rows), p.get("selftest_note", "")):
        return
    for name, ok in SOAK_BANDS.items():
        v = selftest_value(rows.get(name))
        st.check(f"self test {name}", v is not None and ok(v), str(v))
    health = rows.get("joy.health")
    st.check("self test joy.health OK", health is not None and health.get("result") == "PASS",
             str(health and (health.get("value"), health.get("result"))))


def grade_c2_16(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("change")
    allow = p.get("one_way_s", 0.0)
    z = hg.first_sample(tr.samples, hg.is_exact_zero, t)
    st.check("output 0 by 340 ms after the stall start (+ transport)",
             z is not None and z[0] - t <= 0.340 + allow,
             "never" if z is None else f"at +{z[0] - t:.3f} s (allowance {allow:.3f})")
    f = hg.first_state(tr.states, _state("FAULT"), t)
    st.check("FAULT STALE", f is not None and _stick(f[1], "fault_reason") == "STALE",
             "never" if f is None else str(_stick(f[1], "fault_reason")))


# ---------------------------------------------------------------- scripts


def begin_c2(rig: Rig) -> None:
    rig.begin()
    s = rig.watch(2.0, until=_state("OK"))
    if not rig.st.check("set-up: STATE stick OK", s is not None,
                        str(_stick(rig.state(), "state"))):
        raise NotRun("firmware", "the stick monitor is not OK at the start (or not in STATE)")


def reboot_cleanup(rig: Rig) -> None:
    """A latched FAULT is RAM only: Restart HMI clears it. Never a calibration run."""
    if _stick(rig.state(), "state") != "FAULT":
        return
    rig.inj.pause()
    rig.restart_hmi()
    rig.wait_back()
    rig.read_cal()
    rig.centre()
    rig.watch(15.0, until=lambda s: s.get("post") == "PASS")
    s = rig.watch(3.0, until=_state("OK"))
    rig.st.check("clean-up: stick OK after the reboot", s is not None,
                 str(_stick(rig.state(), "state")))


def _mv(rig: Rig, mv: int, residual: bool) -> dict:
    begin_c2(rig)
    to_drive_by_mcb(rig)
    rig.watch(0.5)  # centred
    rig.mark("change")
    rig.inject(*rig.cal.vertical_at(mv))
    rig.watch(2.1)
    return {"residual": residual}


def s_c2_1(rig: Rig) -> dict:
    return _mv(rig, 6, False)


def s_c2_2(rig: Rig) -> dict:
    return _mv(rig, 0, True)


def _rail(rig: Rig) -> None:
    driving_forward(rig)
    rig.mark("change")
    h, v, tw = rig.cal.forward()
    rig.inject(3300, v, tw)


def s_c2_3(rig: Rig) -> dict:
    begin_c2(rig)
    _rail(rig)
    rig.watch(1.0)
    rig.mark("centred")
    rig.centre()
    rig.watch(3.1)
    return {}


def s_c2_3b(rig: Rig) -> dict:
    begin_c2(rig)
    to_drive_by_mcb(rig)
    rig.watch(0.5)
    lim = limit_mv(rig.cal.v[2])
    rig.mark("change")
    rig.inject(*rig.cal.vertical_at(lim - 20))
    rig.watch(2.0)
    rig.mark("over")
    rig.inject(*rig.cal.vertical_at(lim + 20))
    rig.watch(1.2)
    return {"limit_mv": lim}


def s_c2_4a(rig: Rig) -> dict:
    begin_c2(rig)
    driving_forward(rig)
    rig.watch(0.3)
    rig.mark("change")
    rig.inject(*rig.cal.forward(), mask=0x81, once=True)
    rig.watch(2.0)  # forward held
    rig.mark("centred")
    rig.centre()
    time.sleep(0.5)
    rig.mark("forward")
    rig.forward()
    rig.watch(1.2)
    return {}


def s_c2_4b(rig: Rig) -> dict:
    begin_c2(rig)
    driving_forward(rig)
    rig.mark("change")
    for _ in range(5):
        rig.inject(*rig.cal.forward(), mask=0x81, once=True)
        rig.watch(0.2)
    rig.watch(0.5)
    rig.mark("end")
    return {}


def s_c2_4c(rig: Rig) -> dict:
    begin_c2(rig)
    to_drive_by_mcb(rig)
    rig.watch(0.5)
    rig.mark("change")
    for _ in range(10):
        rig.inject(*rig.cal.centre(), mask=0x81, once=True)
        rig.watch(1.2)
    rig.mark("end")
    rig.watch(0.3)
    return {}


def _standing_mask(rig: Rig, mask: int) -> dict:
    begin_c2(rig)
    driving_forward(rig)
    rig.mark("change")
    rig.inject(*rig.cal.forward(), mask=mask)
    rig.watch(2.1)
    rig.centre()
    return {}


def s_c2_5(rig: Rig) -> dict:
    return _standing_mask(rig, 0x02)


def s_c2_6(rig: Rig) -> dict:
    return _standing_mask(rig, 0x40)


def s_c2_7(rig: Rig) -> dict:
    return _standing_mask(rig, 0x10)


def s_c2_8(rig: Rig) -> dict:
    begin_c2(rig)
    rig.need("banner")
    _rail(rig)
    rig.watch(2.0)
    rig.centre()
    return {}


def s_c2_9(rig: Rig) -> dict:
    begin_c2(rig)
    rig.sim_toggle("s", True)
    _rail(rig)
    t1 = watch_disables(rig, "change")
    if t1 is None:
        raise NotRun("bench", "no DISABLE reached the sim after the fault")
    rig.watch(max(0.0, t1 + 7.2 - time.monotonic()))
    return {}


def s_c2_10(rig: Rig) -> dict:
    begin_c2(rig)
    _rail(rig)
    rig.watch(1.0)
    rig.mark("release")
    rig.centre()
    rig.watch(5.0)
    rig.forward()
    rig.watch(2.0)
    rig.sim_cmd("a")  # re-enable
    rig.watch(3.0, until=hg.screen_is(DRIVE))
    rig.centre()
    rig.watch(0.5)
    rig.forward()
    rig.watch(2.0)
    rig.mark("end")
    return {}


def _keys(rig: Rig) -> list:
    """Full down, up, right, left, 0.5 s each with centre between; FOCUS after each."""
    c = rig.cal
    h, v, tw = c.centre()
    seen = [rig.focus()]
    for target in ((h, c.v[2], tw), (h, c.v[0], tw), (c.h[2], v, tw), (c.h[0], v, tw)):
        rig.inject(*target)
        rig.watch(0.5)
        rig.centre()
        rig.watch(0.5)
        seen.append(rig.focus())
    return seen


def s_c2_11(rig: Rig) -> dict:
    begin_c2(rig)
    rig.go("Settings")
    rig.watch(3.0, until=hg.screen_is("SettingsScreen"))
    control = _keys(rig)
    rig.home()
    rig.mark("fault")
    h, v, tw = rig.cal.centre()
    rig.inject(3300, v, tw)
    rig.watch(1.0, until=_state("FAULT"))
    rig.centre()
    rig.go("Settings")
    s = rig.watch(3.0, until=hg.screen_is("SettingsScreen"))
    rig.mark("keys-fault")
    fault = _keys(rig)
    rig.mark("keys-fault-end")
    return {"control_focus": control, "control_moved": len({f for f in control if f}) > 1,
            "fault_focus": fault, "fault_moved": len({f for f in fault if f}) > 1,
            "screen": (s or {}).get("screen", "SettingsScreen")}


def s_c2_12(rig: Rig) -> dict:
    begin_c2(rig)
    h, v, tw = rig.cal.centre()
    rig.mark("fault")
    rig.inject(3300, v, tw)
    rig.watch(1.0, until=_state("FAULT"))
    rig.centre()
    screens = []
    for name, go in (("Home", rig.home), ("Settings", lambda: rig.go("Settings")),
                     ("Joystick", lambda: rig.go("Joystick")),
                     ("Drive", lambda: (rig.home(), rig.sim_cmd("a")))):
        go()
        rig.watch(4.0, until=lambda s: s.get("screen") not in (None, "BootScreen"))
        rig.mark(f"on-{name}")
        rig.watch(15.0)
        rig.mark(f"off-{name}")
        screens.append(name)
    rig.sim_cmd("ok")
    rig.watch(max(0.0, 61.0 - 4 * 15.0))
    rig.st.not_verified.append("the indicator in each SHOT (STATE grades it)")
    return {"screens": screens}


def s_c2_13(rig: Rig) -> dict:
    raise NotRun("bench", NOT_RUNNABLE["C2-13"])


def no_grade(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    """For a step the bench may not run (NOT_RUNNABLE)."""


def s_c2_14(rig: Rig) -> dict:
    begin_c2(rig)
    rig.inj.pause()
    rig.mark("restart")
    rig.restart_hmi()
    rig.wait_back()
    rig.centre()  # from the remote UI's first second
    rig.mark("back")
    rig.watch(5.0)
    return {}


def s_c2_15(rig: Rig) -> dict:
    begin_c2(rig)
    to_drive_by_mcb(rig)
    end = time.monotonic() + float(rig.params.get("soak_s", SOAK_S))
    while time.monotonic() < end:
        rig.state()
        time.sleep(1.0)  # one STATE a second over the soak; the injection refreshes itself
    rig.sim_cmd("ok")
    rig.watch(3.0, until=hg.screen_is(LOCKED))
    return {"selftest_after": True}


def s_c2_16(rig: Rig) -> dict:
    begin_c2(rig)
    driving_forward(rig)
    from scenario_c4 import _stall, one_way_s
    one_way = one_way_s(rig)
    rig.mark("change")
    _stall(rig, "CADC", 1000)
    rig.watch(1.0)
    return {"one_way_s": one_way}


STEPS = {
    "C2-1": (s_c2_1, grade_mv_drives, "6 mV drives full"),
    "C2-2": (s_c2_2, grade_mv_drives, "0 mV (residual D2) drives full"),
    "C2-3": (s_c2_3, grade_c2_3, "rail while driving"),
    "C2-3b": (s_c2_3b, grade_c2_3b, "limit edge"),
    "C2-4a": (s_c2_4a, grade_c2_4a, "one bad cycle"),
    "C2-4b": (s_c2_4b, grade_c2_4b, "alternating bad cycles"),
    "C2-4c": (s_c2_4c, grade_c2_4c, "sparse bad cycles"),
    "C2-5": (s_c2_5, grade_c2_5, "failed read"),
    "C2-6": (s_c2_6, grade_c2_6, "stale X/Y"),
    "C2-7": (s_c2_7, grade_c2_7, "NaN"),
    "C2-8": (s_c2_8, grade_c2_8, "fault while driving, MCB obeys (C2b)"),
    "C2-9": (s_c2_9, grade_c2_9, "fault while driving, MCB ignores (C2b)"),
    "C2-10": (s_c2_10, grade_c2_10, "release: no auto-resume"),
    "C2-11": (s_c2_11, grade_c2_11, "stick keys silent in FAULT"),
    "C2-12": (s_c2_12, grade_c2_12, "fault indicator persists"),
    "C2-13": (s_c2_13, no_grade, "way out by touch (NOT_RUN: would rewrite the calibration)"),
    "C2-14": (s_c2_14, grade_c2_14, "boot"),
    "C2-15": (s_c2_15, grade_c2_15, "30 min soak, then the self test"),
    "C2-16": (s_c2_16, grade_c2_16, "ContinuousAdc starvation (STALL CADC)"),
}
CLEANUP = {name: reboot_cleanup for name in STEPS if name not in NOT_RUNNABLE}
SELFTEST_AFTER = {"C2-15"}
