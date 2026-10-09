"""BENCH-015..: the hazard bench steps without a board (run_bench.py selftest runs them).

Each step's grader is run on two kinds of hand-written trace: one built from the spec's
pass criteria (it must grade PASS) and one that breaks one criterion (that check must fail,
by name). The traces are written from the specs (hazard-c1-spec.md §6, hazard-c3-spec.md
§7, hazard-c4-spec.md §8, hazard-c2-spec.md §9), never recorded from a board or from the
graders. Also: the step plan (--hazard, retirements, names), run_bench's sole-sim proof and
B2 wiring, boot_check's data-driven markers, the injector and the sim child's sample file.
Stdlib only.
"""

from __future__ import annotations

import contextlib
import pathlib
import sys
import tempfile
import threading
import time
from typing import Callable

import boot_check
import common
import hazard_grade as hg
import hazard_rig
import hazard_steps
import run_bench
import scenario_c1 as c1
import scenario_c2 as c2
import scenario_c3 as c3
import scenario_c4 as c4
import sim_child

DT = 0.035  # one ADC cycle, as measured (C4 §2.4)


def expect(what: str, got: object, want: object) -> None:
    if got != want:
        raise AssertionError(f"{what}: got {got!r}, want {want!r}")


@contextlib.contextmanager
def quiet():
    saved = common.log
    common.log = lambda message: None
    try:
        yield
    finally:
        common.log = saved


# ---------------------------------------------------------------- trace builder


class TB:
    """A hand-written trace: samples, STATE polls, sim events and marks on one clock."""

    def __init__(self) -> None:
        self.tr = hg.Trace()

    def mark(self, label: str, t: float) -> "TB":
        self.tr.marks[label] = t
        self.tr.events.append({"mono": t, "ev": "mark", "label": label})
        return self

    def stream(self, t0: float, t1: float, y: float | Callable[[float], float] = 0.0,
               x: float = 0.0, tw: float = 0.0, dt: float = DT) -> "TB":
        k = 0
        while (t := round(t0 + k * dt, 4)) < t1 - 1e-6:
            yy = y(t) if callable(y) else y
            self.tr.samples.append((t, x, yy, tw, 0))
            k += 1
        return self

    def polls(self, t0: float, t1: float, every: float = 0.1, **fields: object) -> "TB":
        k = 0
        while (t := round(t0 + k * every, 4)) < t1 - 1e-6:
            k += 1
            state = {"screen": "LockedScreen", "phase": "LOCKED", "notice": "NONE",
                     "banner": "NONE",
                     "menu_open": False, "calibrating": False, "post": "PASS",
                     "post_check": None, "indicator": "NONE", "indicator_text": "",
                     "reset_reason": "SW", "stick": None}
            for key, v in fields.items():
                state[key] = v(t) if callable(v) else v
            self.tr.states.append((t, state))
        return self

    def ev(self, t: float, ev: str, **kw: object) -> "TB":
        self.tr.events.append({"mono": t, "ev": ev, **kw})
        return self

    def dc(self, t: float, request: str, profile: str = "NORMAL",
           action: str = "applied") -> "TB":
        return self.ev(t, "drive_command", request=request, profile=profile, action=action)

    def build(self) -> hg.Trace:
        self.tr.samples.sort()
        self.tr.states.sort(key=lambda r: r[0])
        self.tr.events.sort(key=lambda e: e["mono"])
        return self.tr


def graded(grade: Callable, tr: hg.Trace, p: dict | None = None) -> hazard_rig.HazardStep:
    with quiet(), tempfile.TemporaryDirectory() as tmp:
        st = hazard_rig.HazardStep("x", pathlib.Path(tmp))
        grade(st, tr, dict(p or {}))
    return st


def passes(what: str, grade: Callable, tr: hg.Trace, p: dict | None = None) -> None:
    st = graded(grade, tr, p)
    if st.problems:
        raise AssertionError(f"{what}: the spec's pass trace failed: {st.problems}")


def fails(what: str, grade: Callable, tr: hg.Trace, check: str, p: dict | None = None) -> None:
    st = graded(grade, tr, p)
    failed = [c["check"] for c in st.checks if not c["ok"]]
    if not any(check in name for name in failed):
        raise AssertionError(f"{what}: wanted a failed check containing {check!r}; "
                             f"failed: {failed}")


def with_(fn: Callable[[TB], TB], extra: Callable[[TB], object]) -> hg.Trace:
    tb = fn(TB())
    extra(tb)
    return tb.build()


# ---------------------------------------------------------------- C1


def tr_b1(tb: TB, drive_at: float = 101.0) -> TB:
    return (tb.mark("enter", 100.0).polls(99.0, drive_at)
            .polls(drive_at, 104.5, screen="DriveScreen", phase="DRIVING",
                   notice="CENTRE_FIRST").stream(99.0, 104.5))


def t_b1() -> None:
    passes("B5''-1", c1.grade_b1, tr_b1(TB()).build())
    fails("Drive late", c1.grade_b1, tr_b1(TB(), drive_at=103.5).build(), "within 3.0 s")
    fails("motion after Drive", c1.grade_b1,
          with_(tr_b1, lambda tb: tb.tr.samples.append((102.0, 0.0, 0.4, 0.0, 0))),
          "no non-zero XYTwist for 3.0 s")
    fails("an ENABLE", c1.grade_b1, with_(tr_b1, lambda tb: tb.dc(100.5, "ENABLE")),
          "no DriveCommand")
    fails("another notice", c1.grade_b1,
          with_(tr_b1, lambda tb: tb.tr.states.append((102.0, {"notice": "NONE"}))),
          "CENTRE_FIRST")


def t_b1_gate_shut() -> None:
    def b(tb: TB, gate_notice: str = "NONE", later: str = "CENTRE_FIRST") -> TB:
        # DriveScreen at 101.0; the fade ends and the gate opens 0.39 s later (C1 §3.3:
        # GATE_SHUT has no text, so the notice is NONE until then)
        return (tb.mark("enter", 100.0).polls(99.0, 101.0)
                .polls(101.0, 101.4, screen="DriveScreen", phase="DRIVING", notice=gate_notice,
                       hold_reason="GATE_SHUT")
                .polls(101.4, 104.5, screen="DriveScreen", phase="DRIVING", notice=later,
                       hold_reason="CENTRE_FIRST").stream(99.0, 104.5))
    passes("B5''-1: NONE while GATE_SHUT, then CENTRE_FIRST", c1.grade_b1, b(TB()).build())
    fails("B5''-1: NONE once the gate is open", c1.grade_b1, b(TB(), later="NONE").build(),
          "CENTRE_FIRST")
    fails("B5''-1: another notice while GATE_SHUT", c1.grade_b1,
          b(TB(), gate_notice="STOPPING").build(), "CENTRE_FIRST")


def t_hold_polls() -> None:
    sent: list[str] = []

    class Hmi:
        def command(self, text):
            sent.append(text)
            return "OK"
    with quiet(), tempfile.TemporaryDirectory() as tmp:
        st = hazard_rig.HazardStep("x", pathlib.Path(tmp))
        rig = hazard_rig.Rig("1.2.3.4", common.REPO, pathlib.Path(tmp), st, set(), proven="p")
        rig.hmi = Hmi()
        rig.state = lambda: sent.append("STATE") or {}
        rig.hold_button(500)
    expect("BTN 1, STATE polled through the hold, BTN 0",
           (sent[0], sent[-1], sent[1:-1].count("STATE") >= 4, set(sent[1:-1])),
           ("BTN 1", "BTN 0", True, {"STATE"}))


def t_b1bc() -> None:
    def b(tb: TB) -> TB:
        return tb.mark("centre", 200.0).mark("forward", 200.2).stream(199.0, 202.5)
    passes("B5''-1b", c1.grade_b1b, b(TB()).build())
    fails("B5''-1b motion", c1.grade_b1b,
          with_(b, lambda tb: tb.tr.samples.append((201.0, 0, 1.0, 0, 0))), "no non-zero")

    def c(tb: TB, start: float = 300.25, every: int = 1) -> TB:
        tb.mark("forward", 300.0).stream(299.0, start)
        n = [0]

        def y(t: float) -> float:
            n[0] += 1
            return 1.0 if n[0] % every == 0 else 0.0
        return tb.stream(start, 302.6, y)
    passes("B5''-1c", c1.grade_b1c, c(TB()).build())
    fails("B5''-1c late", c1.grade_b1c, c(TB(), start=300.4).build(), "within 0.3 s")
    fails("B5''-1c sparse", c1.grade_b1c, c(TB(), every=2).build(), ">= 90 %")


def tr_stop(tb: TB, label: str, t: float, locked: float = 1.0) -> TB:
    return (tb.mark(label, t).polls(t - 1.0, t + locked, screen="DriveScreen")
            .polls(t + locked, t + 2.6).stream(t - 1.0, t + 0.3, 1.0).stream(t + 0.3, t + 2.6)
            .dc(t + 0.4, "DISABLE"))


def t_b2_b3() -> None:
    passes("B5''-2", c1.grade_b2, tr_stop(TB(), "stop-i", 400.0).build())
    fails("Locked late", c1.grade_b2, tr_stop(TB(), "stop-i", 400.0, locked=2.0).build(),
          "within 1.5 s")
    fails("two DISABLEs", c1.grade_b2,
          with_(lambda tb: tr_stop(tb, "stop-i", 400.0), lambda tb: tb.dc(401.0, "DISABLE")),
          "exactly 1 DISABLE")
    fails("motion after 0.6 s", c1.grade_b2,
          with_(lambda tb: tr_stop(tb, "stop-i", 400.0),
                lambda tb: tb.tr.samples.append((400.8, 0, 1.0, 0, 0))), "mark + 0.6 s")
    both = lambda tb: tr_stop(tr_stop(tb, "stop-e", 500.0), "stop-z", 520.0)  # noqa: E731
    passes("B5''-3", c1.grade_b3, both(TB()).build())
    fails("B5''-3 z", c1.grade_b3, with_(both, lambda tb: tb.dc(520.5, "ENABLE")),
          "stop-z: exactly 1 DISABLE and 0 ENABLE")


