"""B5s-1..17: the seat-path hazard fix on the board (docs/plans/hazard-seat-spec.md §6). No motors.

Same rig as C1-C2 (hazard_rig.py): the sim plays the MCB (no chair, no actuators), and the
rig's preflight proves it is the only other RTPS participant. Verdicts come from the sim's
event log (`seat_command`, `seat_move`, `mib_publish` with the seat values), STATE (polled every
100 ms; its seat keys, REQ-RUI-09) and the serial log. Every step starts with the sim IDLE, POST
PASS, the stick centred and OK, every seat mode off and every axis known, and ends Locked.
"Within X" is from the triggering command's send time (its mark) to the first STATE poll or sim
event that shows the result (§6).

| Step | Sim | Pass if |
| --- | --- | --- |
| B5s-1 | IDLE | Seat, Elevation "+": exactly 1 SeatCommand, axis 2, reported + 5.0 mm, <= 0.5 s; seat[2] shows it <= 1.0 s |
| B5s-2 | seatval 2 nan | seat[2] null <= 1.0 s; "+" and "15" each: 0 SeatCommands for 2 s, POSITION_UNKNOWN, REFUSED_SEAT_UNKNOWN; no reboot; off: known <= 1.0 s, then exactly 1 |
| B5s-3 | seatval 2 inf, -inf, 1e30, -1e30 | as B5s-2 for each |
| B5s-4 | seatval 2 255.0, then 255.1 | 255.0 known; "+": 0, AT_LIMIT, no banner; "-": 1, 250.0; 255.1: null |
| B5s-5 | p, r (Actuators) | all null 1.5..2.9 s after p; 0 SeatCommands p..r + 2 s; known <= 1.0 s of r; nothing caused by r |
| B5s-6 | seatslow on, p, r | 1 SeatCommand in [press, r + 5 s]; Locked with REFUSED_SEAT <= 2.9 s of p; seat_move through the pause |
| B5s-7 | seatslow on, Restart HMI | 0 SeatCommands restart..press; null until the first MibStatus; POST RESULT PASS; "+": 1, reported + step |
| B5s-8 | e, z, ok | Actuators KEY RIGHT: e, z 0 and MCB_NOT_IDLE, ok 1 (+5.0); Skunk "Seat up": e, z 0 (greyed, gate not reached), ok 1 |
| B5s-9 | IDLE | B5''-19 on the Seat screen, then the Actuators page and Skunk Works: 0 before PASS (POST_NOT_PASSED), 1 after |
| B5s-10 | IDLE | stick FAULT: "+" 0, REFUSED_STICK; after Restart HMI exactly 1 |
| B5s-11 | IDLE | Actuators KEY RIGHT held 5 s: exactly 1 SeatCommand in [key, key + 6 s] |
| B5s-12 | IDLE | PRESS on "+" 5 s: 0 during the hold, at most 1 within 0.5 s of RELEASE |
| B5s-13 | IDLE | SEATFAIL 1, "+": 0 at the sim, failures +1, REFUSED_SEAT_NOT_SENT, serial line; nothing 5 s; "+": 1 |
| B5s-14 | seatdrop 1 | "+": 1 dropped, no other in 5 s; failures unchanged; value unchanged |
| B5s-15 | IDLE | 10 taps on "+": every target <= the latest published elevation + 5.0 mm |
| B5s-16 | e; IDLE + stick FAULT | menu Seat Functions: refused, the reason's banner, the menu stays, 0 SeatCommands |
| B5s-17 | seatval 2 100 | seat[2] = 1000 within 1.0 s; 0 SeatCommands |

Not scriptable, recorded as not verified: the banners' words (STATE names the banner, not its
text), ErrorBanner6 on the Actuators page (no STATE field: B5s-5), a greyed tile's look.
Elevation is axis 2 (RAMMP_SEAT_AXIS_TABLE), the Seat screen's second-row first button
(SeatView::button_axis) and the Actuators page's third row.
"""

from __future__ import annotations

import contextlib
import datetime
import math
import re
import time

import hazard_grade as hg
import scenario_c2
from hazard_rig import NotRun, Rig, SerialWatch, skunk_actions, tile_centre

ELEVATION = 2
EL_STEP_MM = 5.0             # RAMMP_SEAT_AXIS_TABLE: elevation step 50 raw, 1 decimal
EL_MIN_RAW, EL_MAX_RAW, EL_STEP_RAW = 0, 2500, 50
AXES = 4
SERIAL_S = 150.0
SEAT_UP_ACTION = "SEAT_UP"
# A SettingRow (ui_comp_settingrow.c): a 700 px flex row, centred, 30 px gaps: Head 136,
# ValueBox 200, Minus 132, Plus 132 -> Plus' centre is 629 px from the row's left edge.
ROW_PLUS_DX = (700 - (136 + 200 + 132 + 132 + 3 * 30)) // 2 + 136 + 30 + 200 + 30 + 132 + 30 + 66
FAULT_RE = re.compile(r"SeatCommand ELEVATION not published \((\d+) failed since start\)")
BOOT_RE = re.compile(r"rst:0x|ESP-ROM:|POST RESULT")
POST_PASS_RE = re.compile(r"POST RESULT PASS")


# ---------------------------------------------------------------- reading STATE and the log


def seat_raw(s: dict, axis: int = ELEVATION):
    """STATE's raw value for `axis`, or None (unknown, or no seat keys)."""
    seat = s.get("seat")
    return seat[axis] if isinstance(seat, list) and len(seat) > axis else None


