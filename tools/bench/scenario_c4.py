"""B5f..B5j: hazard fix C4 (the ADC-side motion guard, the task watchdog) on the board
(docs/plans/hazard-c4-spec.md §8). No motors: the sim plays the MCB; the stick-injection
build (ci/sdkconfig.stick_inject); no step grades XYTwist while a SHOT runs.

| Step | What | PASS |
| --- | --- | --- |
| B5f | neutral, sim a, Drive, full forward (y > 0.5 for 1 s), sim i | every XYTwist received > 120 ms after the first IDLE MibStatus publish is 0, in 10 of 10 repeats |
| B5g | as B5f, then sim p | every XYTwist after the last publish + 2120 ms is 0; XYTwist still at >= 25 Hz in [+2.12 s, +4 s]; 3 of 3 repeats |
| B5h | sim ign 50, Drive, forward, exit hold | the sim stays ENABLED and XYTwist y > 0.5 for 5 s after the hold |
| B5i | STALL UI 300, then STALL UI 3000 | 300: XYTwist 0 published by stall start + 240 ms (board time; the network reported), back after; 3000: reset, the panic names lv_task, reset reason TASK_WDT, the last 1.5 s of XYTwist before the silence all 0 |
| B5j | STALL ADC 3000 | reset; the panic names Read ADC; the sim logs XYTwist silence from the stall and its ongone at 1 s |

The MibStatus publish times come from the sim's `mib_publish` records (scripts/mcb_sim_logic.py,
added for B5f as the spec asks). "Stall start" is the mark the runner writes just before it
sends STALL; the command's one-way time (half the median PING round trip, measured before)
is allowed on top. While a STALL is in flight the remote-UI connection is busy, so the
injection is not refreshed: it lapses 300 ms after the last refresh (the real stick, at rest,
reads then). B5i/B5j need the serial port. They grade the watchdog phase the tree builds
(hazard-c4-spec.md §4.2): phase 1 (CONFIG_ESP_TASK_WDT_PANIC off, report only) wants the
report naming the stalled task and no reset; phase 2 (panic on) wants the reset. The STALL hook
answers at once and the task stalls itself at its next cycle, so a stall ends `ms` after its
start mark (plus the command's one-way time), not when the reply comes.
"""

from __future__ import annotations

import datetime
import pathlib
import re
import statistics
import time

import hazard_grade as hg
from hazard_rig import HazardStep, NotRun, Rig, SerialWatch
from scenario_c1 import LOCKED, check_no_nonzero, to_drive_by_mcb, wait_neutral_latch

B5F_REPEATS = 10
B5G_REPEATS = 3
WDT_TRIP_RE = re.compile(r"task_wdt: Task watchdog got triggered")
PANIC_ON_RE = re.compile(r"^CONFIG_ESP_TASK_WDT_PANIC=y\s*$", re.M)


def watchdog_panics(tree: pathlib.Path) -> bool:
    """Phase 2 of the task watchdog (a trip panics and resets): the tree's sdkconfig.defaults
    sets CONFIG_ESP_TASK_WDT_PANIC=y. Phase 1 (off) only reports (hazard-c4-spec.md §4.2)."""
    path = pathlib.Path(tree) / "sdkconfig.defaults"
    return path.exists() and PANIC_ON_RE.search(path.read_text(encoding="utf-8")) is not None


def drive_y_half(rig: Rig, label: str) -> None:
    """Neutral, sim a, Drive, then full forward until XYTwist y > 0.5, held 1 s (set-up)."""
    start = datetime.datetime.now()
    to_drive_by_mcb(rig, label)
    rig.centre()
    wait_neutral_latch(rig)  # the gate open and the 300 ms neutral latch set (C1)
    rig.setup_span(start)  # set-up, ungraded: a lapse in it does not void the step
    rig.forward()
    ok = rig.wait_y(lambda y: y > 0.5, 2.0)
    if not rig.st.check(f"set-up {label}: XYTwist y > 0.5", ok, ""):
        raise NotRun("bench", "the stick did not drive in the set-up")
    time.sleep(1.05)  # y > 0.5 for 1 s


