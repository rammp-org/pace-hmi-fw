"""B5''-16..22: hazard fix C3 (POST) on the board (docs/plans/hazard-c3-spec.md §7). No motors.

Same rig as C1 (hazard_rig.py): the sim plays the MCB, the stick is injected after the
sole-sim proof, verdicts come from the sim's log, STATE polls (every 100 ms; "within X" is
from the triggering command's send time to the first poll that shows the result, C3 §7) and
the serial log. A real boot's POST ends before the remote UI is up, so the "stick bumped"
steps use POST RERUN (bench builds only, C3 Q12 = H2 approved). Every step ends Locked with
the stick centred; one that leaves POST FAIL latched restarts the HMI (a clean reset).

| Step | Sim | Pass if |
| --- | --- | --- |
| B5''-16 | IDLE | 5 boots: 19 POST PASS lines, POST RESULT PASS 19/19 <= 3000 ms; post PASS, indicator NONE, reset_reason SW; first DriveCommand a DISABLE, the only one in 10 s |
| B5''-17 | IDLE | y +25 %, POST RERUN: PENDING on joy.y_cal_off/y_noise, WAITING "Centre the joystick", never FAIL; hold refused (banner REFUSED_POST, no DriveCommand); centred: PASS within 2.5 s, indicator NONE; hold: 1 ENABLE |
| B5''-17b | IDLE | button held, POST RERUN: joy.button_idle, "Release the joystick button"; PASS within 2.5 s of release |
| B5''-18 | ENABLED, s | Drive, forward, POST RERUN: Drive stays; zero from RERUN + 0.2 s to PASS at >= 25 Hz; notice names the check; after PASS and 0.5 s centred: y > 0 within 0.3 s |
| B5''-18b | ongone keep, a, s | Restart HMI: first DriveCommand a DISABLE, exactly 1 in the link's first 2 s; no ENABLE; DriveScreen within 3 s of the link; zero until centred 0.5 s; serial POST RESULT PASS |
| B5''-18c | ongone keep, a | Restart HMI: 1 DISABLE; the sim goes IDLE; Locked for 10 s |
| B5''-19 | IDLE | y +25 %, POST RERUN, Seat: no SeatCommand for 2 s, banner REFUSED_POST; centred, PASS: exactly 1 SeatCommand |
| B5''-20 | IDLE | CAL UNSAVED, POST RERUN: FAIL joy.cal_saved, FAILED "The joystick must be calibrated first", FAIL 10 s; hold refused; Restart HMI clears it |
| B5''-21 | IDLE | CRASH: serial POST sys.clean_reset FAIL 0, POST RESULT FAIL; reset_reason PANIC, FAILED "Restarted after a fault: PANIC"; hold refused; Restart HMI: PASS, SW |
| B5''-22 | IDLE | fail_mask 7, POST RERUN: FAIL 3.0..3.7 s after, adc.valid, "timed out" |
| B5''-22b | IDLE | fail_mask 1 for one refresh in the first window: FAIL adc.valid (below 990), latched |

Banners: STATE's `banner` (owner, 2026-10-08), polled like everything else. Not scriptable,
recorded as not verified: the About screen's "Last reset" text.

B5''-18b's "DriveScreen within 3 s of the HMI's link" is timed from the serial log (owner,
2026-10-08): the arrival time of the firmware's screen-change line (SCREEN_LOG_RE) against
the sim's hmi_back, both on the runner's clock. With no such line the first STATE poll is
used and the step is never PASS on it: "partial: remote UI up after the window".
"""

from __future__ import annotations

import re
import time

import hazard_grade as hg
from hazard_rig import HazardStep, NotRun, Rig, SerialWatch
from scenario_c1 import (DRIVE, _drive_after, check_no_nonzero, check_resume, check_within,
                         to_drive_by_mcb, watch_disables)

# One check's line (TS-POST-05); not "POST RESULT", "POST reset reason:", "POST waiting:".
POST_LINE_RE = re.compile(r"POST (?!RESULT )(\S+\.\S+) (PASS|FAIL|SKIP) ")
POST_RESULT_RE = re.compile(r"POST RESULT (PASS|FAIL) (\d+)/(\d+) (\d+)")
SERIAL_S = 120.0
BOOTS = 5
BUMP = 0.25  # C3 B5''-17: y = centre + 25 % of (max - centre)
# The screen-change line the firmware logs when a screen loads (the owning lane adds it):
# "screen -> DriveScreen".
SCREEN_LOG_RE = re.compile(r"screen -> (\w+)")