def all_null(s: dict) -> bool:
    seat = s.get("seat")
    return isinstance(seat, list) and len(seat) == AXES and all(v is None for v in seat)


def all_known(s: dict) -> bool:
    seat = s.get("seat")
    return isinstance(seat, list) and len(seat) == AXES and all(v is not None for v in seat)


def published(events: list[dict], before: float, axis: int = ELEVATION) -> float | None:
    """The seat value (whole units) of the last MibStatus that reached the HMI before `before`."""
    out = None
    for e in hg.publishes(events, -math.inf, before):
        seat = e.get("seat")
        if isinstance(seat, list) and len(seat) > axis:
            out = seat[axis]
    return out


def sent(events: list[dict], t0: float, t1: float = math.inf) -> list[dict]:
    """SeatCommands the sim received and did not drop."""
    return [e for e in hg.seat_commands(events, t0, t1) if e.get("result") != "DROPPED"]


def dropped(events: list[dict], t0: float, t1: float = math.inf) -> list[dict]:
    return [e for e in hg.seat_commands(events, t0, t1) if e.get("result") == "DROPPED"]


def last_state(tr: hg.Trace, before: float) -> dict:
    polls = [s for t, s in tr.states if t < before]
    return polls[-1] if polls else {}


def first_state_after(tr: hg.Trace, after: float) -> dict:
    return next((s for t, s in tr.states if t >= after), {})


def count(s: dict, key: str) -> int:
    v = s.get(key)
    return v if isinstance(v, int) else -1


# ---------------------------------------------------------------- shared checks


def check_one(st, tr: hg.Trace, what: str, t0: float, t1: float, target: float | None = None,
              within: float | None = None) -> None:
    """Exactly one SeatCommand on elevation in [t0, t1); with `target`, that target (whole
    units); with `within`, no later than t0 + within."""
    cmds = sent(tr.events, t0, t1)
    ok = len(cmds) == 1 and cmds[0].get("axis") == ELEVATION
    st.check(f"{what}: exactly 1 SeatCommand, elevation", ok,
             f"{[(round(e['mono'] - t0, 2), e.get('axis'), e.get('target')) for e in cmds]}")
    if not ok:
        return
    if target is not None:
        st.check(f"{what}: target {target:.1f} mm", abs(cmds[0].get("target", 1e9) - target) < 1e-3,
                 f"target {cmds[0].get('target')}")
    if within is not None:
        dt = cmds[0]["mono"] - t0
        st.check(f"{what}: within {within:.1f} s of the press", dt <= within, f"{dt:.2f} s")


def check_none(st, tr: hg.Trace, what: str, t0: float, t1: float) -> None:
    cmds = hg.seat_commands(tr.events, t0, t1)
    st.check(f"{what}: no SeatCommand", not cmds,
             f"{[(round(e['mono'] - t0, 2), e.get('target'), e.get('result')) for e in cmds]}")


def check_refused(st, tr: hg.Trace, what: str, t0: float, t1: float, reason: str,
                  banner: str | None) -> None:
    """A refused press at t0: no SeatCommand until t1, seat_reason `reason`, banner `banner`
    (None: no seat banner) in a poll after the press."""
    check_none(st, tr, what, t0, t1)
    before = count(last_state(tr, t0), "seat_refused")
    hit = hg.first_state(tr.states, lambda s: count(s, "seat_refused") > before, t0, t1)
    st.check(f"{what}: refused once, seat_reason {reason}",
             hit is not None and count(hit[1], "seat_refused") == before + 1
             and hit[1].get("seat_reason") == reason,
             f"refused {before} -> {count(hit[1], 'seat_refused') if hit else '-'}, "
             f"seat_reason {hit[1].get('seat_reason') if hit else '-'}")
    if banner is not None:
        b = hg.first_state(tr.states, hg.field_is("banner", banner), t0, t1)
        st.check(f"{what}: banner {banner}", b is not None,
                 f"banner {first_state_after(tr, t0 + 0.5).get('banner')}")


def check_no_boot(st, p: dict, what: str) -> None:
    text = p.get("serial_text")
    if text is None:
        st.not_verified.append(f"{what}: no reboot (no serial port given)")
        return
    m = BOOT_RE.search(text)
    st.check(f"{what}: no reboot (no boot line in the serial log)", m is None,
             m.group(0) if m else "")


# ---------------------------------------------------------------- the sim's seat modes


def sim_seat(rig: Rig, cmd: str, regex: str) -> None:
    if rig.sim.reply(cmd, regex) is None:
        raise NotRun("bench", f"the sim did not take {cmd!r}")
    rig.trace.sent.append((round(time.monotonic(), 3), f"sim {cmd}"))


def seatval(rig: Rig, axis: int, value: str) -> None:
    sim_seat(rig, f"seatval {axis} {value}", rf"seat axis {axis} publishes ")


def seat_modes_off(rig: Rig) -> None:
    for axis in range(AXES):
        seatval(rig, axis, "off")
    sim_seat(rig, "seatdrop 0", r"dropping the next 0 SeatCommand")
    sim_seat(rig, "seatslow off", r"seat slow: off")


def seat_begin(rig: Rig) -> None:
    """The step's start (§6): the rig's (IDLE, POST PASS, centred, Locked), every seat mode
    off, the seat keys in STATE and every axis known."""
    rig.begin()
    seat_modes_off(rig)
    rig.need("seat", "seat_reason")
    s = rig.watch(3.0, until=all_known)
    rig.st.check("set-up: every seat axis known", s is not None, str(rig.state().get("seat")))
    if s is None:
        raise NotRun("bench", "set-up failed: a seat axis is not known")