def tr_b4(tb: TB) -> TB:
    return (tb.mark("pause", 500.0).polls(499.0, 502.5, screen="DriveScreen")
            .polls(502.5, 509.3).stream(499.0, 502.2, 1.0).stream(502.2, 509.3)
            .dc(502.4, "DISABLE").mark("resume", 504.0))


def t_b4() -> None:
    passes("B5''-4", c1.grade_b4, tr_b4(TB()).build())
    fails("Drive after r", c1.grade_b4,
          with_(tr_b4, lambda tb: tb.polls(506.0, 506.1, screen="DriveScreen")), "Locked for 5 s")
    fails("no DISABLE", c1.grade_b4,
          with_(tr_b4, lambda tb: tb.tr.events.remove(next(e for e in tb.tr.events
                                                           if e["ev"] == "drive_command"))),
          ">= 1 DISABLE")

    def b(tb: TB) -> TB:
        return (tb.mark("pause", 600.0).polls(599.0, 602.4, screen="DriveScreen")
                .polls(602.4, 605.5).stream(599.0, 602.2, 1.0).stream(602.2, 608.8)
                .mark("resume", 604.0)
                .polls(605.5, 608.8, screen="DriveScreen", notice="CENTRE_FIRST"))
    passes("B5''-4b", c1.grade_b4b, b(TB()).build())
    fails("B5''-4b motion", c1.grade_b4b,
          with_(b, lambda tb: tb.tr.samples.append((606.0, 0, 1.0, 0, 0))),
          "no non-zero XYTwist for 3.0 s")


def tr_b5(tb: TB, locked: float = 702.2) -> TB:
    return (tb.mark("exit", 700.0).polls(699.0, locked, screen="DriveScreen")
            .polls(locked, 704.3).dc(701.6, "DISABLE"))


def t_b5() -> None:
    passes("B5''-5", c1.grade_b5, tr_b5(TB()).build())
    fails("Locked late", c1.grade_b5, tr_b5(TB(), locked=702.8).build(), "within 1.0 s")
    fails("too many DISABLEs", c1.grade_b5,
          with_(tr_b5, lambda tb: [tb.dc(702.0 + i * 0.25, "DISABLE") for i in range(3)]),
          "<= 3 DISABLEs")


def tr_b6(tb: TB, t1: float = 801.6, label: str = "exit", output: str = "drives",
          fault_at: float = 5.1, drop: int | None = None) -> TB:
    tb.mark(label, t1 - 1.6)
    for k in range(20):
        if k != drop:
            tb.dc(t1 + k * 0.25, "DISABLE", action="refused")
    tb.dc(t1 + 5.75, "DISABLE", action="refused").dc(t1 + 6.75, "DISABLE", action="refused")
    tb.polls(t1 - 1.0, t1 + 7.25, screen="DriveScreen", phase="EXITING",
             notice=lambda t: "MCB_DID_NOT_STOP" if t >= t1 + fault_at else
             ("STOPPING" if t >= t1 + 0.1 else "NONE"))
    return tb.stream(799.0, t1 + 7.25, 1.0 if output == "drives" else 0.0)


def t_b6_b6b_b8() -> None:
    passes("B5''-6", c1.grade_b6, tr_b6(TB()).build())
    fails("fault early", c1.grade_b6, tr_b6(TB(), fault_at=4.9).build(), "MCB_DID_NOT_STOP")
    fails("a lost re-send", c1.grade_b6, tr_b6(TB(), drop=8).build(), "every gap <= 0.4 s")
    fails("the stick stopped", c1.grade_b6, tr_b6(TB(), output="zero").build(), ">= 90 %")

    def b(tb: TB, extra: float | None = None) -> TB:
        tr_b6(tb)
        off = 801.6 + 7.3
        tb.mark("s-off", off).dc(off + 0.8, "DISABLE").polls(off, off + 1.0, screen="DriveScreen")
        tb.polls(off + 1.0, off + 4.0, notice="NONE")
        if extra:
            tb.dc(off + 1.0 + extra, "DISABLE")
        return tb
    passes("B5''-6b", c1.grade_b6b, b(TB()).build())
    fails("B5''-6b late DISABLE", c1.grade_b6b, b(TB(), extra=2.5).build(),
          "none from Locked + 2 s")

    def e(tb: TB, menu: bool = True) -> TB:
        tr_b6(tb)
        off = 801.6 + 7.3
        return tb.mark("s-off", off).polls(off + 1.0, off + 3.0, menu_open=menu)
    passes("B5''-8", c1.grade_b8, e(TB()).build())
    fails("B5''-8 no menu", c1.grade_b8, e(TB(), menu=False).build(), "menu open")


def t_b7() -> None:
    def b(tb: TB, n: int = 5) -> TB:
        tb.mark("exit", 900.0).polls(899.0, 902.6, screen="DriveScreen").polls(902.6, 904.0)
        for k in range(n):
            tb.dc(901.6 + 0.25 * k, "DISABLE", action="dropped" if k < 3 else "applied")
        return tb
    passes("B5''-7", c1.grade_b7, b(TB()).build())
    fails("B5''-7 three", c1.grade_b7, b(TB(), n=3).build(), ">= 4 DISABLEs")


def t_b9_b10() -> None:
    def b9(tb: TB, cal_until: float = 1005.2) -> TB:
        return (tb.mark("enable", 1000.0).mark("cancelled", 1007.2)
                .polls(999.0, 1007.2, screen="JoystickScreen",
                       calibrating=lambda t: t < cal_until)
                .polls(1008.5, 1009.0, screen="DriveScreen").stream(999.0, 1009.0))
    passes("B5''-9", c1.grade_b9, b9(TB()).build())
    fails("B5''-9 run ended", c1.grade_b9, b9(TB(), cal_until=1003.0).build(), "calibrating")

    def b10(tb: TB) -> TB:
        return (tb.mark("enter", 1100.0).mark("forward", 1102.0).polls(1099.0, 1101.5)
                .polls(1101.5, 1104.5, screen="DriveScreen", notice="NOT_CALIBRATED")
                .stream(1099.0, 1104.2))
    passes("B5''-10", c1.grade_b10, b10(TB()).build())
    fails("B5''-10 drives", c1.grade_b10,
          with_(b10, lambda tb: tb.tr.samples.append((1103.0, 0, 1.0, 0, 0))), "no non-zero")


def t_b11_b12() -> None:
    def b11(tb: TB, resume: float = 1205.15) -> TB:
        return (tb.mark("enter", 1200.0).mark("pass", 1203.0).mark("forward", 1205.0)
                .polls(1199.0, 1201.5)
                .polls(1201.5, 1203.0, screen="DriveScreen", notice="POST_NOT_PASSED",
                       post="PENDING")
                .polls(1203.0, 1206.0, screen="DriveScreen", notice="CENTRE_FIRST")
                .stream(1199.0, resume).stream(resume, 1206.0, 1.0))
    passes("B5''-11", c1.grade_b11, b11(TB()).build())
    fails("B5''-11 latch", c1.grade_b11, b11(TB(), resume=1203.5).build(),
          "zero for the 1 s after pass")

    def b12(tb: TB) -> TB:
        return (tb.mark("fault", 1300.0).mark("ok", 1301.0).mark("forward", 1302.5)
                .polls(1300.2, 1301.0, screen="DriveScreen", notice="STICK_FAULT")
                .stream(1299.0, 1300.15, 1.0).stream(1300.15, 1302.7).stream(1302.7, 1303.5, 1.0))
    passes("B5''-12", c1.grade_b12, b12(TB()).build())
    fails("B5''-12 slow", c1.grade_b12,
          with_(b12, lambda tb: tb.tr.samples.append((1300.5, 0, 1.0, 0, 0))), "within 0.2 s")


def t_b13_b14_b15() -> None:
    def b13(tb: TB, profile: str = "LOW") -> TB:
        return tb.mark("tap", 1400.0).dc(1400.3, "ENABLE", profile)
    passes("B5''-13", c1.grade_b13, b13(TB()).build())
    fails("B5''-13 profile", c1.grade_b13, b13(TB(), "NORMAL").build(), "profile LOW")

    def b14(tb: TB, drive: float = 1501.2) -> TB:
        return (tb.polls(1495.0, 1500.0, screen="SeatScreen").mark("enter", 1500.0)
                .polls(drive, drive + 0.2, screen="DriveScreen"))
    passes("B5''-14", c1.grade_b14, b14(TB()).build())
    fails("B5''-14 late", c1.grade_b14, b14(TB(), drive=1503.5).build(), "within 3.0 s")

    def b15(tb: TB) -> TB:
        return (tb.mark("restart", 1600.0).mark("back", 1620.0).dc(1618.0, "DISABLE")
                .polls(1620.0, 1621.0).polls(1621.0, 1661.0, screen="DriveScreen",
                                              notice="POST_NOT_PASSED")
                .stream(1599.0, 1661.0))
    passes("B5''-15", c1.grade_b15, b15(TB()).build())
    fails("B5''-15 ENABLE", c1.grade_b15, with_(b15, lambda tb: tb.dc(1619.0, "ENABLE")),
          "no ENABLE")


# ---------------------------------------------------------------- C3


