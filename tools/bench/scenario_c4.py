"""B5f..B5j: hazard fix C4 (the ADC-side motion guard, the task watchdog) on the board
(docs/plans/hazard-c4-spec.md §8). No motors: the sim plays the MCB; the stick-injection
build (ci/sdkconfig.stick_inject); no step grades XYTwist while a SHOT runs.

| Step | What | PASS |
| --- | --- | --- |
| B5f | neutral, sim a, Drive, full forward (y > 0.5 for 1 s), sim i | every XYTwist received > 120 ms after the first IDLE MibStatus publish is 0, in 10 of 10 repeats |
| B5g | as B5f, then sim p | every XYTwist after the last publish + 2120 ms is 0; XYTwist still at >= 25 Hz in [+2.12 s, +4 s]; 3 of 3 repeats |
| B5h | sim ign 50, Drive, forward, exit hold | the sim stays ENABLED and XYTwist y > 0.5 for 5 s after the hold |
| B5i | STALL UI 300, then STALL UI 3000 | 300: XYTwist 0 by stall start + 240 ms, back after; 3000: reset, the panic names lv_task, reset reason TASK_WDT, the last 1.5 s of XYTwist before the silence all 0 |
| B5j | STALL ADC 3000 | reset; the panic names Read ADC; the sim logs XYTwist silence from the stall and its ongone at 1 s |

The MibStatus publish times come from the sim's `mib_publish` records (scripts/mcb_sim_logic.py,
added for B5f as the spec asks). "Stall start" is the mark the runner writes just before it
sends STALL; the command's one-way time (half the median PING round trip, measured before)
is allowed on top. While a STALL is in flight the remote-UI connection is busy, so the
injection is not refreshed: it lapses 300 ms after the last refresh (the real stick, at rest,
reads then). B5i/B5j need the panic on (C4 phase 2) and the serial port.
"""

from __future__ import annotations

import re
import statistics
import time

import hazard_grade as hg
from hazard_rig import HazardStep, NotRun, Rig, SerialWatch
from scenario_c1 import LOCKED, check_no_nonzero, to_drive_by_mcb

B5F_REPEATS = 10
B5G_REPEATS = 3
WDT_TRIP_RE = re.compile(r"task_wdt: Task watchdog got triggered")


def drive_y_half(rig: Rig, label: str) -> None:
    """Neutral, sim a, Drive, then full forward until XYTwist y > 0.5, held 1 s (set-up)."""
    to_drive_by_mcb(rig, label)
    rig.centre()
    time.sleep(0.5)  # neutral 300 ms first (C1 latch)
    rig.forward()
    ok = rig.wait_y(lambda y: y > 0.5, 2.0)
    if not rig.st.check(f"set-up {label}: XYTwist y > 0.5", ok, ""):
        raise NotRun("bench", "the stick did not drive in the set-up")
    time.sleep(1.05)  # y > 0.5 for 1 s


def one_way_s(rig: Rig) -> float:
    rtts = []
    for _ in range(3):
        t = time.monotonic()
        rig.hmi.command("PING")
        rtts.append(time.monotonic() - t)
    return statistics.median(rtts) / 2.0


# ---------------------------------------------------------------- graders


def check_driving_before(st: HazardStep, tr: hg.Trace, t: float, tag: str) -> None:
    win = hg.window(tr.samples, t - 1.0, t)
    st.check(f"{tag} set-up: XYTwist y > 0.5 for the 1 s before",
             bool(win) and all(s[2] > 0.5 for s in win), f"{len(win)} samples")