def seat_cleanup(rig: Rig) -> None:
    """Every seat mode off, then a latched stick FAULT cleared by a reboot (C2's clean-up)."""
    seat_modes_off(rig)
    scenario_c2.reboot_cleanup(rig)


# ---------------------------------------------------------------- navigation


@contextlib.contextmanager
def navigating(rig: Rig):
    """A navigation that succeeded is a set-up span (hazard_rig.Rig.setup_span): no grader reads
    it, so an injection lapse inside it (a screen built on demand holds the remote UI ~0.7 s:
    Settings, Skunk Works, on 2026-10-11) does not void the step. The seat steps grade no
    motion; the stick only has to stay centred, which the board's own stick at rest also is."""
    start = datetime.datetime.now()
    yield
    rig.setup_span(start)


# ---------------------------------------------------------------- the Seat screen


def _focus(rig: Rig, what: str):
    f = rig.focus()
    if f is None:
        raise NotRun("bench", f"FOCUS gave nothing for {what}")
    return f


def centre_of(f: tuple) -> tuple[int, int]:
    x, y, w, h = f[:4]
    return x + w // 2, y + h // 2


def seat_map(rig: Rig) -> dict:
    with navigating(rig):
        return _seat_map(rig)


def _seat_map(rig: Rig) -> dict:
    """With the stick centred: the Seat screen's Elevation button and its adjustment page's
    "-", "+" and three presets, placed by FOCUS (the keypad walks them; nothing is pressed
    but the Elevation button, which opens the page). The page stays open."""
    rig.go("Seat Functions")
    if rig.watch(3.0, until=hg.screen_is("SeatScreen")) is None:
        raise NotRun("bench", "could not open the Seat screen")
    first = _focus(rig, "the function grid")
    rig.hmi.nudge("DOWN")
    time.sleep(0.2)  # one keypad read
    elevation = _focus(rig, "Elevation")
    if elevation[:2] == first[:2] or tuple(elevation[2:4]) != tuple(first[2:4]):
        raise NotRun("bench", f"Seat screen: Elevation not below the first button "
                     f"({first}, {elevation})")
    m = {"elevation": centre_of(elevation)}
    rig.tap(*m["elevation"])
    time.sleep(0.3)  # the page change
    for key in ("UP", "UP", "DOWN"):
        rig.hmi.nudge(key)
        time.sleep(0.2)
    minus = _focus(rig, "-")
    for name, key in (("minus", None), ("plus", "RIGHT"), ("p0", "DOWN"), ("p15", "RIGHT"),
                      ("p25", "RIGHT")):
        if key is not None:
            if name == "p0":
                rig.hmi.nudge("LEFT")  # back to "-", then down to the first preset
                time.sleep(0.2)
            rig.hmi.nudge(key)
            time.sleep(0.2)
        m[name] = centre_of(_focus(rig, name))
    if len({m[k] for k in ("minus", "plus", "p0", "p15", "p25")}) != 5 or minus[2] >= 720:
        raise NotRun("bench", f"Seat screen: the adjustment page is not as expected ({m})")
    rig.st.record("seat_map", m)
    return m


def open_elevation_by_touch(rig: Rig, m: dict) -> None:
    with navigating(rig):
        _open_elevation_by_touch(rig, m)


def _open_elevation_by_touch(rig: Rig, m: dict) -> None:
    """The Seat screen by the menu, then Elevation by touch (no stick key: a FAULT silences
    them)."""
    rig.go("Seat Functions")
    if rig.watch(3.0, until=hg.screen_is("SeatScreen")) is None:
        raise NotRun("bench", "could not open the Seat screen")
    rig.tap(*m["elevation"])
    time.sleep(0.3)


def on_seat(rig: Rig) -> None:
    s = rig.state()
    if s.get("screen") != "SeatScreen" or s.get("menu_open"):
        raise NotRun("bench", f"seat press: not on the Seat screen ({s.get('screen')}, "
                     f"menu {s.get('menu_open')})")


def press(rig: Rig, at: tuple[int, int], label: str) -> float:
    on_seat(rig)
    t = rig.mark(label)
    rig.tap(*at)
    return t


# ---------------------------------------------------------------- the Actuators page


def open_actuators(rig: Rig) -> tuple[int, int]:
    with navigating(rig):
        return _open_actuators(rig)