def post_serial(ms: int = 1300, passed: int = 19) -> str:
    lines = [f"[x] POST chk.n{k} PASS 1 u [0,1]" for k in range(passed)]
    lines += [f"[x] POST oth.n{k} FAIL 0 u [1,1]" for k in range(19 - passed)]
    return "\n".join(lines + [f"[x] POST RESULT {'PASS' if passed == 19 else 'FAIL'} "
                              f"{passed}/19 {ms}"])


def t_b16() -> None:
    def b(tb: TB, enable_after: float | None = None, reason: str = "SW") -> TB:
        for i in range(1, 6):
            r = 100.0 * i
            tb.mark(f"restart-{i}", r).mark(f"back-{i}", r + 20.0).dc(r + 21.0, "DISABLE")
            tb.polls(r + 20.0, r + 31.0, reset_reason=reason)
            if enable_after and i == 3:
                tb.dc(r + 21.0 + enable_after, "ENABLE")
        return tb
    p = {"serial": {i: post_serial() for i in range(1, 6)}}
    passes("B5''-16", c3.grade_b16, b(TB()).build(), p)
    fails("ENABLE within 10 s", c3.grade_b16, b(TB(), enable_after=5.0).build(),
          "boot 3: the HMI's first DriveCommand", p)
    fails("reset reason", c3.grade_b16, b(TB(), reason="PANIC").build(), "reset_reason SW", p)
    slow = {"serial": {**p["serial"], 2: post_serial(ms=3100)}}
    fails("slow POST", c3.grade_b16, b(TB()).build(), "boot 2: POST RESULT PASS", slow)


def t_b17() -> None:
    def b(tb: TB, enables: int = 1, fail_at: float | None = None, banner: bool = True) -> TB:
        tb.mark("rerun", 100.0).mark("hold-1", 103.0).mark("centred", 104.6)
        tb.mark("hold-2", 106.2)
        tb.polls(100.1, 104.6, post="PENDING", post_check="joy.y_cal_off",
                 indicator="WAITING", indicator_text="Centre the joystick",
                 banner=lambda t: "REFUSED_POST" if banner and 103.6 <= t < 104.4 else "NONE")
        tb.polls(104.6, 105.8, post="PENDING", post_check="joy.y_cal_off", indicator="WAITING")
        tb.polls(105.8, 110.0)
        for k in range(enables):
            tb.dc(107.8 + 0.1 * k, "ENABLE")
        if fail_at:
            tb.polls(fail_at, fail_at + 0.05, post="FAIL")
        return tb
    passes("B5''-17", c3.grade_b17, b(TB()).build())
    fails("FAIL", c3.grade_b17, b(TB(), fail_at=102.0).build(), "never FAIL")
    fails("two ENABLEs", c3.grade_b17, b(TB(), enables=2).build(), "1 ENABLE")
    fails("no banner", c3.grade_b17, b(TB(), banner=False).build(), "banner REFUSED_POST")

    def bb(tb: TB, pass_at: float = 204.5) -> TB:
        return (tb.mark("rerun", 200.0).mark("release", 203.0)
                .polls(200.1, pass_at, post="PENDING", post_check="joy.button_idle",
                       indicator="WAITING", indicator_text="Release the joystick button")
                .polls(pass_at, 207.0))
    passes("B5''-17b", c3.grade_b17b, bb(TB()).build())
    fails("B5''-17b late", c3.grade_b17b, bb(TB(), pass_at=206.0).build(), "within 2.5 s")


def t_b18() -> None:
    def b(tb: TB, dt: float = DT, resume: float = 308.25, leak: float | None = None) -> TB:
        tb.mark("rerun", 300.0).mark("centred", 305.0).mark("forward", 308.1)
        tb.polls(299.0, 300.1, screen="DriveScreen")
        tb.polls(300.1, 306.2, screen="DriveScreen", post="PENDING", notice="POST_NOT_PASSED",
                 post_check=lambda t: "joy.y_cal_off" if t < 305.0 else "joy.y_noise")
        tb.polls(306.2, 310.2, screen="DriveScreen", notice="CENTRE_FIRST")
        tb.stream(299.0, 300.2, 1.0).stream(300.2, resume, dt=dt).stream(resume, 310.2, 1.0)
        if leak:
            tb.tr.samples.append((leak, 0, 1.0, 0, 0))
        return tb
    passes("B5''-18", c3.grade_b18, b(TB()).build())
    fails("motion before PASS", c3.grade_b18, b(TB(), leak=302.0).build(), "until PASS is zero")
    fails("slow stream", c3.grade_b18, b(TB(), dt=0.05).build(), ">= 25 Hz")
    fails("slow resume", c3.grade_b18, b(TB(), resume=308.5).build(), "within 0.3 s")

    def b18b(tb: TB, drive: float = 426.8) -> TB:
        return (tb.mark("restart", 400.0).ev(425.0, "hmi_back").mark("back", 426.5)
                .dc(425.5, "DISABLE").mark("centred", 432.0)
                .polls(426.5, drive).polls(drive, 434.0, screen="DriveScreen")
                .stream(425.0, 432.5).stream(432.5, 434.0, 1.0))
    p = {"serial_text": "x\nPOST RESULT PASS 19/19 1200\n",
         "serial_lines": [(410.0, "[ui/I][2.1]: screen -> BootScreen"),
                          (426.7, "[ui/I][21.9]: screen -> DriveScreen")]}
    passes("B5''-18b", c3.grade_b18b, b18b(TB()).build(), p)
    fails("B5''-18b serial: Drive late", c3.grade_b18b, b18b(TB()).build(), "serial screen-change",
          {**p, "serial_lines": [(428.4, "[ui/I][23.6]: screen -> DriveScreen")]})
    st = graded(c3.grade_b18b, b18b(TB()).build(), {**p, "serial_lines": []})
    r = st.result()
    expect("no screen-change line: never PASS on the poll alone, partial",
           (r["verdict"], r["reason"].startswith("partial: remote UI up after the window")),
           ("NOT_RUN", True))
    fails("B5''-18b ENABLE", c3.grade_b18b, with_(b18b, lambda tb: tb.dc(426.0, "ENABLE")),
          "no ENABLE", p)
    fails("B5''-18b no POST line", c3.grade_b18b, b18b(TB()).build(), "POST RESULT PASS", {})

    def b18c(tb: TB) -> TB:
        return (tb.mark("restart", 500.0).mark("back", 520.0).dc(519.0, "DISABLE")
                .ev(519.05, "mib_state", state="IDLE").polls(520.0, 530.3))
    passes("B5''-18c", c3.grade_b18c, b18c(TB()).build())
    fails("B5''-18c Drive", c3.grade_b18c,
          with_(b18c, lambda tb: tb.polls(525.0, 525.05, screen="DriveScreen",
                                          phase="DRIVING")), "Locked for 10 s")


def t_b19_b20() -> None:
    def b19(tb: TB, banner: bool = True) -> TB:
        return (tb.mark("seat-1", 600.0).mark("centred", 602.2).mark("seat-2", 604.0)
                .polls(600.0, 602.2, screen="SeatScreen", post="PENDING",
                       banner="REFUSED_POST" if banner else "NONE")
                .ev(604.3, "seat_command", axis=0))
    passes("B5''-19", c3.grade_b19, b19(TB()).build())
    fails("B5''-19 early seat", c3.grade_b19,
          with_(b19, lambda tb: tb.ev(600.5, "seat_command", axis=0)), "no SeatCommand")
    fails("B5''-19 no banner", c3.grade_b19, b19(TB(), banner=False).build(),
          "banner REFUSED_POST")

    def b20(tb: TB, pending_at: float | None = None) -> TB:
        tb.mark("rerun", 700.0).mark("hold", 703.2).mark("back", 730.0)
        tb.polls(700.5, 713.6, post="FAIL", post_check="joy.cal_saved", indicator="FAILED",
                 indicator_text="The joystick must be calibrated first. Turn the HMI off and on")
        tb.polls(735.0, 736.0)
        if pending_at:
            tb.polls(pending_at, pending_at + 0.05, post="PENDING")
        return tb
    passes("B5''-20", c3.grade_b20, b20(TB()).build())
    fails("B5''-20 not latched", c3.grade_b20, b20(TB(), pending_at=710.0).build(),
          "stays FAIL")
    fails("B5''-20 hold", c3.grade_b20, with_(b20, lambda tb: tb.dc(704.0, "ENABLE")),
          "hold refused")


def t_b21_b22() -> None:
    def b21(tb: TB, reason: str = "PANIC") -> TB:
        return (tb.mark("back", 820.0).mark("hold", 822.0).mark("back-2", 850.0)
                .polls(821.0, 822.0, post="FAIL", post_check="sys.clean_reset",
                       reset_reason=reason, indicator="FAILED",
                       indicator_text="Restarted after a fault: PANIC. Turn the HMI off and on")
                .polls(855.0, 856.0))
    p = {"serial_text": "POST sys.clean_reset FAIL 0  [1,1]\nPOST RESULT FAIL 18/19 1200\n"}
    passes("B5''-21", c3.grade_b21, b21(TB()).build(), p)
    fails("B5''-21 reason", c3.grade_b21, b21(TB(), "SW").build(), "reset_reason PANIC", p)
    fails("B5''-21 serial", c3.grade_b21, b21(TB()).build(), "sys.clean_reset FAIL 0",
          {"serial_text": "POST RESULT FAIL 18/19 1200"})

    def b22(tb: TB, fail_at: float = 903.3) -> TB:
        return (tb.mark("rerun", 900.0).polls(900.1, fail_at, post="PENDING")
                .polls(fail_at, 905.0, post="FAIL", post_check="adc.valid", indicator="FAILED",
                       indicator_text="Start-up check timed out: Joystick read failed"))
    passes("B5''-22", c3.grade_b22, b22(TB()).build())
    fails("B5''-22 late", c3.grade_b22, b22(TB(), fail_at=903.9).build(), "3.0 and 3.7 s")

    def b22b(tb: TB, unlatch: bool = False) -> TB:
        tb.mark("rerun", 1000.0).polls(1000.1, 1001.5, post="PENDING")
        tb.polls(1001.5, 1006.0, post="FAIL", post_check="adc.valid")
        if unlatch:
            tb.polls(1006.0, 1006.1)
        return tb
    q = {"serial_text": "POST adc.valid FAIL 900 permille [990,1000]"}
    passes("B5''-22b", c3.grade_b22b, b22b(TB()).build(), q)
    fails("B5''-22b unlatched", c3.grade_b22b, b22b(TB(), unlatch=True).build(), "latched", q)
    fails("B5''-22b value", c3.grade_b22b, b22b(TB()).build(), "below 990",
          {"serial_text": "POST adc.valid FAIL 995 permille [990,1000]"})