def bumped(cal: hg.Cal) -> tuple[int, int, int]:
    v_min, v_c, v_max = cal.v
    return cal.vertical_at(round(v_c + BUMP * (v_max - v_c)))


def _post(value: str):
    return hg.field_is("post", value)


def _last_before(tr: hg.Trace, t: float) -> dict | None:
    polls = [s for tt, s in tr.states if tt < t]
    return polls[-1] if polls else None


def grade_serial_boot(st: HazardStep, text: str, tag: str) -> None:
    lines = POST_LINE_RE.findall(text)
    passed = [n for n, v in lines if v == "PASS"]
    st.check(f"{tag}: 19 'POST <name> PASS' lines", len(passed) == 19,
             f"{len(passed)} PASS of {len(lines)} POST lines")
    m = POST_RESULT_RE.search(text)
    st.check(f"{tag}: POST RESULT PASS 19/19 <ms> with ms <= 3000",
             m is not None and m.group(1) == "PASS" and m.group(2) == "19"
             and m.group(3) == "19" and int(m.group(4)) <= 3000,
             m.group(0) if m else "no POST RESULT line")


def grade_b16(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    for i in range(1, BOOTS + 1):
        tag = f"boot {i}"
        r = tr.mark(f"restart-{i}")
        b = tr.mark(f"back-{i}")
        grade_serial_boot(st, p["serial"].get(i, ""), tag)
        s = hg.first_state(tr.states, _post("PASS"), b)
        st.check(f"{tag}: STATE post PASS, indicator NONE, reset_reason SW",
                 s is not None and s[1].get("indicator") == "NONE"
                 and s[1].get("reset_reason") == "SW",
                 "no PASS poll" if s is None else
                 f"indicator {s[1].get('indicator')}, reset_reason {s[1].get('reset_reason')}")
        cmds = hg.drive_commands(tr.events, r, tr.marks.get(f"restart-{i + 1}", float("inf")))
        first = cmds[0] if cmds else None
        ok = first is not None and first["request"] == "DISABLE" and len(
            hg.drive_commands(tr.events, first["mono"], first["mono"] + 10.0)) == 1
        st.check(f"{tag}: the HMI's first DriveCommand is a DISABLE, the only one in 10 s",
                 ok, str([(c["request"], round(c["mono"] - r, 2)) for c in cmds[:4]]))


def grade_b17(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    rr, h1, c, h2 = (tr.mark(k) for k in ("rerun", "hold-1", "centred", "hold-2"))
    s = _last_before(tr, h1)
    st.check("after 3 s: post PENDING", s is not None and s.get("post") == "PENDING",
             str(s and s.get("post")))
    st.check("post_check one of joy.y_cal_off / joy.y_noise",
             s is not None and s.get("post_check") in ("joy.y_cal_off", "joy.y_noise"),
             str(s and s.get("post_check")))
    st.check("indicator WAITING 'Centre the joystick'",
             s is not None and s.get("indicator") == "WAITING"
             and s.get("indicator_text") == "Centre the joystick",
             str(s and (s.get("indicator"), s.get("indicator_text"))))
    fail = hg.first_state(tr.states, _post("FAIL"), rr, h2)
    st.check("never FAIL", fail is None, "" if fail is None else f"at {fail[0] - rr:.2f} s")
    cmds = hg.drive_commands(tr.events, h1, c)
    st.check("1st hold: no DriveCommand", not cmds, str([x["request"] for x in cmds]))
    check_banner(st, tr, "1st hold", "REFUSED_POST", h1, c)
    found = hg.first_state(tr.states, _post("PASS"), c)
    if check_within(st, "after centring: post PASS", found, c, 2.5) is not None:
        st.check("indicator NONE", found[1].get("indicator") == "NONE",
                 str(found[1].get("indicator")))
    en = hg.drive_commands(tr.events, h2, h2 + 3.5, "ENABLE")
    st.check("2nd hold: 1 ENABLE", len(en) == 1, f"{len(en)}")


def check_banner(st: HazardStep, tr: hg.Trace, what: str, banner: str, t0: float,
                 t1: float) -> None:
    seen = hg.first_state(tr.states, hg.field_is("banner", banner), t0, t1)
    st.check(f"{what}: banner {banner}", seen is not None,
             "never" if seen is None else f"at +{seen[0] - t0:.2f} s")


def grade_b17b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    rel = tr.mark("release")
    s = _last_before(tr, rel)
    st.check("post_check joy.button_idle, 'Release the joystick button'",
             s is not None and s.get("post_check") == "joy.button_idle"
             and s.get("indicator_text") == "Release the joystick button",
             str(s and (s.get("post_check"), s.get("indicator_text"))))
    check_within(st, "PASS after the release", hg.first_state(tr.states, _post("PASS"), rel),
                 rel, 2.5)


def grade_b18(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    rr, c, f = tr.mark("rerun"), tr.mark("centred"), tr.mark("forward")
    ok, n, bad = hg.all_states(tr.states, hg.screen_is(DRIVE), rr, f + 2.0)
    st.check("DriveScreen stays (M1)", ok, f"{n} polls; first other {bad}")
    # The runner takes the rerun on its next tick: the first polls after it may still say PASS.
    left = hg.first_state(tr.states, lambda s: s.get("post") != "PASS", rr)
    ps = hg.first_state(tr.states, _post("PASS"), left[0]) if left else None
    if not st.check("STATE shows PASS after the rerun (and a gate that left PASS first)",
                    ps is not None, "the gate never left PASS" if left is None else ""):
        return
    t_pass = ps[0]
    check_no_nonzero(st, "every XYTwist from RERUN + 0.2 s until PASS is zero", tr, rr + 0.2,
                     t_pass)
    hz = hg.rate_hz(tr.samples, rr + 0.2, t_pass)
    st.check("... at >= 25 Hz", hz >= 25.0, f"{hz:.1f} Hz")
    pending = [s for t, s in hg.states_in(tr.states, rr + 0.5, t_pass) if s.get("post") != "PASS"]
    st.check("notice names the check (POST_NOT_PASSED, post_check set)",
             bool(pending) and all(s.get("notice") == "POST_NOT_PASSED" and s.get("post_check")
                                   for s in pending),
             f"{len(pending)} polls; {[(s.get('notice'), s.get('post_check')) for s in pending[:2]]}")
    st.check("PASS and 0.5 s centred before the forward", t_pass <= f - 0.5 and c <= f - 0.5,
             f"PASS at forward - {f - t_pass:.2f} s, centred at forward - {f - c:.2f} s")
    check_resume(st, tr, f)


def _link_after(tr: hg.Trace, r: float) -> float | None:
    # hmi_back from a sim that saw the reboot; hmi_seen from the sim that replaced it
    e = hg.first_event(tr.events, lambda e: e.get("ev") in ("hmi_back", "hmi_seen"), r)
    return None if e is None else e["mono"]


def grade_b18b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    r, b, c = tr.mark("restart"), tr.mark("back"), tr.mark("centred")
    link = _link_after(tr, r)
    if not st.check("the HMI's link after the restart (the sim's hmi_back)", link is not None, ""):
        return
    cmds = hg.drive_commands(tr.events, r)
    st.check("the HMI's first DriveCommand after the reboot is a DISABLE",
             bool(cmds) and cmds[0]["request"] == "DISABLE",
             str([x["request"] for x in cmds[:3]]))
    first2 = hg.drive_commands(tr.events, link, link + 2.0)
    st.check("exactly 1 in its first 2 s of link, a DISABLE",
             len(first2) == 1 and first2[0]["request"] == "DISABLE",
             str([x["request"] for x in first2]))
    st.check("no ENABLE", not hg.drive_commands(tr.events, r, float("inf"), "ENABLE"), "")
    screen_line = next((t for t, line in p.get("serial_lines", [])
                        if t >= r and (m := SCREEN_LOG_RE.search(line)) and m.group(1) == DRIVE),
                       None)
    if screen_line is not None:
        dt = screen_line - link
        st.check("DriveScreen within 3.0 s of the HMI's link (serial screen-change line)",
                 dt <= 3.0, f"after {dt:.2f} s")
    else:
        d = _drive_after(tr, b)
        seen = "never" if d is None else f"first poll at link + {d[0] - link:.2f} s"
        st.record("drive_after_link_fallback", seen)
        st.partial = ("remote UI up after the window: no screen-change line in the serial "
                      f"log, DriveScreen from the first STATE poll only ({seen})")
    check_no_nonzero(st, "no non-zero XYTwist until the script centres 0.5 s", tr, r, c + 0.5)
    st.check("serial POST RESULT PASS present", "POST RESULT PASS" in p.get("serial_text", ""),
             "" if p.get("serial_text") else "no serial capture")


def grade_b18c(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    r, b = tr.mark("restart"), tr.mark("back")
    dis = hg.drive_commands(tr.events, r, float("inf"), "DISABLE")
    st.check("1 DISABLE", len(dis) == 1, f"{len(dis)}")
    idle = hg.first_event(tr.events, lambda e: e.get("ev") == "mib_state"
                          and e.get("state") == "IDLE", r)
    st.check("the sim goes IDLE", idle is not None, "")
    ok, n, bad = hg.all_states(tr.states, lambda s: s.get("phase") == "LOCKED"
                               and s.get("screen") != DRIVE, b, b + 10.0)
    st.check("the HMI stays Locked for 10 s", ok, f"{n} polls; first other {bad}")


def grade_b19(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    s1, c, s2 = tr.mark("seat-1"), tr.mark("centred"), tr.mark("seat-2")
    first = hg.seat_commands(tr.events, s1, min(s1 + 2.0, c))
    st.check("1st press: no SeatCommand for 2 s", not first and s1 + 2.0 <= c,
             f"{len(first)} SeatCommand(s); centred {c - s1:.1f} s after the press")
    check_banner(st, tr, "1st press", "REFUSED_POST", s1, c)
    second = hg.seat_commands(tr.events, s2, s2 + 2.0)
    st.check("2nd press: exactly 1 SeatCommand", len(second) == 1, f"{len(second)}")


def grade_b20(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    rr, h, back = tr.mark("rerun"), tr.mark("hold"), tr.mark("back")
    s = _last_before(tr, h)
    st.check("post FAIL, post_check joy.cal_saved",
             s is not None and s.get("post") == "FAIL" and s.get("post_check") == "joy.cal_saved",
             str(s and (s.get("post"), s.get("post_check"))))
    st.check("indicator FAILED 'The joystick must be calibrated first'",
             s is not None and s.get("indicator") == "FAILED"
             and "The joystick must be calibrated first" in (s.get("indicator_text") or ""),
             str(s and (s.get("indicator"), s.get("indicator_text"))))
    ok, n, bad = hg.all_states(tr.states, _post("FAIL"), rr + 3.0, rr + 13.0)
    st.check("stays FAIL for 10 s with good windows", ok, f"{n} polls; first other {bad}")
    cmds = hg.drive_commands(tr.events, h, h + 3.5)
    st.check("hold refused (no DriveCommand)", not cmds, str([x["request"] for x in cmds]))
    ps = hg.first_state(tr.states, _post("PASS"), back)
    st.check("Restart HMI clears it (post PASS after the reboot)", ps is not None, "")


def grade_b21(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    b, h, b2 = tr.mark("back"), tr.mark("hold"), tr.mark("back-2")
    text = p.get("serial_text", "")
    st.check("serial: POST sys.clean_reset FAIL 0",
             re.search(r"POST sys\.clean_reset FAIL 0\b", text) is not None, "")
    m = POST_RESULT_RE.search(text)
    st.check("serial: POST RESULT FAIL", m is not None and m.group(1) == "FAIL",
             m.group(0) if m else "no POST RESULT line")
    s = hg.first_state(tr.states, _post("FAIL"), b, h)
    st.check("STATE reset_reason PANIC, indicator FAILED 'Restarted after a fault: PANIC'",
             s is not None and s[1].get("reset_reason") == "PANIC"
             and s[1].get("indicator") == "FAILED"
             and "Restarted after a fault: PANIC" in (s[1].get("indicator_text") or ""),
             "no FAIL poll" if s is None else
             str((s[1].get("reset_reason"), s[1].get("indicator"), s[1].get("indicator_text"))))
    cmds = hg.drive_commands(tr.events, h, h + 3.5)
    st.check("unlock hold refused", not cmds, str([x["request"] for x in cmds]))
    ps = hg.first_state(tr.states, _post("PASS"), b2)
    st.check("Restart HMI: PASS, reset_reason SW",
             ps is not None and ps[1].get("reset_reason") == "SW",
             "no PASS poll" if ps is None else str(ps[1].get("reset_reason")))


def grade_b22(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    rr = tr.mark("rerun")
    f = hg.first_state(tr.states, _post("FAIL"), rr)
    st.check("post FAIL between 3.0 and 3.7 s after the RERUN",
             f is not None and 3.0 <= f[0] - rr <= 3.7,
             "never" if f is None else f"after {f[0] - rr:.2f} s")
    st.check("post_check adc.valid, 'timed out'",
             f is not None and f[1].get("post_check") == "adc.valid"
             and "timed out" in (f[1].get("indicator_text") or ""),
             "" if f is None else str((f[1].get("post_check"), f[1].get("indicator_text"))))


def grade_b22b(st: HazardStep, tr: hg.Trace, p: dict) -> None:
    rr = tr.mark("rerun")
    f = hg.first_state(tr.states, _post("FAIL"), rr)
    st.check("post FAIL, adc.valid", f is not None and f[1].get("post_check") == "adc.valid",
             "never" if f is None else str(f[1].get("post_check")))
    if f is not None:
        ok, n, bad = hg.all_states(tr.states, _post("FAIL"), f[0], float("inf"))
        st.check("latched", ok, f"{n} polls; first other {bad}")
    text = p.get("serial_text")
    if text:
        m = re.search(r"POST adc\.valid FAIL (\d+)", text)
        st.check("adc.valid below 990", m is not None and int(m.group(1)) < 990,
                 m.group(0) if m else "no POST adc.valid line")
    else:
        st.not_verified.append("adc.valid's value (no serial port given)")


# ---------------------------------------------------------------- scripts


def _serial(rig: Rig, seconds: float = SERIAL_S) -> SerialWatch:
    if not rig.port:
        raise NotRun("bench", "this step reads the serial log: run it with the board's port")
    return SerialWatch(rig.port, seconds)


def _reboot(rig: Rig, tag: str = "", serial_s: float = 0.0) -> float:
    rig.mark(f"restart{tag}")
    t = rig.restart_hmi(serial_s)
    rig.wait_back()
    rig.mark(f"back{tag}")
    return t


def s_b16(rig: Rig) -> dict:
    serial: dict[int, str] = {}
    for i in range(1, BOOTS + 1):
        rig.begin()
        watch = _serial(rig)
        rig.inj.pause()  # "stick centred (no injection)": the real stick at rest
        _reboot(rig, f"-{i}")
        rig.watch(15.0, until=_post("PASS"))
        t_dis = watch_disables(rig, f"restart-{i}", 30.0)
        rig.watch(max(1.0, (t_dis or time.monotonic()) + 10.5 - time.monotonic()))
        serial[i] = watch.stop()
    rig.st.not_verified.append("About 'Last reset: software' (the screen's text)")
    return {"serial": serial}


def s_b17(rig: Rig) -> dict:
    rig.begin()
    rig.need("banner")
    rig.inject(*bumped(rig.cal))
    rig.mark("rerun")
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    rig.watch(3.0)
    rig.mark("hold-1")
    rig.hold_button()
    rig.watch(1.5)
    rig.mark("centred")
    rig.centre()
    rig.watch(3.0, until=_post("PASS"))
    rig.watch(0.3)
    rig.mark("hold-2")
    rig.hold_button()
    rig.watch(3.0)
    return {}


def s_b17b(rig: Rig) -> dict:
    rig.begin()
    rig.hmi.command("BTN 1")
    rig.mark("rerun")
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    rig.watch(3.0)
    rig.mark("release")
    rig.hmi.command("BTN 0")
    rig.watch(3.0, until=_post("PASS"))
    return {}


def s_b18(rig: Rig) -> dict:
    rig.begin()
    rig.need("notice")  # "notice names the check": an image without the hook is NOT_RUN
    rig.sim_toggle("s", True)
    to_drive_by_mcb(rig)
    rig.watch(0.5)  # centred
    rig.forward()
    rig.mark("rerun")
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    rig.watch(5.0)  # forward held
    rig.mark("centred")
    rig.centre()
    rig.watch(3.0)
    rig.mark("forward")
    rig.forward()
    rig.watch(2.1)
    return {}


def s_b18b(rig: Rig) -> dict:
    rig.begin()
    rig.sim_ongone("keep")
    rig.sim_toggle("s", True)
    to_drive_by_mcb(rig)
    if not rig.port:
        raise NotRun("bench", "this step reads the serial log: run it with the board's port")
    rig.inj.pause()
    _reboot(rig, serial_s=SERIAL_S)  # from DriveScreen: an RTS reset whose capture reads on
    watch = rig.restart_watch
    rig.forward()  # "inject forward from the remote UI's reconnect"
    rig.watch(5.0, until=hg.screen_is(DRIVE))
    rig.watch(2.0)
    rig.mark("centred")
    rig.centre()
    rig.watch(0.6)
    rig.mark("forward")
    rig.forward()
    rig.watch(1.0)
    text = watch.stop()
    return {"serial_text": text, "serial_lines": watch.lines}


def s_b18c(rig: Rig) -> dict:
    rig.begin()
    rig.need("phase")
    rig.sim_ongone("keep")
    to_drive_by_mcb(rig)
    rig.inj.pause()
    _reboot(rig)
    rig.centre()
    rig.watch(10.2)
    return {}


SEAT_BUTTONS = 6  # SeatView's function grid: 3 rows of 2 (seat_view.cpp fill_grids)
SEAT_ADJUST = 6   # its adjustment page: back; "-", "+"; three presets


def centre_of(focus: tuple) -> tuple[int, int]:
    """The middle of a FOCUS rect (x, y, w, h, groupsize)."""
    x, y, w, h = focus[:4]
    return x + w // 2, y + h // 2


def check_seat_targets(buttons: tuple | None, minus: tuple | None) -> None:
    """The two widgets B5''-19 presses, as FOCUS reported them with the stick centred:
    the Seat screen's first function button (Elevation, ui_SeatButton1: the cursor's place
    on arrival) and its adjustment page's "-" (ui_SeatAdjustmentButton1). Anything else is
    a rig fault, never a firmware verdict (on 2026-10-09 a keypad walk under the bumped
    stick ended on the burger key)."""
    # The keypad group may hold one more object than the grid (05c22b5: 7 for 6 buttons);
    # the button itself is checked by its size (ui_SeatButton1: 320x162).
    if buttons is None or buttons[4] < SEAT_BUTTONS or tuple(buttons[2:4]) != (320, 162):
        raise NotRun("bench", f"Seat screen: the function grid is not focused (FOCUS {buttons})")
    if minus is None or minus[4] < SEAT_ADJUST:
        raise NotRun("bench", f"Seat screen: the adjustment page is not focused (FOCUS {minus})")
    if tuple(minus[:4]) == tuple(buttons[:4]) or minus[2] >= 720:
        raise NotRun("bench", f"Seat screen: '-' not found (FOCUS {minus}, button {buttons})")


def seat_targets(rig: Rig) -> tuple[int, int]:
    """With the stick centred (no stick keys): open the Seat screen, read Elevation's place
    (FOCUS), tap it by touch, walk the adjustment page's focus to '-' (up to the back row,
    one down) and read its place; the page stays open. Returns where '-' is."""
    rig.go("Seat Functions")
    if rig.watch(3.0, until=hg.screen_is("SeatScreen")) is None:
        raise NotRun("bench", "could not open the Seat screen")
    buttons = rig.focus()
    if buttons is None or buttons[4] < SEAT_BUTTONS or tuple(buttons[2:4]) != (320, 162):
        check_seat_targets(buttons, None)  # raises: not the function grid
    rig.tap(*centre_of(buttons))
    time.sleep(0.3)  # the page change
    for key in ("UP", "UP", "DOWN"):
        rig.hmi.nudge(key)
        time.sleep(0.2)  # one keypad read
    minus = rig.focus()
    rig.st.record("seat_targets", {"elevation": buttons, "minus": minus})
    check_seat_targets(buttons, minus)
    return centre_of(minus)


def seat_press(rig: Rig, minus: tuple[int, int]) -> None:
    """One seat request by touch: '-' on the open adjustment page. The target is checked
    first: still the Seat screen, no menu over it."""
    s = rig.state()
    if s.get("screen") != "SeatScreen" or s.get("menu_open"):
        raise NotRun("bench", f"seat press: not on the Seat screen ({s.get('screen')}, "
                     f"menu {s.get('menu_open')})")
    rig.tap(*minus)


def s_b19(rig: Rig) -> dict:
    rig.begin()
    rig.need("banner")
    minus = seat_targets(rig)  # stick centred: no stick key moves the focus
    rig.inject(*bumped(rig.cal))
    rig.mark("rerun")
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    rig.watch(2.0, until=_post("PENDING"))
    rig.mark("seat-1")
    seat_press(rig, minus)
    rig.watch(2.0)
    rig.mark("centred")
    rig.centre()
    rig.watch(3.0, until=_post("PASS"))
    rig.mark("seat-2")
    seat_press(rig, minus)  # the same "-", now with POST passed
    rig.watch(2.1)
    rig.home()
    return {}


def s_b20(rig: Rig) -> dict:
    rig.begin()
    rig.bench_verb(rig.hmi.cal_unsaved, "CAL UNSAVED")
    t_rr = rig.mark("rerun")
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    rig.watch(3.0)
    rig.mark("hold")
    rig.hold_button()
    rig.watch(max(0.0, t_rr + 13.5 - time.monotonic()))  # FAIL for 10 s from rerun + 3 s
    rig.inj.pause()
    _reboot(rig)
    rig.centre()
    rig.watch(15.0, until=_post("PASS"))
    return {}


def s_b21(rig: Rig) -> dict:
    rig.begin()
    watch = _serial(rig, 150.0)
    rig.mark("crash")
    rig.crash()
    rig.wait_back()
    rig.mark("back")
    rig.centre()
    rig.watch(10.0, until=_post("FAIL"))
    rig.watch(0.5)
    rig.mark("hold")
    rig.hold_button()
    rig.watch(3.0)
    rig.inj.pause()
    _reboot(rig, "-2")
    rig.centre()
    rig.watch(15.0, until=_post("PASS"))
    rig.watch(0.5)
    rig.st.not_verified.append("About 'Last reset: PANIC' (the screen's text)")
    return {"serial_text": watch.stop()}


def s_b22(rig: Rig) -> dict:
    rig.begin()
    rig.inject(*rig.cal.centre(), mask=7)
    rig.mark("rerun")
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    rig.watch(5.0, until=_post("FAIL"))
    rig.watch(0.5)
    rig.centre()
    return {}


def s_b22b(rig: Rig) -> dict:
    rig.begin()
    watch = SerialWatch(rig.port, 30.0) if rig.port else None
    rig.mark("rerun")
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    time.sleep(0.4)  # inside the first window (30 cycles, ~1.05 s)
    rig.inject(*rig.cal.centre(), mask=1, once=True)
    rig.watch(3.0, until=_post("FAIL"))
    rig.watch(5.0)
    return {"serial_text": watch.stop() if watch else ""}


def restart_cleanup(rig: Rig) -> None:
    """POST FAIL latches until a clean reset: Restart HMI, then the usual clean-up."""
    rig.inj.pause()
    rig.restart_hmi()
    rig.wait_back()
    rig.centre()
    rig.watch(15.0, until=_post("PASS"))


STEPS = {
    "B5''-16": (s_b16, grade_b16, "POST at boot, 5 boots (serial)"),
    "B5''-17": (s_b17, grade_b17, "stick bumped (POST RERUN)"),
    "B5''-17b": (s_b17b, grade_b17b, "button held (POST RERUN)"),
    "B5''-18": (s_b18, grade_b18, "ENABLED before POST pass"),
    "B5''-18b": (s_b18b, grade_b18b, "HMI reset, MCB ENABLED, ignores DISABLE (serial)"),
    "B5''-18c": (s_b18c, grade_b18c, "HMI reset, MCB ENABLED, obeys"),
    "B5''-19": (s_b19, grade_b19, "seat before POST"),
    "B5''-20": (s_b20, grade_b20, "latched hardware FAIL (CAL UNSAVED)"),
    "B5''-21": (s_b21, grade_b21, "reset reason, unclean (CRASH, serial)"),
    "B5''-22": (s_b22, grade_b22, "POST budget (all reads fail)"),
    "B5''-22b": (s_b22b, grade_b22b, "one bad read in the first window"),
}
# B5''-18b and -18c restart the HMI from DriveScreen, where the burger key is a stop
# request and the menu cannot be reached: they restart through the port (an RTS reset).
NEEDS_SERIAL = {"B5''-16", "B5''-18b", "B5''-18c", "B5''-21"}
CLEANUP = {"B5''-22": restart_cleanup, "B5''-22b": restart_cleanup}