def _open_actuators(rig: Rig) -> tuple[int, int]:
    """Bench, PIN 1234 by the stick's keys (as B4b), the actuators page, cursor on Elevation;
    returns where Elevation's "+" is (from the row FOCUS places)."""
    rig.go("Bench")
    if rig.watch(3.0, until=hg.screen_is("BenchGateScreen")) is None:
        raise NotRun("bench", "could not open the PIN gate")
    for key, times in (("UP", 4), ("LEFT", 3)):
        for _ in range(times):
            rig.hmi.nudge(key)
            time.sleep(0.15)
    for key in ("ENTER", "RIGHT", "ENTER", "RIGHT", "ENTER", "DOWN", "LEFT", "LEFT", "ENTER"):
        if key == "ENTER":
            rig.key_enter()
        else:
            rig.hmi.nudge(key)
        time.sleep(0.15)
    if rig.watch(3.0, until=hg.screen_is("SettingsScreen")) is None:
        raise NotRun("bench", "PIN 1234 did not open the actuators page")
    for _ in range(ELEVATION):
        rig.hmi.nudge("DOWN")
        time.sleep(0.2)
    row = _focus(rig, "the Elevation row")
    if row[2] != 700:
        raise NotRun("bench", f"the actuators page's focused object is not a row ({row})")
    plus = (row[0] + ROW_PLUS_DX, row[1] + row[3] // 2)
    rig.st.record("actuators_plus", plus)
    return plus


def key_right(rig: Rig, label: str) -> float:
    t = rig.mark(label)
    rig.hmi.nudge("RIGHT")
    return t


# ---------------------------------------------------------------- Skunk Works


def skunk_seat_up(rig: Rig) -> tuple[int, int]:
    with navigating(rig):
        return _skunk_seat_up(rig)


def _skunk_seat_up(rig: Rig) -> tuple[int, int]:
    actions = skunk_actions(rig.tree)
    if SEAT_UP_ACTION not in actions:
        raise NotRun("bench", f"no {SEAT_UP_ACTION} in the tree's actions_spec.h")
    rig.go("Skunk Works")
    if rig.watch(3.0, until=hg.screen_is("SkunkWorksScreen")) is None:
        raise NotRun("bench", "could not open Skunk Works")
    f0 = _focus(rig, "the first tile")
    if f0[4] < len(actions):
        raise NotRun("bench", f"Skunk Works' tile grid not as expected (FOCUS {f0})")
    at = tile_centre(actions.index(SEAT_UP_ACTION), len(actions), f0[:4])
    rig.st.record("seat_up_tile", at)
    return at


def stick_fault(rig: Rig) -> None:
    """C2's recipe (C2-3, C2-11): horizontal 3300 mV for 0.5 s, then centred: a latched FAULT."""
    h, v, tw = rig.cal.centre()
    rig.inject(3300, v, tw)
    s = rig.watch(1.0, until=lambda s: (s.get("stick") or {}).get("state") == "FAULT")
    rig.centre()
    if s is None:
        raise NotRun("bench", "the injected rail did not latch a stick FAULT")


def _serial(rig: Rig, seconds: float = SERIAL_S) -> SerialWatch | None:
    return SerialWatch(rig.port, seconds) if rig.port else None


def _stop(watch: SerialWatch | None) -> str | None:
    return watch.stop() if watch is not None else None


# ---------------------------------------------------------------- B5s-1


def s_b1(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    press(rig, m["plus"], "press")
    rig.watch(2.0)
    return {}


def grade_b1(st, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("press")
    before = published(tr.events, t)
    if before is None:
        st.check("a MibStatus with the seat before the press", False, "none")
        return
    target = round(before + EL_STEP_MM, 3)
    check_one(st, tr, "the press", t, t + 2.0, target, within=0.5)
    shown = hg.first_state(tr.states, lambda s: seat_raw(s) == round(target * 10), t)
    if shown is None:
        st.check("seat[2] shows the target within 1.0 s", False, "never")
    else:
        st.check("seat[2] shows the target within 1.0 s", shown[0] - t <= 1.0,
                 f"{shown[0] - t:.2f} s")


# ---------------------------------------------------------------- B5s-2, 3


HOSTILE = ("nan", "inf", "-inf", "1e30", "-1e30")


def _hostile(rig: Rig, m: dict, value: str, tag: str) -> None:
    rig.mark(f"val{tag}")
    seatval(rig, ELEVATION, value)
    rig.watch(1.0)
    press(rig, m["plus"], f"plus{tag}")
    rig.watch(2.0)
    press(rig, m["p15"], f"preset{tag}")
    rig.watch(2.0)
    rig.mark(f"off{tag}")
    seatval(rig, ELEVATION, "off")
    rig.watch(1.0)
    press(rig, m["plus"], f"again{tag}")
    rig.watch(2.0)


def s_b2(rig: Rig) -> dict:
    seat_begin(rig)
    watch = _serial(rig)
    m = seat_map(rig)
    _hostile(rig, m, "nan", "")
    return {"serial_text": _stop(watch), "values": [""]}


def s_b3(rig: Rig) -> dict:
    seat_begin(rig)
    watch = _serial(rig)
    m = seat_map(rig)
    tags = []
    for value in HOSTILE[1:]:
        tag = "-" + value.replace("-", "m")
        _hostile(rig, m, value, tag)
        tags.append(tag)
    return {"serial_text": _stop(watch), "values": tags}


def grade_hostile(st, tr: hg.Trace, tag: str, what: str) -> None:
    v, plus, preset = tr.mark(f"val{tag}"), tr.mark(f"plus{tag}"), tr.mark(f"preset{tag}")
    off, again = tr.mark(f"off{tag}"), tr.mark(f"again{tag}")
    gone = hg.first_state(tr.states, lambda s: seat_raw(s) is None and "seat" in s, v)
    st.check(f"{what}: seat[2] null within 1.0 s", gone is not None and gone[0] - v <= 1.0,
             f"{(gone[0] - v):.2f} s" if gone else "never")
    check_refused(st, tr, f"{what} '+'", plus, plus + 2.0, "POSITION_UNKNOWN",
                  "REFUSED_SEAT_UNKNOWN")
    check_refused(st, tr, f"{what} preset 15", preset, preset + 2.0, "POSITION_UNKNOWN",
                  "REFUSED_SEAT_UNKNOWN")
    back = hg.first_state(tr.states, lambda s: seat_raw(s) is not None, off)
    st.check(f"{what}: known within 1.0 s of off", back is not None and back[0] - off <= 1.0,
             f"{(back[0] - off):.2f} s" if back else "never")
    check_one(st, tr, f"{what} '+' after off", again, again + 2.0)


def grade_b2(st, tr: hg.Trace, p: dict) -> None:
    grade_hostile(st, tr, "", "NaN")
    check_no_boot(st, p, "NaN")


def grade_b3(st, tr: hg.Trace, p: dict) -> None:
    for tag in p.get("values", []):
        grade_hostile(st, tr, tag, tag[1:])
    check_no_boot(st, p, "inf, huge")


# ---------------------------------------------------------------- B5s-4


def s_b4(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    rig.mark("band")
    seatval(rig, ELEVATION, "255.0")
    rig.watch(1.0)
    press(rig, m["plus"], "plus")
    rig.watch(2.0)
    press(rig, m["minus"], "minus")
    rig.watch(2.0)
    rig.mark("past")
    seatval(rig, ELEVATION, "255.1")
    rig.watch(1.2)
    return {}


def grade_b4(st, tr: hg.Trace, p: dict) -> None:
    band, plus, minus, past = (tr.mark(x) for x in ("band", "plus", "minus", "past"))
    s = last_state(tr, plus)
    st.check("255.0: known (seat[2] = 2550)", seat_raw(s) == 2550, str(s.get("seat")))
    check_refused(st, tr, "255.0 '+'", plus, minus, "AT_LIMIT", None)
    banners = {s.get("banner") for t, s in tr.states if plus <= t < minus}
    st.check("255.0 '+': no banner", banners <= {"NONE"}, str(sorted(map(str, banners))))
    check_one(st, tr, "255.0 '-'", minus, past, 250.0)
    gone = hg.first_state(tr.states, lambda s: "seat" in s and seat_raw(s) is None, past)
    st.check("255.1 (raw 2551): seat[2] null", gone is not None, str(last_state(tr, 1e18)
                                                                    .get("seat")))


# ---------------------------------------------------------------- B5s-5


def s_b5(rig: Rig) -> dict:
    seat_begin(rig)
    plus = open_actuators(rig)
    t = rig.mark("pause")
    rig.sim_cmd("p")
    rig.watch(max(0.0, t + 3.0 - time.monotonic()))
    rig.mark("press")
    rig.tap(*plus)
    rig.hmi.nudge("RIGHT")
    rig.watch(max(0.0, t + 5.0 - time.monotonic()))
    rig.mark("resume")
    rig.sim_cmd("r")
    rig.watch(2.5)
    rig.st.not_verified.append("ErrorBanner6 up while the link is down (no STATE field)")
    return {}


def grade_b5(st, tr: hg.Trace, p: dict) -> None:
    pz, r = tr.mark("pause"), tr.mark("resume")
    gone = hg.first_state(tr.states, all_null, pz)
    dt = gone[0] - pz if gone else None
    st.check("every seat value null 1.5..2.9 s after p", dt is not None and 1.5 <= dt <= 2.9,
             f"{dt:.2f} s" if dt is not None else "never")
    check_none(st, tr, "p .. r + 2 s", pz, r + 2.0)
    back = hg.first_state(tr.states, all_known, r)
    st.check("values known within 1.0 s of r", back is not None and back[0] - r <= 1.0,
             f"{back[0] - r:.2f} s" if back else "never")


# ---------------------------------------------------------------- B5s-6


def s_b6(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    sim_seat(rig, "seatslow on", r"seat slow: on")
    t = press(rig, m["p0"], "press")
    rig.watch(max(0.0, t + 2.0 - time.monotonic()))
    rig.mark("pause")
    rig.sim_cmd("p")
    rig.watch(max(0.0, t + 7.0 - time.monotonic()))
    rig.mark("resume")
    rig.sim_cmd("r")
    rig.watch(5.1)
    return {}


def grade_b6(st, tr: hg.Trace, p: dict) -> None:
    t, pz, r = tr.mark("press"), tr.mark("pause"), tr.mark("resume")
    check_one(st, tr, "preset 0", t, r + 5.0, 0.0)
    home = hg.first_state(tr.states, lambda s: s.get("screen") == "LockedScreen"
                          and s.get("banner") == "REFUSED_SEAT", pz)
    st.check("Seat screen -> Locked with the link banner within 2.9 s of p",
             home is not None and home[0] - pz <= 2.9,
             f"{home[0] - pz:.2f} s" if home else "never")
    moves = hg.events_in(tr.events, pz, r, "seat_move")
    st.check("the sim's seat_move continues through the pause (residual 2)", len(moves) >= 2,
             f"{len(moves)} seat_move in the pause")


# ---------------------------------------------------------------- B5s-7


def s_b7(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    sim_seat(rig, "seatslow on", r"seat slow: on")
    t = press(rig, m["p0"], "press")
    rig.watch(max(0.0, t + 1.0 - time.monotonic()))
    watch = _serial(rig, 120.0)  # through the reboot: its POST RESULT line
    rig.mark("restart")
    with navigating(rig):
        rig.restart_hmi()
        rig.wait_back()
    rig.mark("back")
    rig.read_cal()
    rig.centre()
    rig.watch(15.0, until=hg.field_is("post", "PASS"))
    rig.watch(10.0)
    m2 = seat_map(rig)
    rig.watch(1.0)
    press(rig, m2["plus"], "plus")
    rig.watch(2.0)
    return {"serial_text": _stop(watch)}


def grade_b7(st, tr: hg.Trace, p: dict) -> None:
    rs, plus = tr.mark("restart"), tr.mark("plus")
    check_none(st, tr, "restart .. '+'", rs, plus)
    # The rebooted HMI's first MibStatus is the new sim's: the rig retires the old sim at the
    # restart ("close") and starts a new one once the HMI answers again ("open"; both log to one
    # file), polling STATE once in between (Rig.wait_back). Every poll after the old sim closed
    # and before the new sim's first publish shows every axis unknown, and there is one.
    closed = hg.first_event(tr.events, lambda e: e.get("ev") == "close", rs)
    opened = hg.first_event(tr.events, lambda e: e.get("ev") == "open", rs)
    first_pub = next((e["mono"] for e in hg.publishes(tr.events, opened["mono"])), None)         if opened else None
    early = [s.get("seat") for t, s in tr.states
             if closed and first_pub and closed["mono"] < t < first_pub]
    st.check("seat values null until the first MibStatus after the reboot",
             bool(early) and all(isinstance(v, list) and all(x is None for x in v)
                                 for v in early),
             f"{len(early)} poll(s) before it: {early[:2]}")
    text = p.get("serial_text")
    if text is None:
        st.not_verified.append("serial POST RESULT PASS (no serial port given)")
    else:
        st.check("serial POST RESULT PASS", POST_PASS_RE.search(text) is not None, "")
    before = published(tr.events, plus)
    check_one(st, tr, "'+' after the reboot", plus, plus + 2.0,
              None if before is None else round(before + EL_STEP_MM, 3))


# ---------------------------------------------------------------- B5s-8


def s_b8(rig: Rig) -> dict:
    seat_begin(rig)
    open_actuators(rig)
    for state in ("e", "z", "ok"):
        rig.sim_cmd(state)
        rig.watch(1.5)
        key_right(rig, f"key-{state}")
        rig.watch(2.0)
    tile = skunk_seat_up(rig)
    for state in ("e", "z", "ok"):
        rig.sim_cmd(state)
        rig.watch(1.5)
        rig.mark(f"tile-{state}")
        rig.tap(*tile)
        rig.watch(2.0)
    rig.st.not_verified.append("Skunk Works 'Seat up' greyed in e, z (its look)")
    return {}


def grade_b8(st, tr: hg.Trace, p: dict) -> None:
    for kind in ("key", "tile"):
        for state in ("e", "z"):
            t = tr.mark(f"{kind}-{state}")
            end = t + 2.0
            if kind == "key":
                check_refused(st, tr, f"Actuators in {state}", t, end, "MCB_NOT_IDLE", None)
            else:
                check_none(st, tr, f"Skunk 'Seat up' in {state}", t, end)
                a, b = last_state(tr, t), last_state(tr, end)
                st.check(f"Skunk 'Seat up' in {state}: greyed, the gate not reached",
                         count(a, "seat_refused") == count(b, "seat_refused")
                         and count(a, "seat_sent") == count(b, "seat_sent"),
                         f"refused {count(a, 'seat_refused')}->{count(b, 'seat_refused')}")
        t = tr.mark(f"{kind}-ok")
        before = published(tr.events, t)
        check_one(st, tr, f"{'Actuators' if kind == 'key' else 'Skunk'} in ok", t, t + 2.0,
                  None if before is None else round(before + EL_STEP_MM, 3))


# ---------------------------------------------------------------- B5s-9


def _post_cycle(rig: Rig, at: tuple[int, int], tag: str, seat_screen: bool = False) -> None:
    """Bumped stick, POST RERUN, a press during PENDING; centred, PASS, the same press."""
    import scenario_c3 as c3
    rig.inject(*c3.bumped(rig.cal))
    rig.bench_verb(rig.hmi.post_rerun, "POST RERUN")
    if rig.watch(2.0, until=hg.field_is("post", "PENDING")) is None:
        raise NotRun("bench", "POST RERUN did not give PENDING")
    if seat_screen:
        on_seat(rig)
    rig.mark(f"{tag}-1")
    rig.tap(*at)
    rig.watch(2.0)
    rig.mark(f"{tag}-centred")
    rig.centre()
    rig.watch(3.0, until=hg.field_is("post", "PASS"))
    if seat_screen:
        on_seat(rig)
    rig.mark(f"{tag}-2")
    rig.tap(*at)
    rig.watch(2.1)


def s_b9(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    _post_cycle(rig, m["minus"], "seat", seat_screen=True)
    with navigating(rig):
        rig.home()
    plus = open_actuators(rig)
    _post_cycle(rig, plus, "act")
    with navigating(rig):
        rig.home()
    tile = skunk_seat_up(rig)
    _post_cycle(rig, tile, "skunk")
    return {}


def grade_b9(st, tr: hg.Trace, p: dict) -> None:
    for tag, what in (("seat", "Seat screen (B5''-19)"), ("act", "Actuators"),
                      ("skunk", "Skunk Works")):
        t1, c, t2 = tr.mark(f"{tag}-1"), tr.mark(f"{tag}-centred"), tr.mark(f"{tag}-2")
        check_refused(st, tr, f"{what} before PASS", t1, min(t1 + 2.0, c), "POST_NOT_PASSED",
                      "REFUSED_POST")
        check_one(st, tr, f"{what} after PASS", t2, t2 + 2.0)


# ---------------------------------------------------------------- B5s-10


def s_b10(rig: Rig) -> dict:
    """The stick FAULT comes with the Seat screen's Elevation page already open: with a FAULT
    the menu's Seat Functions row is refused (REQ-UI-32, Q10, B5s-16), so the screen cannot be
    reached by the menu after it; the press is by touch (the keys are silent)."""
    seat_begin(rig)
    m = seat_map(rig)
    rig.mark("fault")
    stick_fault(rig)
    press(rig, m["plus"], "plus-1")
    rig.watch(2.0)
    rig.mark("restart")
    rig.inj.pause()
    with navigating(rig):
        rig.restart_hmi()
        rig.wait_back()
    rig.read_cal()
    rig.centre()
    rig.watch(15.0, until=hg.field_is("post", "PASS"))
    rig.watch(3.0, until=lambda s: (s.get("stick") or {}).get("state") == "OK")
    rig.watch(3.0, until=all_known)
    open_elevation_by_touch(rig, m)
    press(rig, m["plus"], "plus-2")
    rig.watch(2.0)
    rig.st.not_verified.append("the banner's words 'Seat not ready' + the C2 text")
    return {}


def grade_b10(st, tr: hg.Trace, p: dict) -> None:
    f, p1, p2 = tr.mark("fault"), tr.mark("plus-1"), tr.mark("plus-2")
    fault = hg.first_state(tr.states, lambda s: (s.get("stick") or {}).get("state") == "FAULT", f)
    st.check("STATE stick FAULT", fault is not None, "")
    check_refused(st, tr, "'+' with the stick FAULT", p1, p1 + 2.0, "STICK_FAULT",
                  "REFUSED_STICK")
    check_one(st, tr, "'+' after the reboot", p2, p2 + 2.0)


# ---------------------------------------------------------------- B5s-11, 12


def s_b11(rig: Rig) -> dict:
    seat_begin(rig)
    open_actuators(rig)
    rig.mark("key")
    rig.hmi.command("KEY RIGHT")
    try:
        rig.watch(5.0)
    finally:
        rig.hmi.command("KEY NONE")
    rig.mark("release")
    rig.watch(1.5)
    return {}


def grade_b11(st, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("key")
    check_one(st, tr, "KEY RIGHT held 5 s", t, t + 6.0)


def s_b12(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    on_seat(rig)
    rig.mark("press")
    rig.hmi.command(f"PRESS {m['plus'][0]} {m['plus'][1]}")
    try:
        rig.watch(5.0)
    finally:
        rig.mark("release")
        rig.hmi.command("RELEASE")
    rig.watch(1.5)
    return {}


def grade_b12(st, tr: hg.Trace, p: dict) -> None:
    t, r = tr.mark("press"), tr.mark("release")
    check_none(st, tr, "during the 5 s hold", t, r)
    after = sent(tr.events, r, r + 0.5)
    later = sent(tr.events, r + 0.5, r + 1.5)
    st.check("at most 1 SeatCommand within 0.5 s of the release, none later",
             len(after) <= 1 and not later, f"{len(after)} then {len(later)}")


# ---------------------------------------------------------------- B5s-13, 14


def s_b13(rig: Rig) -> dict:
    seat_begin(rig)
    watch = _serial(rig, 60.0)
    m = seat_map(rig)
    rig.mark("fail")
    rig.bench_verb(lambda: rig.hmi.seat_fail(1), "SEATFAIL 1")
    press(rig, m["plus"], "plus-1")
    rig.watch(5.0)
    press(rig, m["plus"], "plus-2")
    rig.watch(2.0)
    return {"serial_text": _stop(watch)}


def grade_b13(st, tr: hg.Trace, p: dict) -> None:
    p1, p2 = tr.mark("plus-1"), tr.mark("plus-2")
    check_none(st, tr, "first press, and 5 s after it (no re-send)", p1, p2)
    a, b = last_state(tr, p1), first_state_after(tr, p1 + 1.0)
    st.check("seat_publish_failures +1",
             count(b, "seat_publish_failures") == count(a, "seat_publish_failures") + 1,
             f"{count(a, 'seat_publish_failures')} -> {count(b, 'seat_publish_failures')}")
    banner = hg.first_state(tr.states, hg.field_is("banner", "REFUSED_SEAT_NOT_SENT"), p1, p2)
    st.check("banner REFUSED_SEAT_NOT_SENT", banner is not None, "")
    text = p.get("serial_text")
    if text is None:
        st.not_verified.append("serial 'SeatCommand ELEVATION not published' (no port)")
    else:
        m = FAULT_RE.search(text)
        st.check("serial: SeatCommand ELEVATION not published (n failed since start)",
                 m is not None and int(m.group(1)) == count(b, "seat_publish_failures"),
                 m.group(0) if m else "no line")
    check_one(st, tr, "second press", p2, p2 + 2.0)


def s_b14(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    sim_seat(rig, "seatdrop 1", r"dropping the next 1 SeatCommand")
    press(rig, m["plus"], "plus")
    rig.watch(5.0)
    return {}


def grade_b14(st, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("plus")
    gone, rest = dropped(tr.events, t, t + 5.0), sent(tr.events, t, t + 5.0)
    st.check("the sim logs 1 dropped SeatCommand and no other in 5 s",
             len(gone) == 1 and not rest, f"dropped {len(gone)}, applied {len(rest)}")
    a, b = last_state(tr, t), last_state(tr, t + 5.0)
    st.check("seat_publish_failures unchanged",
             count(a, "seat_publish_failures") == count(b, "seat_publish_failures") >= 0, "")
    st.check("the value unchanged", seat_raw(a) is not None and seat_raw(a) == seat_raw(b),
             f"{seat_raw(a)} -> {seat_raw(b)}")


# ---------------------------------------------------------------- B5s-15


def s_b15(rig: Rig) -> dict:
    seat_begin(rig)
    m = seat_map(rig)
    on_seat(rig)
    rig.mark("taps")
    x, y = m["plus"]
    for _ in range(10):
        rig.hmi.command(f"PRESS {x} {y}")
        time.sleep(0.06)  # two pointer reads
        rig.hmi.command("RELEASE")
        time.sleep(0.06)
    rig.mark("taps-end")
    rig.watch(2.0)
    return {}


def grade_b15(st, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("taps")
    cmds = sent(tr.events, t, tr.mark("taps-end") + 2.0)
    st.check("the taps sent at least one SeatCommand", bool(cmds), "")
    bad = []
    for e in cmds:
        before = published(tr.events, e["mono"])
        if before is None or e.get("target", 1e9) > before + EL_STEP_MM + 1e-3:
            bad.append((round(e["mono"] - t, 2), e.get("target"), before))
    st.check("every target <= the latest published elevation + 5.0 mm", not bad, str(bad[:3]))


# ---------------------------------------------------------------- B5s-16, 17


def close_menu(rig: Rig) -> None:
    """The burger key again when the menu is still open (a refused row leaves it up)."""
    if rig.state().get("menu_open"):
        rig.burger()
        rig.watch(2.0, until=lambda s: s.get("menu_open") is False)


def s_b16(rig: Rig) -> dict:
    seat_begin(rig)
    rig.mark("e")
    rig.sim_cmd("e")
    rig.watch(1.5)
    rig.mark("row-e")
    rig.go("Seat Functions")
    rig.watch(2.0)
    close_menu(rig)
    rig.sim_cmd("ok")
    rig.watch(1.5)
    rig.mark("fault")
    stick_fault(rig)
    rig.mark("row-fault")
    rig.go("Seat Functions")
    rig.watch(2.0)
    close_menu(rig)
    return {}


def grade_b16(st, tr: hg.Trace, p: dict) -> None:
    for tag, banner in (("row-e", "REFUSED_SEAT"), ("row-fault", "REFUSED_STICK")):
        t = tr.mark(tag)
        check_none(st, tr, f"Seat Functions ({tag[4:]})", t, t + 2.0)
        b = hg.first_state(tr.states, hg.field_is("banner", banner), t, t + 2.0)
        st.check(f"Seat Functions ({tag[4:]}): banner {banner}", b is not None,
                 f"banner {first_state_after(tr, t + 1.0).get('banner')}")
        end = last_state(tr, t + 2.0)
        st.check(f"Seat Functions ({tag[4:]}): refused, the menu stays",
                 end.get("screen") != "SeatScreen" and end.get("menu_open") is True,
                 f"screen {end.get('screen')}, menu {end.get('menu_open')}")


def s_b17(rig: Rig) -> dict:
    seat_begin(rig)
    rig.go("Seat Functions")
    rig.watch(3.0, until=hg.screen_is("SeatScreen"))
    rig.mark("val")
    seatval(rig, ELEVATION, "100")
    rig.watch(3.0)
    return {}


def grade_b17(st, tr: hg.Trace, p: dict) -> None:
    t = tr.mark("val")
    shown = hg.first_state(tr.states, lambda s: seat_raw(s) == 1000, t)
    st.check("seat[2] = 100.0 within 1.0 s", shown is not None and shown[0] - t <= 1.0,
             f"{shown[0] - t:.2f} s" if shown else "never")
    check_none(st, tr, "the MCB moved the seat itself", t, t + 3.0)


STEPS = {
    "B5s-1": (s_b1, grade_b1, "baseline: one press, one step"),
    "B5s-2": (s_b2, grade_b2, "seat value NaN"),
    "B5s-3": (s_b3, grade_b3, "seat value inf, -inf, 1e30, -1e30"),
    "B5s-4": (s_b4, grade_b4, "the tolerance band, AT_LIMIT"),
    "B5s-5": (s_b5, grade_b5, "link loss (Actuators)"),
    "B5s-6": (s_b6, grade_b6, "link loss mid-move (seatslow)"),
    "B5s-7": (s_b7, grade_b7, "HMI reset mid-move (seatslow)"),
    "B5s-8": (s_b8, grade_b8, "MCB not IDLE (e, z)"),
    "B5s-9": (s_b9, grade_b9, "POST not passed: Seat, Actuators, Skunk Works"),
    "B5s-10": (s_b10, grade_b10, "stick FAULT"),
    "B5s-11": (s_b11, grade_b11, "stuck key (Actuators)"),
    "B5s-12": (s_b12, grade_b12, "stuck touch (Seat)"),
    "B5s-13": (s_b13, grade_b13, "publish failure (SEATFAIL)"),
    "B5s-14": (s_b14, grade_b14, "lost SeatCommand (seatdrop)"),
    "B5s-15": (s_b15, grade_b15, "rapid presses"),
    "B5s-16": (s_b16, grade_b16, "the menu's Seat Functions row"),
    "B5s-17": (s_b17, grade_b17, "the MCB moves the seat on its own"),
}
# The serial log, when the board's port is given, adds checks; without it they are recorded as
# not verified (B5s-2, 3, 7, 13), so none of these needs it to run.
NEEDS_SERIAL: set[str] = set()
CLEANUP = {name: seat_cleanup for name in STEPS}