# ---------------------------------------------------------------- C4


def t_b5f_b5g() -> None:
    def f(tb: TB, bad: int | None = None) -> TB:
        for i in range(1, 11):
            m = 2000.0 + 10.0 * i
            tb.mark(f"idle-{i}", m).stream(m - 1.2, m + 0.15, 1.0).stream(m + 0.15, m + 2.0)
            tb.ev(m + 0.05, "mib_publish", state="IDLE", seq=i, targets=1)
            if bad == i:
                tb.tr.samples.append((m + 0.3, 0, 0.8, 0, 0))
        return tb
    passes("B5f", c4.grade_b5f, f(TB()).build())
    fails("B5f repeat 3", c4.grade_b5f, f(TB(), bad=3).build(), "10 of 10")

    def g(tb: TB, dt: float = DT) -> TB:
        for i in range(1, 4):
            m = 2200.0 + 10.0 * i
            tb.mark(f"pause-{i}", m).ev(m - 0.4, "mib_publish", state="ENABLED", seq=i, targets=1)
            tb.ev(m + 0.01, "pause").stream(m - 1.2, m + 1.7, 1.0).stream(m + 1.7, m + 4.5, dt=dt)
        return tb
    passes("B5g", c4.grade_b5g, g(TB()).build())
    fails("B5g rate", c4.grade_b5g, g(TB(), dt=0.05).build(), "3 of 3")


def t_b5h() -> None:
    def h(tb: TB, drop_at: float | None = None, idle: bool = False) -> TB:
        tb.mark("hold", 3000.0).mark("held", 3002.0)
        tb.stream(2999.0, 3007.3, lambda t: 0.0 if drop_at and abs(t - drop_at) < 0.02 else 0.9)
        for k in range(15):
            tb.ev(3000.0 + 0.5 * k, "mib_publish", state="IDLE" if idle and k == 9 else "ENABLED",
                  seq=k, targets=1)
        return tb.dc(3001.6, "DISABLE", action="ignored")
    passes("B5h", c4.grade_b5h, h(TB()).build())
    fails("B5h y", c4.grade_b5h, h(TB(), drop_at=3004.0).build(), "y > 0.5 for 5 s")
    fails("B5h IDLE", c4.grade_b5h, h(TB(), idle=True).build(), "stays ENABLED")


WDT_LV = ("E (12) task_wdt: Task watchdog got triggered. The following tasks/users did not "
          "reset the watchdog in time:\nE (12) task_wdt:  - lv_task (CPU 1)\n")
WDT_ADC = WDT_LV.replace("lv_task (CPU 1)", "Read ADC (CPU 0)")


def t_b5i_b5j() -> None:
    def i(tb: TB, late: bool = False) -> TB:
        tb.mark("stall-300", 3100.0).mark("stalled-300", 3100.35).mark("forward", 3101.0)
        tb.stream(3099.0, 3100.19, 1.0).stream(3100.19, 3101.2).stream(3101.2, 3102.0, 1.0)
        tb.mark("stall-3000", 3110.0).stream(3110.0, 3112.0)
        if late:
            tb.tr.samples.append((3111.5, 0, 0.6, 0, 0))
        return tb
    p = {"one_way_s": 0.01, "serial_text": WDT_LV, "reset_reason": "TASK_WDT"}
    passes("B5i", c4.grade_b5i, i(TB()).build(), p)
    fails("B5i motion before the silence", c4.grade_b5i, i(TB(), late=True).build(),
          "last 1.5 s", p)
    fails("B5i panic", c4.grade_b5i, i(TB()).build(), "names lv_task", {**p, "serial_text": ""})

    def j(tb: TB, gone: float = 3201.2) -> TB:
        return tb.mark("stall", 3200.0).stream(3199.0, 3200.05, 1.0).ev(gone, "hmi_gone")
    q = {"one_way_s": 0.01, "serial_text": WDT_ADC}
    passes("B5j", c4.grade_b5j, j(TB()).build(), q)
    fails("B5j ongone", c4.grade_b5j, j(TB(), gone=3202.0).build(), "ongone at 1 s", q)
    fails("B5j panic", c4.grade_b5j, j(TB()).build(), "Read ADC", {**q, "serial_text": WDT_LV})


# ---------------------------------------------------------------- C2


def stick(state: str = "OK", **kw: object) -> dict:
    return {"state": state, "health": "OK" if state == "OK" else "CHECK",
            "reason": "NONE", "reason_axis": "", "fault_reason": "NONE", "fault_axis": "",
            "suspect_onsets": 0, "faults": 0, "bad": {}, "xy_age_max_ms": 131,
            "max_mv": [0, 0, 0], "joy_key": 0, **kw}


def t_c2_mv_and_rail() -> None:
    def mv(tb: TB, y: float = 1.0) -> TB:
        return (tb.mark("change", 4000.0).polls(3999.5, 4002.2, stick=stick())
                .stream(3999.0, 4000.1).stream(4000.1, 4002.2, y))
    passes("C2-1", c2.grade_mv_drives, mv(TB()).build())
    fails("C2-1 half", c2.grade_mv_drives, mv(TB(), 0.5).build(), "y >= 0.99")
    st = graded(c2.grade_mv_drives, mv(TB()).build(), {"residual": True})
    expect("C2-2 says RESIDUAL-D2 confirmed", "RESIDUAL-D2 confirmed" in
           st.records.get("residual", ""), True)

    def rail(tb: TB, axis: str = "H") -> TB:
        fault = stick("FAULT", fault_reason="HIGH", fault_axis=axis)
        return (tb.mark("change", 4100.0).mark("centred", 4101.0)
                .stream(4099.0, 4100.15, 1.0).stream(4100.15, 4104.2)
                .polls(4099.5, 4100.35, screen="DriveScreen", stick=stick())
                .polls(4100.35, 4101.2, screen="DriveScreen", notice="STICK_FAULT", stick=fault)
                .polls(4101.2, 4104.2, stick=fault))
    passes("C2-3", c2.grade_c2_3, rail(TB()).build())
    fails("C2-3 axis", c2.grade_c2_3, rail(TB(), "V").build(), "HIGH H")
    fails("C2-3 motion", c2.grade_c2_3,
          with_(rail, lambda tb: tb.tr.samples.append((4102.0, 0.0, 0.0, 0.2, 0))),
          "exactly 0")

    def edge(tb: TB, at: float = 4202.5) -> TB:
        return (tb.mark("change", 4200.0).mark("over", 4202.0).polls(4200.0, at, stick=stick())
                .polls(at, 4203.6, stick=stick("FAULT", fault_reason="HIGH", fault_axis="V"))
                .stream(4199.0, 4200.1).stream(4200.1, 4202.0, -1.0).stream(4202.0, 4203.6))
    passes("C2-3b", c2.grade_c2_3b, edge(TB()).build())
    fails("C2-3b late", c2.grade_c2_3b, edge(TB(), 4203.5).build(), "within t + 1000 ms")


def t_c2_bad_cycles() -> None:
    def a(tb: TB, after: int = 4) -> TB:
        return (tb.mark("change", 4300.0).mark("centred", 4302.0).mark("forward", 4302.5)
                .polls(4299.0, 4300.0, stick=stick(suspect_onsets=3))
                .polls(4300.0, 4303.6, stick=stick(suspect_onsets=after))
                .stream(4299.0, 4300.05, 1.0).stream(4300.05, 4302.6).stream(4302.6, 4303.6, 1.0))
    passes("C2-4a", c2.grade_c2_4a, a(TB()).build())
    fails("C2-4a onsets", c2.grade_c2_4a, a(TB(), after=5).build(), "suspect onsets +1")

    def b(tb: TB, at: float = 4400.6) -> TB:
        return (tb.mark("change", 4400.0).mark("end", 4401.5).polls(4399.0, at, stick=stick())
                .polls(at, 4401.6, stick=stick("FAULT")).stream(4399.0, 4400.15, 1.0)
                .stream(4400.15, 4401.6))
    passes("C2-4b", c2.grade_c2_4b, b(TB()).build())
    fails("C2-4b late", c2.grade_c2_4b, b(TB(), 4401.2).build(), "within t + 1.0 s")

    def c(tb: TB, fault: bool = False) -> TB:
        tb.mark("change", 4500.0).mark("end", 4512.0)
        tb.polls(4499.0, 4500.0, stick=stick(suspect_onsets=0))
        tb.polls(4500.0, 4512.5, stick=lambda t: stick(
            "FAULT" if fault and t > 4510 else "OK", suspect_onsets=min(10, int((t - 4500) / 1.2) + 1)))
        return tb
    passes("C2-4c", c2.grade_c2_4c, c(TB()).build())
    fails("C2-4c fault", c2.grade_c2_4c, c(TB(), fault=True).build(), "never FAULT")