ONE_WAY_PINGS = 7


def one_way_s(rig: Rig) -> float:
    """Half the median remote-UI PING round trip (perf_counter: whatever the interpreter's
    monotonic clock; 3 PINGs on a 15.6 ms clock gave a median of 0 on 2026-10-11)."""
    rtts = []
    for _ in range(ONE_WAY_PINGS):
        t = time.perf_counter()
        rig.hmi.command("PING")
        rtts.append(time.perf_counter() - t)
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


# B5i's 240 ms is measured at the board's publish (owner, 2026-10-11); the network's share is
# reported, not graded. Neither the serial log nor XYTwist carries the board's time, so the
# board's side is estimated from the sim's (PC) clock with the measured one-way delay w (the
# median remote-UI round trip / 2, one_way_s, taken just before the stalls): the stall starts on
# the board at mark + w (the STALL command's way in) and a sample published at P reaches the sim
# at P + w, so P - start = (arrival - mark) - 2 w. What the network adds beyond its median stays
# in the figure (a late zero is graded late, never early).
STALL_ZERO_WITHIN_S = 0.240


def board_after(arrival: float, mark: float, one_way: float) -> float:
    """The board's publish time of a sample after its stall started (see above)."""
    return arrival - mark - 2.0 * one_way


def check_zero_by(st: HazardStep, tag: str, z, s: float, one_way: float) -> None:
    board = None if z is None else board_after(z[0], s, one_way)
    st.check(f"{tag}: XYTwist 0 published by stall start + {STALL_ZERO_WITHIN_S * 1000:.0f} ms "
             f"(board time)", board is not None and board <= STALL_ZERO_WITHIN_S,
             "never" if z is None else
             f"board +{board:.3f} s; at the sim +{z[0] - s:.3f} s, one-way {one_way:.3f} s")
    if board is not None:  # the network's share is reported, not graded
        st.record(f"b5i_{tag}_network_s", round(z[0] - s - board, 3))