def grade_b5f(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    passed = 0
    for i in range(1, B5F_REPEATS + 1):
        m = tr.mark(f"idle-{i}")
        check_driving_before(st, tr, m, f"repeat {i}")
        pub = hg.publishes(tr.events, m, m + 2.0, "IDLE")
        if not pub:
            st.check(f"repeat {i}: an IDLE MibStatus published", False, "none in 2 s")
            continue
        t_idle = pub[0]["mono"]
        bad = hg.nonzero_in(tr.samples, t_idle + 0.120, t_idle + 2.0)
        n = len(hg.window(tr.samples, t_idle + 0.120, t_idle + 2.0))
        ok = n > 0 and not bad
        passed += ok
        st.check(f"repeat {i}: every XYTwist > 120 ms after the first IDLE publish is 0", ok,
                 f"{n} samples, {len(bad)} non-zero"
                 + (f", first at +{bad[0][0] - t_idle:.3f} s" if bad else ""))
    st.check(f"{B5F_REPEATS} of {B5F_REPEATS} repeats", passed == B5F_REPEATS,
             f"{passed} of {B5F_REPEATS}")


def grade_b5g(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    passed = 0
    for i in range(1, B5G_REPEATS + 1):
        m = tr.mark(f"pause-{i}")
        check_driving_before(st, tr, m, f"repeat {i}")
        pause = hg.first_event(tr.events, lambda e: e.get("ev") == "pause", m)
        last = hg.publishes(tr.events, m - 5.0, pause["mono"] if pause else m + 1.0)
        if pause is None or not last:
            st.check(f"repeat {i}: the pause and the last publish before it", False,
                     f"pause {pause is not None}, publishes {len(last)}")
            continue
        t_last = last[-1]["mono"]
        zero_ok = not hg.nonzero_in(tr.samples, t_last + 2.12, t_last + 4.0)
        hz = hg.rate_hz(tr.samples, t_last + 2.12, t_last + 4.0)
        ok = zero_ok and hz >= 25.0
        passed += ok
        st.check(f"repeat {i}: zero after the last publish + 2120 ms, >= 25 Hz in [+2.12, +4] s",
                 ok, f"non-zero: {not zero_ok}, {hz:.1f} Hz")
    st.check(f"{B5G_REPEATS} of {B5G_REPEATS} repeats", passed == B5G_REPEATS,
             f"{passed} of {B5G_REPEATS}")


def grade_b5h(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    h, hr = tr.mark("hold"), tr.mark("held")
    win = hg.window(tr.samples, hr, hr + 5.0)
    st.check("XYTwist y > 0.5 for 5 s after the hold", bool(win) and all(s[2] > 0.5 for s in win),
             f"{len(win)} samples, {sum(1 for s in win if s[2] <= 0.5)} at or under 0.5")
    left = [e for e in hg.events_in(tr.events, h, hr + 5.0)
            if e.get("ev") in ("mib_state", "mib_publish") and e.get("state") != "ENABLED"]
    st.check("the sim stays ENABLED", not left, str(left[:1]))
    st.check("the DISABLEs were ignored, not lost",
             all(e.get("action") == "ignored"
                 for e in hg.drive_commands(tr.events, h, hr + 5.0, "DISABLE")), "")


def panic_names(text: str, task: str) -> bool:
    """A task-watchdog report in the serial log that lists `task` among those that did not
    reset the watchdog."""
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if WDT_TRIP_RE.search(line) and any(task in nxt for nxt in lines[i + 1:i + 8]):
            return True
    return False


def grade_b5i(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    s, e, f = tr.mark("stall-300"), tr.mark("stalled-300"), tr.mark("forward")
    allow = p.get("one_way_s", 0.0)
    z = hg.first_sample(tr.samples, hg.is_exact_zero, s)
    st.check("300: XYTwist 0 by stall start + 240 ms",
             z is not None and z[0] - s <= 0.240 + allow,
             "never" if z is None else f"first zero at +{z[0] - s:.3f} s (allowance {allow:.3f})")
    if z is not None:
        check_no_nonzero(st, "300: zero from then to the stall's end", tr, z[0], e)
    back = hg.first_sample(tr.samples, hg.y_positive, f)
    st.check("300: back after (centred 0.5 s, forward: y > 0 within 1 s)",
             back is not None and back[0] - f <= 1.0,
             "never" if back is None else f"after {back[0] - f:.2f} s")
    s3 = tr.mark("stall-3000")
    text = p.get("serial_text", "")
    st.check("3000: reset, the panic names lv_task", panic_names(text, "lv_task"), "")
    st.check("3000: reset reason TASK_WDT", p.get("reset_reason") == "TASK_WDT",
             str(p.get("reset_reason")))
    last = hg.silence_after(tr.samples, s3, 1.0)
    if last is None:
        st.check("3000: XYTwist before the silence", False, "no sample after the stall")
        return
    check_no_nonzero(st, "3000: the last 1.5 s of XYTwist before the silence all 0", tr,
                     last[0] - 1.5, last[0] + 1e-6)


def grade_b5j(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    s = tr.mark("stall")
    text = p.get("serial_text", "")
    st.check("reset; the panic names Read ADC", panic_names(text, "Read ADC"), "")
    last = hg.silence_after(tr.samples, s, 1.0)
    st.check("XYTwist silence from the stall", last is not None and last[0] - s <= 0.1
             + p.get("one_way_s", 0.0),
             "no sample" if last is None else f"last sample at +{last[0] - s:.3f} s")
    gone = hg.first_event(tr.events, lambda e: e.get("ev") == "hmi_gone", s)
    st.check("the sim's ongone at 1 s", gone is not None and last is not None
             and 1.0 <= gone["mono"] - last[0] <= 1.5,
             "no hmi_gone" if gone is None or last is None else
             f"{gone['mono'] - last[0]:.2f} s after the last sample")


# ---------------------------------------------------------------- scripts


def s_b5f(rig: Rig) -> dict:
    rig.begin()
    for i in range(1, B5F_REPEATS + 1):
        drive_y_half(rig, f"enter-{i}")
        rig.mark(f"idle-{i}")
        rig.sim_cmd("i")
        rig.watch(2.0, until=hg.screen_is(LOCKED))
        rig.centre()
        rig.watch(1.0)
    return {}


def s_b5g(rig: Rig) -> dict:
    rig.begin()
    for i in range(1, B5G_REPEATS + 1):
        drive_y_half(rig, f"enter-{i}")
        rig.mark(f"pause-{i}")
        rig.sim_cmd("p")
        rig.watch(4.5)
        rig.sim_cmd("r")
        rig.sim_cmd("ok")
        rig.centre()
        rig.watch(3.0, until=hg.screen_is(LOCKED))
    return {}


def s_b5h(rig: Rig) -> dict:
    rig.begin()
    rig.sim_count("ign", 50)
    drive_y_half(rig, "enter")
    rig.mark("hold")
    rig.hold_button()
    rig.mark("held")
    rig.watch(5.2)
    return {}


def _stall(rig: Rig, target: str, ms: int) -> None:
    try:
        rig.hmi.stall(target, ms)
    except rig._hmi_ui.BenchVerbError as e:
        if e.not_in_firmware:
            raise NotRun("firmware", f"STALL {target}: {e.reply}") from None
        raise


def _stall_reset(rig: Rig, target: str, ms: int) -> None:
    """A stall the watchdog turns into a reset: no answer comes back."""
    try:
        _stall(rig, target, ms)
    except NotRun:
        raise
    except Exception:  # noqa: BLE001 - the board resets mid-command
        pass
    rig._drop_hmi()


def s_b5i(rig: Rig) -> dict:
    if not rig.port:
        raise NotRun("bench", "B5i reads the panic from the serial log: give the board's port")
    rig.begin()
    drive_y_half(rig, "enter")
    one_way = one_way_s(rig)
    rig.mark("stall-300")
    _stall(rig, "UI", 300)
    rig.mark("stalled-300")
    rig.centre()
    rig.watch(0.6)
    rig.mark("forward")
    rig.forward()
    rig.watch(1.2)
    watch = SerialWatch(rig.port, 120.0)
    rig.mark("stall-3000")
    _stall_reset(rig, "UI", 3000)
    rig.inj.pause()
    rig.wait_back()
    reason = rig.state().get("reset_reason")
    return {"one_way_s": one_way, "serial_text": watch.stop(), "reset_reason": reason}


def s_b5j(rig: Rig) -> dict:
    if not rig.port:
        raise NotRun("bench", "B5j reads the panic from the serial log: give the board's port")
    rig.begin()
    drive_y_half(rig, "enter")
    one_way = one_way_s(rig)
    watch = SerialWatch(rig.port, 120.0)
    rig.mark("stall")
    _stall_reset(rig, "ADC", 3000)
    rig.inj.pause()
    rig.wait_back()
    return {"one_way_s": one_way, "serial_text": watch.stop()}


STEPS = {
    "B5f": (s_b5f, grade_b5f, "MCB state, ADC side: XYTwist 0 within 120 ms of IDLE, 10 repeats"),
    "B5g": (s_b5g, grade_b5g, "stale MibStatus: XYTwist 0 after 2120 ms, still flowing, 3 repeats"),
    "B5h": (s_b5h, grade_b5h, "G4 holds: ign 50, exit hold, the stick keeps driving"),
    "B5i": (s_b5i, grade_b5i, "STALL UI 300 and 3000 (serial)"),
    "B5j": (s_b5j, grade_b5j, "STALL ADC 3000 (serial)"),
}
NEEDS_SERIAL = {"B5i", "B5j"}