def t_c2_reads() -> None:
    def r(tb: TB, reason: str = "MISSING", silent: bool = False, early: float = 0.1) -> TB:
        tb.mark("change", 4600.0).stream(4599.0, 4600.0 + early, 1.0)
        tb.stream(4600.0 + early, 4600.5 if silent else 4602.1)
        tb.polls(4599.0, 4600.3, stick=stick())
        return tb.polls(4600.3, 4602.1, stick=stick("FAULT", fault_reason=reason, fault_axis="V"))
    passes("C2-5", c2.grade_c2_5, r(TB()).build())
    fails("C2-5 silence", c2.grade_c2_5, r(TB(), silent=True).build(), ">= 25 Hz")
    passes("C2-7", c2.grade_c2_7, r(TB(), "NAN").build())
    fails("C2-7 reason", c2.grade_c2_7, r(TB(), "MISSING").build(), "reason NAN")

    def s(tb: TB, fresh: float = 0.2) -> TB:
        tb.mark("change", 4700.0).stream(4699.0, 4700.0 + fresh, 1.0)
        tb.stream(4700.0 + fresh, 4702.1)
        return tb.polls(4699.0, 4700.6, stick=stick()).polls(
            4700.6, 4702.1, stick=stick("FAULT", fault_reason="STALE"))
    passes("C2-6", c2.grade_c2_6, s(TB()).build())
    fails("C2-6 no fresh y", c2.grade_c2_6, s(TB(), fresh=-0.5).build(), "y > 0 in [t")


def t_c2_8_9_10() -> None:
    def e(tb: TB, before: bool = False, banner: bool = True) -> TB:
        tb.mark("enter", 4790.0).mark("change", 4800.0).dc(4800.3, "DISABLE")
        flag = (lambda t: "STICK_FAULT" if banner and t >= 4800.4 else "NONE")
        tb.polls(4799.0, 4801.0, screen="DriveScreen", banner=flag)
        tb.polls(4801.0, 4802.1, banner=flag)
        tb.stream(4799.0, 4800.15, 1.0).stream(4800.15, 4802.1)
        if before:
            tb.dc(4799.0, "DISABLE")
        return tb
    passes("C2-8", c2.grade_c2_8, e(TB()).build())
    fails("C2-8 earlier DISABLE", c2.grade_c2_8, e(TB(), before=True).build(), "none before")
    fails("C2-8 no banner", c2.grade_c2_8, e(TB(), banner=False).build(), "banner STICK_FAULT")
    passes("C2-9", c2.grade_c2_9, tr_b6(TB(), t1=4900.3, label="change", output="zero").build())
    fails("C2-9 motion", c2.grade_c2_9,
          with_(lambda tb: tr_b6(tb, t1=4900.3, label="change", output="zero"),
                lambda tb: tb.tr.samples.append((4903.0, 0, 1.0, 0, 0))), "G3 over G4")

    def ten(tb: TB, notice: str = "STICK_FAULT") -> TB:
        return (tb.mark("release", 5000.0).mark("end", 5013.0).stream(4999.0, 5013.0)
                .polls(5000.0, 5009.0, stick=stick("FAULT"))
                .polls(5009.0, 5013.0, screen="DriveScreen", notice=notice, stick=stick("FAULT")))
    passes("C2-10", c2.grade_c2_10, ten(TB()).build())
    fails("C2-10 notice", c2.grade_c2_10, ten(TB(), "NONE").build(), "STICK_FAULT")


def t_c2_11_12() -> None:
    def k(tb: TB, key: int = 0) -> TB:
        return (tb.mark("keys-fault", 5100.0).mark("keys-fault-end", 5104.0)
                .polls(5100.0, 5104.0, screen="SettingsScreen", stick=stick("FAULT", joy_key=key)))
    p = {"control_moved": True, "control_focus": [1, 2], "fault_moved": False,
         "fault_focus": [1, 1], "screen": "SettingsScreen"}
    passes("C2-11", c2.grade_c2_11, k(TB()).build(), p)
    fails("C2-11 key", c2.grade_c2_11, k(TB(), 18).build(), "joy_key 0", p)
    fails("C2-11 no control", c2.grade_c2_11, k(TB()).build(), "control",
          {**p, "control_moved": False})

    def i(tb: TB, gap: str | None = None) -> TB:
        tb.mark("fault", 5200.0)
        for n, name in enumerate(("Home", "Settings", "Joystick", "Drive")):
            a = 5201.0 + 15.0 * n
            tb.mark(f"on-{name}", a).mark(f"off-{name}", a + 15.0)
            tb.polls(a, a + 15.0, indicator="NONE" if name == gap else "FAILED",
                     indicator_text="" if name == gap else "Joystick fault: recalibrate to clear",
                     stick=stick("FAULT"))
        return tb.polls(5261.0, 5261.1, stick=stick("FAULT"))
    q = {"screens": ["Home", "Settings", "Joystick", "Drive"]}
    passes("C2-12", c2.grade_c2_12, i(TB()).build(), q)
    fails("C2-12 gap", c2.grade_c2_12, i(TB(), "Settings").build(), "on Settings", q)


def t_c2_13_14() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        r = hazard_steps.run_step("C2-13", "0.0.0.0", pathlib.Path(tmp), common.REPO, ["c2"])
    expect("C2-13 is NOT_RUN before the board is touched, and says why",
           (r["verdict"], "rewriting the stored calibration" in r["reason"]), ("NOT_RUN", True))
    sources = [pathlib.Path(m.__file__).read_text(encoding="utf-8")
               for m in (c1, c2, c3, c4, hazard_rig, hazard_steps)]
    expect("no step presses Calibrate or injects a calibration run (no flash write)",
           any(k in src for src in sources for k in ("CalibrateButton", "CALIBRATE_BUTTON",
                                                     "inject_calibration_run")), False)
    expect("the C2 clean-up reboots", c2.CLEANUP["C2-3"].__name__, "reboot_cleanup")

    def d(tb: TB, ok_at: float = 5401.0, age: int = 131) -> TB:
        return (tb.mark("back", 5400.0).polls(5400.2, ok_at, stick=stick("INIT"))
                .polls(ok_at, 5404.0, stick=stick(xy_age_max_ms=age)))
    passes("C2-14", c2.grade_c2_14, d(TB()).build())
    fails("C2-14 slow", c2.grade_c2_14, d(TB(), ok_at=5402.0).build(), "within 1.5 s")
    fails("C2-14 age", c2.grade_c2_14, d(TB(), age=240).build(), "xy_age_max_ms")


def t_c2_15_16() -> None:
    rows = {"joy.bad_samples": {"value": 0, "unit": "", "result": "PASS"},
            "joy.xy_age_max": {"value": 140, "unit": "ms", "result": "PASS"},
            "time.adc_avg": {"value": 35071, "unit": "us", "result": "PASS"},
            "time.adc_max": {"value": 38000, "unit": "us", "result": "PASS"},
            "mem.stk_adc": {"value": 2500, "unit": "B", "result": "PASS"},
            "joy.health": {"value": 1, "unit": "", "result": "PASS"}}
    passes("C2-15", c2.grade_c2_15, hg.Trace(), {"selftest": rows})
    fails("C2-15 adc_max", c2.grade_c2_15, hg.Trace(), "time.adc_max",
          {"selftest": {**rows, "time.adc_max": {"value": 41000, "unit": "us"}}})
    fails("C2-15 no self test", c2.grade_c2_15, hg.Trace(), "the self test ran", {})

    def s(tb: TB, zero: float = 5500.3) -> TB:
        return (tb.mark("change", 5500.0).stream(5499.0, zero, 1.0).stream(zero, 5501.5)
                .polls(5499.0, 5500.6, stick=stick())
                .polls(5500.6, 5501.5, stick=stick("FAULT", fault_reason="STALE")))
    passes("C2-16", c2.grade_c2_16, s(TB()).build(), {"one_way_s": 0.01})
    fails("C2-16 slow", c2.grade_c2_16, s(TB(), 5500.5).build(), "by 340 ms", {"one_way_s": 0.01})


# ---------------------------------------------------------------- plan, run_bench, boot


def t_plan() -> None:
    expect("default list has no hazard step", any(s in run_bench.plan_steps(None)[1]
                                                  for s in hazard_steps.ALL_STEPS), False)
    c1_only = run_bench.plan_steps(None, ["c1"])[1]
    expect("--hazard c1 adds B5''-1..15 after B5e",
           c1_only[c1_only.index("B5e") + 1:], hazard_steps.GROUPS["c1"])
    c13 = hazard_steps.plan(["c1", "c3"])
    expect("with c3, B5''-15 is retired (C3 F5)", ("B5''-15" in c13, "B5''-18b" in c13),
           (False, True))
    c12 = hazard_steps.plan(["c1", "c2"])
    expect("with c2, B5''-12 is retired (C2 E9)", ("B5''-12" in c12, "C2-1" in c12),
           (False, True))
    expect("landing order", hazard_steps.plan(["c2", "c4", "c3", "c1"])[0], "B5''-1")
    expect("all", hazard_steps.parse_groups("all"), list(hazard_steps.GROUP_ORDER))
    expect("names: B5pp, any case", run_bench.plan_steps("b0,B5PP-6B,b5''-16,c2-4A,b5F")[1],
           ["B0", "B5''-6b", "B5''-16", "C2-4a", "B5f"])
    for bad in ("c5", "c1,x"):
        try:
            hazard_steps.parse_groups(bad)
        except ValueError:
            continue
        raise AssertionError(f"group {bad!r} accepted")
    expect("every step has a script, a grader and a title",
           all(len(hazard_steps.MODULES[hazard_steps.STEP_GROUP[s]].STEPS[s]) == 3
               for s in hazard_steps.ALL_STEPS), True)
    expect("the specs' step count: C1 19, C3 11, C4 5, C2 19",
           [len(hazard_steps.GROUPS[g]) for g in hazard_steps.GROUP_ORDER], [19, 11, 5, 19])