def grade_b5i(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    s, f = tr.mark("stall-300"), tr.mark("forward")
    allow = p.get("one_way_s", 0.0)
    e = s + 0.300 + 2.0 * allow  # the stall's end, at the sim: the task stalls itself 300 ms
    z = hg.first_sample(tr.samples, hg.is_exact_zero, s)
    check_zero_by(st, "300", z, s, allow)
    if z is not None:
        check_no_nonzero(st, "300: zero from then to the stall's end", tr, z[0], e)
    back = hg.first_sample(tr.samples, hg.y_positive, f)
    st.check("300: back after (centred 0.5 s, forward: y > 0 within 1 s)",
             back is not None and back[0] - f <= 1.0,
             "never" if back is None else f"after {back[0] - f:.2f} s")
    s3 = tr.mark("stall-3000")
    text = p.get("serial_text", "")
    if not p.get("panic", True):
        grade_stall_report(st, tr, p, "3000", s3, "lv_task")
        z3 = hg.first_sample(tr.samples, hg.is_exact_zero, s3)
        check_no_nonzero(st, "3000: XYTwist 0 from stall start + 240 ms to the stall's end", tr,
                         s3 + STALL_ZERO_WITHIN_S + 2.0 * allow, s3 + 3.0)
        check_zero_by(st, "3000", z3, s3, allow)
        return
    st.check("3000: reset, the panic names lv_task", panic_names(text, "lv_task"), "")
    st.check("3000: reset reason TASK_WDT", p.get("reset_reason") == "TASK_WDT",
             str(p.get("reset_reason")))
    last = hg.silence_after(tr.samples, s3, 1.0)
    if last is None:
        st.check("3000: XYTwist before the silence", False, "no sample after the stall")
        return
    check_no_nonzero(st, "3000: the last 1.5 s of XYTwist before the silence all 0", tr,
                     last[0] - 1.5, last[0] + 1e-6)


def grade_stall_report(st: HazardStep, tr: hg.Trace, p: dict, tag: str, s: float,
                       task: str) -> None:
    """Phase 1 (report only): the watchdog reports the stalled task, and nothing resets."""
    text = p.get("serial_text", "")
    st.check(f"{tag}: phase 1: the task watchdog reports {task}", panic_names(text, task), "")
    st.check(f"{tag}: phase 1: no reset (reset reason unchanged)",
             p.get("reset_reason") == p.get("reset_reason_before"),
             f"{p.get('reset_reason_before')} -> {p.get('reset_reason')}")
    after = [x for x in tr.samples if x[0] >= s + 3.5]
    st.check(f"{tag}: phase 1: XYTwist flows after the stall", len(after) > 0,
             f"{len(after)} samples after the stall")


def grade_b5j(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    s = tr.mark("stall")
    text = p.get("serial_text", "")
    if not p.get("panic", True):
        grade_stall_report(st, tr, p, "3000", s, "Read ADC")
        last = hg.silence_after(tr.samples, s, 1.0)
        st.check("XYTwist silence from the stall", last is not None and last[0] - s <= 0.1
                 + p.get("one_way_s", 0.0),
                 "no sample" if last is None else f"last sample at +{last[0] - s:.3f} s")
        back = None if last is None else hg.first_sample(tr.samples, lambda x: True,
                                                          last[0] + 1e-6)
        st.check("XYTwist back after the 3 s stall", back is not None and last is not None
                 and 2.5 <= back[0] - last[0] <= 4.0,
                 "never" if back is None or last is None else
                 f"{back[0] - last[0]:.2f} s of silence")
        return
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
    rig.watch(0.4)  # the stall runs on the board; the hook answered at once
    rig.centre()
    rig.watch(0.6)
    rig.mark("forward")
    rig.forward()
    rig.watch(1.2)
    panic = watchdog_panics(rig.tree)
    before = rig.state().get("reset_reason")
    watch = SerialWatch(rig.port, 120.0)
    rig.mark("stall-3000")
    if panic:
        _stall_reset(rig, "UI", 3000)
        rig.inj.pause()
        rig.wait_back()
    else:
        _stall(rig, "UI", 3000)
        rig.watch(4.0)  # phase 1: the watchdog reports at 2 s; nothing resets
    reason = rig.state().get("reset_reason")
    return {"one_way_s": one_way, "serial_text": watch.stop(), "reset_reason": reason,
            "reset_reason_before": before, "panic": panic}


def s_b5j(rig: Rig) -> dict:
    if not rig.port:
        raise NotRun("bench", "B5j reads the panic from the serial log: give the board's port")
    rig.begin()
    drive_y_half(rig, "enter")
    one_way = one_way_s(rig)
    panic = watchdog_panics(rig.tree)
    before = rig.state().get("reset_reason")
    watch = SerialWatch(rig.port, 120.0)
    rig.mark("stall")
    if panic:
        _stall_reset(rig, "ADC", 3000)
        rig.inj.pause()
        rig.wait_back()
    else:
        _stall(rig, "ADC", 3000)
        rig.watch(4.5)  # phase 1: silence while the stick task stalls, then XYTwist again
    reason = rig.state().get("reset_reason")
    return {"one_way_s": one_way, "serial_text": watch.stop(), "reset_reason": reason,
            "reset_reason_before": before, "panic": panic}


STEPS = {
    "B5f": (s_b5f, grade_b5f, "MCB state, ADC side: XYTwist 0 within 120 ms of IDLE, 10 repeats"),
    "B5g": (s_b5g, grade_b5g, "stale MibStatus: XYTwist 0 after 2120 ms, still flowing, 3 repeats"),
    "B5h": (s_b5h, grade_b5h, "G4 holds: ign 50, exit hold, the stick keeps driving"),
    "B5i": (s_b5i, grade_b5i, "STALL UI 300 and 3000 (serial)"),
    "B5j": (s_b5j, grade_b5j, "STALL ADC 3000 (serial)"),
}
NEEDS_SERIAL = {"B5i", "B5j"}