def t_run_bench_hazard() -> None:
    import run_bench_selftest as rbs
    calls: list[tuple] = []
    sweeps = [0]

    def fake_step(step, ip, out, tree, groups, port, proven, sweep=None):
        calls.append((step, tuple(groups), port, proven is not None))
        return {"verdict": "PASS", "checks": [], "problems": []}

    def run(proof_ok: bool) -> dict:
        calls.clear()
        with rbs.fake_bench(rbs.FakeBoard()):
            saved = (hazard_steps.run_step, hazard_rig.sole_sim_proof)
            hazard_steps.run_step = fake_step
            hazard_rig.sole_sim_proof = lambda ip, sweep, strays=None: (
                sweeps.__setitem__(0, sweeps[0] + 1) or (proof_ok, "proof"))
            try:
                a = rbs.argparse.Namespace(label="t", build_dir=pathlib.Path("."), flash=False,
                                           tree=common.REPO, no_save=True, ip=rbs.IP,
                                           hazard_groups=["c1", "c3"])
                sequence, wanted = run_bench.plan_steps("B5,B5''-1,B5pp-16", ["c1", "c3"])
                r = run_bench.Run(a)
                r.ip, r.port = rbs.IP, "COMX"
                run_bench.run_steps(r, sequence, wanted)
                return r.summary
            finally:
                hazard_steps.run_step, hazard_rig.sole_sim_proof = saved
    sweeps[0] = 0
    summary = run(True)
    expect("verdicts", rbs.verdicts(summary), {"B5": "PASS", "B5''-1": "PASS",
                                               "B5''-16": "PASS"})
    expect("one proof for the run", sweeps[0], 1)
    expect("groups, port and proof reach the steps", calls,
           [("B5''-1", ("c1", "c3"), "COMX", True), ("B5''-16", ("c1", "c3"), "COMX", True)])
    summary = run(False)
    expect("no proof: hazard steps INVALID, nothing injected, B5 untouched",
           (rbs.verdicts(summary), calls),
           ({"B5": "PASS", "B5''-1": "INVALID", "B5''-16": "INVALID"}, []))


def t_b2_wiring() -> None:
    import run_bench_selftest as rbs
    seen = {}

    def analyse(text, baseline, tree=None, groups=()):
        seen.update(tree=tree, groups=list(groups))
        return {"verdict": "PASS", "problems": [], "ip": rbs.IP}
    with rbs.fake_bench(rbs.FakeBoard()):
        saved = run_bench.boot_check.analyse
        run_bench.boot_check.analyse = analyse
        try:
            a = rbs.argparse.Namespace(label="t", build_dir=pathlib.Path("."), flash=False,
                                       tree=pathlib.Path("T"), no_save=True, ip=None,
                                       hazard_groups=["c3"])
            r = run_bench.Run(a)
            r.port = "COMX"
            run_bench.run_steps(r, *run_bench.plan_steps("B2"))
        finally:
            run_bench.boot_check.analyse = saved
    expect("B2 grades with the tree and the fixes", seen, {"tree": pathlib.Path("T"),
                                                            "groups": ["c3"]})


def fake_tree(root: pathlib.Path, checks: int, idle_off: bool) -> pathlib.Path:
    (root / "main").mkdir(parents=True)
    rows = "\n".join(f'    Check{{Id::C{i}, "c.{i}", "", 0, 1, Need::REQUIRED, "d"}},'
                     for i in range(checks))
    (root / "main" / "selftest_spec.hpp").write_text(
        f"inline constexpr std::array kChecks{{\n{rows}\n}};\n", encoding="utf-8")
    (root / "sdkconfig.defaults").write_text(
        "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n\n" if idle_off else "CONFIG_X=y\n",
        encoding="utf-8")
    return root


def _boot(n: int = 54, extra: str = "") -> tuple[str, str]:
    base = (common.BASELINE_DIR / "boot-board2.log").read_text(encoding="utf-8",
                                                               errors="replace")
    want = boot_check.OVERRIDES["settings_loaded"]["value"]
    (_, got), = boot_check.find(base.splitlines(), boot_check.MARKERS[0][1])[:1]
    log = base.replace(got, want).replace("ready: 54 checks", f"ready: {n} checks") + extra
    return log, base


def t_boot_count() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        for n, problem, finding in ((54, False, False), (57, False, True), (60, False, True),
                                    (55, True, False)):
            tree = fake_tree(pathlib.Path(tmp) / str(n), n, False)
            log, base = _boot(n)
            r = boot_check.analyse(log, base, tree)
            expect(f"{n} checks: problem", any("selftest_ready" in p for p in r["problems"]),
                   problem)
            expect(f"{n} checks: declared-change finding",
                   any("declared change" in f for f in r["findings"]), finding)
        tree = fake_tree(pathlib.Path(tmp) / "c4", 57, False)
        log, base = _boot(54)
        expect("the board says 54, the tree 57", any("selftest_ready" in p for p in
                                                     boot_check.analyse(log, base,
                                                                        tree)["problems"]), True)


def t_boot_wdt_and_post() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        on = fake_tree(pathlib.Path(tmp) / "on", 54, False)
        off = fake_tree(pathlib.Path(tmp) / "off", 54, True)
        log, base = _boot()
        expect("idle checks on: the baseline's trip is a known finding",
               boot_check.analyse(log, base, on)["problems"], [])
        expect("idle checks off: any task_wdt line is a problem",
               any("task_wdt" in p for p in boot_check.analyse(log, base, off)["problems"]), True)
        clean = "\n".join(line for line in log.splitlines() if "task_wdt" not in line)
        expect("idle checks off, no task_wdt line", boot_check.analyse(clean, base, off)["problems"],
               [])
        post = "\n[post/I][9.7]: POST RESULT PASS 19/19 1300\n[post/I][8.4]: POST reset reason: software (3)\n"
        expect("c3: both POST markers present", boot_check.analyse(log + post, base, on,
                                                                   {"c3"})["problems"], [])
        missing = boot_check.analyse(log, base, on, {"c3"})["problems"]
        expect("c3: missing markers", sorted(p.split(" (")[0] for p in missing),
               ["missing marker post_reset_reason", "missing marker post_result"])
        failed = boot_check.analyse(log + post.replace("RESULT PASS", "RESULT FAIL"), base, on,
                                    {"c3"})["problems"]
        expect("c3: POST RESULT FAIL is a problem", any("post_result" in p for p in failed), True)
        expect("without c3 they are not required", boot_check.analyse(log, base, on)["problems"],
               [])


def t_samples_and_proof() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "x.jsonl"
        samples = [(1.0, 0.0, 0.5, 0.0, 0), (1.035, 0.0, float("nan"), 0.0, 1)]
        expect("written", sim_child.write_samples(path, samples), 2)
        back = sim_child.read_samples(path)
        expect("read back", (back[0], back[1][4]), ((1.0, 0.0, 0.5, 0.0, 0), 1))
        expect("NaN survives", back[1][2] != back[1][2], True)
        expect("summary", {k: v for k, v in sim_child.summarise(samples).items()
                           if k in ("count", "buttons_seen")}, {"count": 2, "buttons_seen": [0, 1]})
    expect("a stray peer process refuses injection",
           hazard_rig.sole_sim_proof("1.2.3.4", lambda: ["1.2.3.4"], lambda: ["99: sim_child"])[0],
           False)
    expect("another responder refuses injection",
           hazard_rig.sole_sim_proof("1.2.3.4", lambda: ["1.2.3.4", "1.2.3.9"], lambda: [])[0],
           False)
    expect("the board alone passes",
           hazard_rig.sole_sim_proof("1.2.3.4", lambda: ["1.2.3.4"], lambda: [])[0], True)


def t_injector() -> None:
    sent: list[tuple] = []
    block = threading.Event()

    def send(h, v, tw, mask, seq):
        if block.is_set():
            time.sleep(0.4)  # a command that holds the connection (a STALL)
        sent.append((time.monotonic(), h, v, tw, mask, seq))
    inj = hazard_rig.Injector(send)
    try:
        inj.set(1507, 1510, 1477)
        time.sleep(0.35)
        inj.set(1507, 6, 1477, mask=0x81, once=True)
        time.sleep(0.25)
        n = len(sent)
        expect("refreshed every 100 ms", 3 <= n <= 7, True)
        once = [r for r in sent if r[4] == 0x81]
        expect("one one-cycle message, then the standing target again",
               (len(once), sent[-1][1:5]), (1, (1507, 1510, 1477, 0)))
        expect("sequence numbers go up", [r[5] for r in sent] == sorted({r[5] for r in sent}), True)
        block.set()
        time.sleep(1.0)
        block.clear()
        time.sleep(0.2)
        inj.pause()
        k = len(sent)
        time.sleep(0.3)
        expect("paused: nothing sent", len(sent), k)
    finally:
        inj.stop()
    expect("clamped to 0..3300 mV", hg.clamp_mv(3400.4), 3300)
    cal = hg.Cal.from_state({"cal": {"h": [11, 1507, 2971], "v": [6, 1510, 2962],
                                     "twist": [10, 1477, 2960]}})
    expect("forward is the vertical at its min", cal.forward(), (1507, 6, 1477))
    expect("no cal", hg.Cal.from_state({"cal": None}), None)


def t_process_tree() -> None:
    # A venv launcher (10) runs the base interpreter (11) that is this run's sim; 20/21 is
    # another sim, 30 an unrelated python, 12 a grandchild of the launcher.
    table = [(10, 1, r"C:\v\Scripts\python.exe sim_child.py --tree T"),
             (11, 10, r"C:\Espressif\tools\python\python.exe sim_child.py --tree T"),
             (12, 11, "python.exe -c pass"),
             (20, 1, r"C:\v\Scripts\python.exe sim_child.py --tree U"),
             (21, 20, r"C:\Espressif\tools\python\python.exe sim_child.py --tree U"),
             (30, 1, "python.exe lsp_server.py")]
    expect("the launcher's tree", common.descendants({10}, table), {10, 11, 12})
    expect("no roots", common.descendants(set(), table), set())
    strays = common.stray_peers(exclude_trees={10}, table=table)
    expect("this run's sim (launcher and child) is not a stray; another sim is",
           [s.split(":")[0] for s in strays], ["20", "21"])
    old = common.stray_peers({10}, table=table)
    expect("excluding the launcher pid alone reports its child (the 2026-10-08 NOT_RUN)",
           [s.split(":")[0] for s in old], ["11", "20", "21"])


class FakeSim:
    """peers.SimChild's surface for the rig's start-up."""
    started: list["FakeSim"] = []

    def __init__(self, ip, tree, log_path=None, event_log=None, ready=True, exits=False):
        self.ready = ready
        self.stopped = False
        self.exits = exits
        self.sent: list[str] = []
        self.marks: list[str] = []
        self.proc = type("P", (), {"pid": 4242, "poll": lambda me, exits=exits: 1 if exits
                                   else None})()
        FakeSim.started.append(self)

    def wait_for(self, regex, timeout, since=0):
        return None if self.exits else True

    def mark(self):
        return 0

    def reply(self, cmd, regex, timeout=5.0):
        import re
        return re.search(regex, "refuse ENABLE=False refuse DISABLE=True ignore next 0 "
                                "DISABLE(s), drop next 0 DISABLE(s), on HMI gone: keep")

    def events(self, after=None):
        return [{"ev": "mib_publish", "state": "ENABLED", "mono": 1.0, "targets": 1}]

    def xy_count(self, timeout=5.0):
        return 1

    def wait_ready(self, timeout=45.0):
        return self.ready

    def not_ready_reason(self, timeout=45.0):
        return "the simulated MCB got no XYTwist (fake)"

    def event_mark(self, label, timeout=5.0):
        self.marks.append(label)
        return True

    def send(self, cmd):
        self.sent.append(cmd)

    def stop(self):
        self.stopped = True


def t_rig_start_failure_stops_sim() -> None:
    saved = (hazard_rig.peers.SimChild, common.stray_peers, hazard_rig.ui_client.open_hmi)
    cases = (("not ready", dict(ready=False), [], None),
             ("a stray beside it", {}, ["99: sim_child.py --tree X"], None),
             ("the remote UI does not answer", {}, [], OSError("refused")))
    try:
        for what, kw, strays, ui_error in cases:
            FakeSim.started = []
            hazard_rig.peers.SimChild = lambda *a, kw=kw, **k: FakeSim(*a, **k, **kw)
            common.stray_peers = lambda *a, strays=strays, **k: list(strays)

            def open_hmi(*a, ui_error=ui_error, **k):
                raise ui_error
            hazard_rig.ui_client.open_hmi = open_hmi
            with quiet(), tempfile.TemporaryDirectory() as tmp:
                st = hazard_rig.HazardStep("x", pathlib.Path(tmp))
                rig = hazard_rig.Rig("1.2.3.4", common.REPO, pathlib.Path(tmp), st, set(),
                                     proven="proof")
                try:
                    with rig:
                        raise AssertionError(f"{what}: the rig started")
                except (hazard_rig.NotRun, OSError):
                    pass
            expect(f"{what}: the sim is stopped", [s.stopped for s in FakeSim.started], [True])
    finally:
        hazard_rig.peers.SimChild, common.stray_peers, hazard_rig.ui_client.open_hmi = saved


def t_serial_watch_and_sim_retry() -> None:
    def capture(port, seconds, reset=False, on_line=None, stop=None):
        on_line(None, "POST RESULT PASS 19/19 1300")
        while not stop.is_set():
            time.sleep(0.01)  # a quiet board: no more lines, the window still open
    w = hazard_rig.SerialWatch("COMX", 120.0, capture=capture)
    time.sleep(0.05)
    t0 = time.monotonic()
    text = w.stop()
    expect("stop() ends a quiet capture at once and keeps its lines",
           (text, time.monotonic() - t0 < 1.0), ("POST RESULT PASS 19/19 1300\n", True))
    saved = (hazard_rig.peers.SimChild, hazard_rig.SIM_RETRY_S)
    made = []

    def sim(*a, **k):
        made.append(FakeSim(*a, exits=not made, **k))
        return made[-1]
    hazard_rig.peers.SimChild, hazard_rig.SIM_RETRY_S = sim, 0.0
    try:
        with quiet(), tempfile.TemporaryDirectory() as tmp:
            st = hazard_rig.HazardStep("x", pathlib.Path(tmp))
            rig = hazard_rig.Rig("1.2.3.4", common.REPO, pathlib.Path(tmp), st, set(), proven="p")
            got = rig._start_sim()
        expect("a sim that exits at once is stopped and started once more",
               ([m.stopped for m in made], got is made[-1], st.records.get("sim_tries")),
               ([True, False], True, 2))
    finally:
        hazard_rig.peers.SimChild, hazard_rig.SIM_RETRY_S = saved

    class Boom(hazard_rig.Rig):
        def __enter__(self):
            raise PermissionError(13, "Access is denied")
    saved_rig = hazard_rig.Rig
    hazard_rig.Rig = Boom
    try:
        with quiet(), tempfile.TemporaryDirectory() as tmp:
            r = hazard_steps.run_step("B5''-17", "1.2.3.4", pathlib.Path(tmp), common.REPO,
                                      ["c3"], proven="p")
    finally:
        hazard_rig.Rig = saved_rig
    expect("a rig fault (the port refused) ends the step NOT_RUN, not the run",
           (r["verdict"], r["reason"].startswith("bench: PermissionError")), ("NOT_RUN", True))


def t_sim_replaced_at_reboot() -> None:
    saved = (hazard_rig.peers.SimChild, common.stray_peers)
    FakeSim.started = []
    hazard_rig.peers.SimChild = lambda *a, **k: FakeSim(*a, **k)
    common.stray_peers = lambda *a, **k: []
    try:
        with quiet(), tempfile.TemporaryDirectory() as tmp:
            st = hazard_rig.HazardStep("x", pathlib.Path(tmp))
            rig = hazard_rig.Rig("1.2.3.4", common.REPO, pathlib.Path(tmp), st, set(), proven="p")
            rig.sim = old = FakeSim("1.2.3.4", common.REPO)
            rig._save_samples = lambda path: None
            rig._retire_sim()
            expect("the old sim stops at the reboot, before the HMI is back",
                   (old.stopped, rig.sim), (True, None))
            rig._relaunch_sim()
            new = rig.sim
        expect("the new sim gets the old modes and ENABLED before it is ready, then a mark",
               (new.sent[:4], new.marks), (["ongone keep", "ign 0", "drop 0", "s"],
                                           ["sim-restarted"]))
        expect("ENABLED carried", new.sent[4], "a")
        expect("the verdict detail", st.details, [hazard_rig.SIM_RESTART_NOTE])
    finally:
        hazard_rig.peers.SimChild, common.stray_peers = saved


def t_forced_stop_kills_tree() -> None:
    import peers
    killed = []

    class Proc:
        pid = 777
        waits = 0

        def wait(self, timeout=None):
            Proc.waits += 1
            if Proc.waits == 1:
                raise TimeoutError("the sim did not quit")

        def kill(self):
            killed.append("kill-launcher-only")
    sim = peers.SimChild.__new__(peers.SimChild)
    sim.proc, sim.log_path, sim.lines = Proc(), None, []
    sim.send = lambda cmd: None
    saved = common.kill_tree
    common.kill_tree = lambda pid: killed.append(pid) or "killed"
    try:
        with quiet():
            sim.stop()
    finally:
        common.kill_tree = saved
    expect("a sim that does not quit is killed as a tree, not by its launcher pid", killed,
           [777])


def t_stick_lapses() -> None:
    import datetime as dt
    t0 = dt.datetime(2026, 10, 9, 3, 0, 0)

    def row(ms: int, cmd: str = "STICK 1 2 3 0 1", **kw) -> dict:
        return {"cmd": cmd, "sent": (t0 + dt.timedelta(milliseconds=ms)).isoformat(), **kw}
    rows = [row(0), row(100), row(150, "STATE"), row(200), row(480), row(600, error="x"),
            row(900), row(1000)]
    expect("gaps between STICK sends over 0.28 s", hazard_rig.stick_lapses(rows, []),
           [((t0 + dt.timedelta(milliseconds=900)).isoformat(timespec="milliseconds"), 0.42)])
    expect("a pause in between is not a lapse",
           hazard_rig.stick_lapses(rows, [t0 + dt.timedelta(milliseconds=700)]), [])
    expect("0.28 s exactly is not", hazard_rig.stick_lapses([row(0), row(280)], []), [])


def t_restart_tile_and_refresh() -> None:
    names = hazard_rig.skunk_actions(common.REPO)
    expect("the tree's Skunk Works tiles", names[-1], "RESTART_HMI")
    expect("5 tiles, 320x240, two a row: Restart HMI alone and centred on row 3",
           hazard_rig.tile_centre(4, 5, (30, 342, 320, 240)), (360, 342 + 2 * 260 + 120))
    expect("6 tiles: the sixth on the right of row 3",
           hazard_rig.tile_centre(5, 6, (30, 342, 320, 240)), (370 + 160, 342 + 2 * 260 + 120))
    expect("the first tile", hazard_rig.tile_centre(0, 5, (30, 342, 320, 240)), (190, 462))
    sent = []
    inj = hazard_rig.Injector(lambda h, v, tw, mask, seq: sent.append((time.monotonic(), seq)))
    try:
        inj.set(1507, 1510, 1477)
        expect("just refreshed: not due", inj.due(0.05), None)
        time.sleep(0.06)
        target = inj.due(0.05)
        expect("due after 50 ms", target, (1507, 1510, 1477, 0))
        inj.push_inline(target)
        expect("the inline refresh is a STICK with a fresh sequence number",
               len({seq for _, seq in sent}), len(sent))
        inj.pause()
        expect("paused: nothing due", inj.due(0.0), None)
    finally:
        inj.stop()


def t_sim_carry_over() -> None:
    line = ("refuse ENABLE=False refuse DISABLE=True ignore next 3 DISABLE(s), drop next 0 "
            "DISABLE(s), on HMI gone: keep, HMI back 1 time(s)")
    expect("ENABLED, refusing DISABLE, 3 ignores left: all carried, the state last",
           hazard_rig.carry_over(line, "ENABLED", False),
           ["ongone keep", "ign 3", "drop 0", "s", "a"])
    idle = line.replace("refuse ENABLE=False", "refuse ENABLE=True").replace(
        "refuse DISABLE=True", "refuse DISABLE=False").replace("keep", "idle")
    expect("IDLE, refusing ENABLE, paused", hazard_rig.carry_over(idle, "IDLE", True),
           ["ongone idle", "ign 3", "drop 0", "x", "ok", "p"])
    expect("no publish seen: modes only", hazard_rig.carry_over(line, None, False)[-1], "s")
    for bad in (("garbage", "IDLE"), (line, "BOOTING")):
        try:
            hazard_rig.carry_over(*bad, False)
        except hazard_rig.NotRun:
            continue
        raise AssertionError(f"{bad} was carried")
    with quiet(), tempfile.TemporaryDirectory() as tmp:
        st = hazard_rig.HazardStep("x", pathlib.Path(tmp))
        st.details.append(hazard_rig.SIM_RESTART_NOTE)
        st.check("c", True, "")
        r = st.result()
    expect("the approximation is in the verdict's detail",
           (r["verdict"], "RTPS rediscovery not supported" in r["detail"]), ("PASS", True))


def t_seat_targets_and_key_enter() -> None:
    elevation, minus = (30, 380, 320, 162, 6), (30, 520, 320, 162, 6)
    c3.check_seat_targets(elevation, minus)
    expect("touch points are the widgets' middles", c3.centre_of(minus), (190, 601))
    for what, b, m in (("the burger key, not '-' (the 2026-10-09 walk)", elevation,
                        (0, 1116, 720, 164, 6)),
                       ("no focus on the page", elevation, None),
                       ("not the function grid", (0, 1116, 720, 164, 1), minus),
                       ("the page did not open", elevation, elevation)):
        try:
            c3.check_seat_targets(b, m)
        except hazard_rig.NotRun as e:
            expect(f"{what}: a rig fault", e.kind, "bench")
            continue
        raise AssertionError(f"{what}: accepted")
    sent: list[str] = []

    class Hmi:
        def command(self, text):
            sent.append(text)
            if text == "KEY ENTER" and len(sent) > 2:
                raise OSError("lost")
            return "OK"
    with quiet(), tempfile.TemporaryDirectory() as tmp:
        st = hazard_rig.HazardStep("x", pathlib.Path(tmp))
        rig = hazard_rig.Rig("1.2.3.4", common.REPO, pathlib.Path(tmp), st, set(), proven="p")
        rig.hmi = Hmi()
        rig.key_enter()
        try:
            rig.key_enter()
        except OSError:
            pass
    expect("every KEY ENTER is released with KEY NONE, even when it fails", sent,
           ["KEY ENTER", "KEY NONE", "KEY ENTER", "KEY NONE"])
    sources = [pathlib.Path(m.__file__).read_text(encoding="utf-8") for m in (c1, c2, c3, c4)]
    expect("no step sends a bare KEY ENTER", any('"KEY ENTER"' in src for src in sources), False)


CASES = [
    ("BENCH-015 the hazard steps: only behind --hazard or --steps; retired as the fixes land; "
     "B5pp names", t_plan),
    ("BENCH-016 run_bench: one sole-sim proof before the hazard steps; without it they are "
     "INVALID and nothing is injected", t_run_bench_hazard),
    ("BENCH-017 B2 grades with the tree and the --hazard fixes", t_b2_wiring),
    ("BENCH-018 B2's self-test count comes from the tree's selftest_spec.hpp; only 54, 57, 60 "
     "are declared", t_boot_count),
    ("BENCH-019 B2: no task_wdt line once the tree turns idle checks off; C3's POST markers "
     "with --hazard c3", t_boot_wdt_and_post),
    ("BENCH-020 the sim child's sample file round-trips; injection needs the sole-sim proof",
     t_samples_and_proof),
    ("BENCH-021 the injector refreshes every 100 ms, sends one-cycle masks once, records lapses",
     t_injector),
    ("BENCH-022 B5''-1 grader: Drive within 3 s, no command, no motion, CENTRE_FIRST", t_b1),
    ("BENCH-023 B5''-1b, 1c graders: the 300 ms neutral hold and the resume", t_b1bc),
    ("BENCH-024 B5''-2, 3 graders: the MCB stops on its own (i, e, z)", t_b2_b3),
    ("BENCH-025 B5''-4, 4b graders: stale MibStatus, DISABLE obeyed and lost", t_b4),
    ("BENCH-026 B5''-5 grader: the stop obeyed", t_b5),
    ("BENCH-027 B5''-6, 6b, 8 graders: the ignored stop, its re-sends and fault", t_b6_b6b_b8),
    ("BENCH-028 B5''-7 grader: lost DISABLEs", t_b7),
    ("BENCH-029 B5''-9, 10 graders: calibration running, never calibrated", t_b9_b10),
    ("BENCH-030 B5''-11, 12 graders: the POST and stick-fault hooks", t_b11_b12),
    ("BENCH-031 B5''-13, 14, 15 graders: profile tap, entry from Seat, HMI reset", t_b13_b14_b15),
    ("BENCH-032 B5''-16 grader: five boots, the POST lines, the boot DISABLE", t_b16),
    ("BENCH-033 B5''-17, 17b graders: stick bumped, button held", t_b17),
    ("BENCH-034 B5''-18, 18b, 18c graders: ENABLED before POST, HMI reset", t_b18),
    ("BENCH-035 B5''-19, 20 graders: seat before POST, latched FAIL", t_b19_b20),
    ("BENCH-036 B5''-21, 22, 22b graders: unclean reset, the budget, one bad read", t_b21_b22),
    ("BENCH-037 B5f, B5g graders: XYTwist 0 after IDLE and after stale MibStatus", t_b5f_b5g),
    ("BENCH-038 B5h grader: G4 holds", t_b5h),
    ("BENCH-039 B5i, B5j graders: the stalls and the watchdog", t_b5i_b5j),
    ("BENCH-040 C2-1..3b graders: 6 mV, 0 mV, the rail, the limit edge", t_c2_mv_and_rail),
    ("BENCH-041 C2-4a..c graders: one, alternating and sparse bad cycles", t_c2_bad_cycles),
    ("BENCH-042 C2-5..7 graders: failed read, stale X/Y, NaN", t_c2_reads),
    ("BENCH-043 C2-8..10 graders: fault while driving, no auto-resume", t_c2_8_9_10),
    ("BENCH-044 C2-11, 12 graders: keys silent, the indicator persists", t_c2_11_12),
    ("BENCH-045 C2-13 is NOT_RUN (no calibration that writes flash); the C2 clean-up reboots; "
     "C2-14 grader: boot", t_c2_13_14),
    ("BENCH-046 C2-15, 16 graders: the soak's self test, ContinuousAdc starvation", t_c2_15_16),
    ("BENCH-047 stray peers exclude this run's sim as a process tree (venv launcher and its "
     "base-interpreter child), not its pid alone", t_process_tree),
    ("BENCH-048 a rig whose start-up fails stops the sim it started", t_rig_start_failure_stops_sim),
    ("BENCH-049 a sim that does not quit is killed with its whole tree", t_forced_stop_kills_tree),
    ("BENCH-050 Restart HMI's tile from the tree's actions_spec.h; a refresh goes before any "
     "command once it is 20 ms old", t_restart_tile_and_refresh),
    ("BENCH-052 a serial watch frees the port at stop() even on a quiet board; a sim that exits "
     "at once is started once more; a rig fault ends only its step", t_serial_watch_and_sim_retry),
    ("BENCH-053 after an HMI reboot the new sim gets the old one's modes and MCB state; the "
     "verdict detail says so", t_sim_carry_over),
    ("BENCH-054 at an HMI reboot the sim is retired at once and its successor gets the old "
     "modes and state before it can publish", t_sim_replaced_at_reboot),
    ("BENCH-055 B5''-1/4b/10's notice: NONE allowed only while the hold reason has no text "
     "(GATE_SHUT, CALIBRATING; C1 3.3), the notice shown after", t_b1_gate_shut),
    ("BENCH-056 STATE is polled through every button hold, so a stop's notice is timed from "
     "the hold's completion", t_hold_polls),
    ("BENCH-057 B5''-19 presses the Seat screen by touch on widgets FOCUS placed, checked "
     "first (a wrong one is NOT_RUN); KEY ENTER is always released", t_seat_targets_and_key_enter),
    ("BENCH-051 injection lapses come from the STICK send times in the remote-UI log, not "
     "the injector's own clock; a pause is not a lapse", t_stick_lapses),
]


if __name__ == "__main__":
    import ui_client
    sys.exit(ui_client.run_cases("tools/bench/hazard_selftest.py", CASES))
