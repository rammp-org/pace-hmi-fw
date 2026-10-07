#!/usr/bin/env python3
"""What the simulated MCB decides, with no network: the logic of rtps_mcb_sim.py.

rtps_mcb_sim.py owns the sockets and the stdin loop; everything it decides lives
here as pure functions and small classes, so it can be tested on inline inputs:

- decide_drive_command(): what one DriveCommand does to the MIB state, with the
  bench's fault modes (refuse ENABLE `x`, refuse DISABLE `s`, ignore the next N
  DISABLEs `ign N`, drop the next N DISABLEs `drop N`);
- HmiPresence: when the HMI has gone away (no XYTwist for HMI_GONE_S) and when it
  is back, which is how an HMI reset looks from the MIB's side;
- state_on_hmi_gone(): what the MIB does to its state when the HMI goes away
  (`ongone keep`: stays ENABLED across an HMI reset; `ongone idle`: drops to IDLE);
- XyTwistLog: the per-second XYTwist summary plus every non-zero sample;
- EventLog: the timestamped JSONL log (rtps_mcb_sim.py --event-log PATH);
- parse_mode_command(): the stdin commands above, parsed.

    python scripts/mcb_sim_logic.py selftest      # cases SIM-001.., Unity's output format
    python scripts/rtps_mcb_sim.py selftest       # the same cases

Stdlib only plus rammp_rtps.py (the enum values come from the spec headers, so
external/rammp-rtps must be checked out). No tkinter, no sockets.
"""

from __future__ import annotations

import datetime
import inspect
import json
import math
import os
import sys
import tempfile
import threading
import time
import traceback
from dataclasses import dataclass
from typing import Callable

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)

#: No XYTwist for this long and the HMI counts as gone. The HMI publishes one every
#: ADC cycle (~33 ms, how-the-firmware-works.md §11), so a second of silence is a
#: reset, a crash or a lost link, never a slow cycle.
HMI_GONE_S = 1.0
#: XYTwist summary window.
XYTWIST_WINDOW_S = 1.0
#: Upper bound for `ign N` / `drop N`, so a typo cannot arm a count nobody can see the end of.
MAX_FAULT_COUNT = 100000

# What a DriveCommand did. "applied": the state follows the request (IDLE or ENABLED).
# "kept": the state is ERROR or INITIALIZING, which a drive request neither clears nor
# finishes. "refused": `x` (ENABLE) or `s` (DISABLE) is set. "ignored": an armed `ign`
# count: received, recorded, answered, state kept. "dropped": an armed `drop` count:
# as if it never arrived (no state, no request, no profile, no reply).
ACTION_APPLIED = "applied"
ACTION_KEPT = "kept"
ACTION_REFUSED = "refused"
ACTION_IGNORED = "ignored"
ACTION_DROPPED = "dropped"

ON_HMI_GONE_KEEP = "keep"
ON_HMI_GONE_IDLE = "idle"
ON_HMI_GONE = (ON_HMI_GONE_KEEP, ON_HMI_GONE_IDLE)


def state_name(state: int) -> str:
    return spec.MIB_SYSTEM_STATE_NAMES.get(state, str(state))


def request_name(request: int) -> str:
    return spec.DRIVE_REQUEST_NAMES.get(request, str(request))


def profile_name(profile: int) -> str:
    return spec.DRIVE_PROFILE_NAMES.get(profile, str(profile))


# ---------------------------------------------------------------- DriveCommand


@dataclass(frozen=True)
class DriveDecision:
    """What one DriveCommand does. The counts are the ones left after it."""
    state: int             # the MIB state after the command
    action: str            # ACTION_*
    ignore_left: int
    drop_left: int

    @property
    def received(self) -> bool:
        """False only for a dropped command: nothing about it reached the MCB."""
        return self.action != ACTION_DROPPED

    @property
    def reply(self) -> bool:
        """Answer at once with a MibStatus (the HMI waits on it); not for a dropped one."""
        return self.received


def decide_drive_command(state: int, request: int, *, refuse_drive: bool = False,
                         refuse_stop: bool = False, ignore_disable: int = 0,
                         drop_disable: int = 0) -> DriveDecision:
    """What a DriveCommand does to the MIB state.

    Only a DISABLE consumes an `ign` or `drop` count, and every DISABLE does, whatever
    the state (a DISABLE that arrives while IDLE uses one up too). Precedence for a
    DISABLE: drop (lost on the wire, so nothing else sees it), then ignore, then `s`.
    Without counts this is exactly the sim's behaviour before the counts existed:
    refused requests keep the state; ERROR and INITIALIZING are not left by a drive
    request; otherwise ENABLE gives ENABLED and anything else IDLE.
    """
    if request == spec.DRIVE_REQUEST_DISABLE:
        if drop_disable > 0:
            return DriveDecision(state, ACTION_DROPPED, ignore_disable, drop_disable - 1)
        if ignore_disable > 0:
            return DriveDecision(state, ACTION_IGNORED, ignore_disable - 1, drop_disable)
    if request == spec.DRIVE_REQUEST_ENABLE and refuse_drive:
        return DriveDecision(state, ACTION_REFUSED, ignore_disable, drop_disable)
    if request == spec.DRIVE_REQUEST_DISABLE and refuse_stop:
        return DriveDecision(state, ACTION_REFUSED, ignore_disable, drop_disable)
    if state not in (spec.MIB_SYSTEM_STATE_IDLE, spec.MIB_SYSTEM_STATE_ENABLED):
        return DriveDecision(state, ACTION_KEPT, ignore_disable, drop_disable)
    new_state = (spec.MIB_SYSTEM_STATE_ENABLED if request == spec.DRIVE_REQUEST_ENABLE
                 else spec.MIB_SYSTEM_STATE_IDLE)
    return DriveDecision(new_state, ACTION_APPLIED, ignore_disable, drop_disable)


def format_drive_line(request: int, profile: int, decision: DriveDecision,
                      refuse_drive: bool) -> str:
    """The sim's `[drive]` console line. For the actions that existed before the
    counts (applied, kept, refused) it is the old line, character for character:
    tools/bench/scenario_drive.py matches `[drive] ENABLE` and `ENABLE.*refusing`."""
    head = (f"[drive] {spec.DRIVE_REQUEST_NAMES.get(request, '?')} "
            f"profile={spec.DRIVE_PROFILE_NAMES.get(profile, '?')}")
    if decision.action == ACTION_DROPPED:
        return f"{head} DROPPED ({decision.drop_left} more to drop): never reached this MCB"
    line = f"{head} -> {spec.MIB_SYSTEM_STATE_NAMES.get(decision.state, '?')}"
    if request == spec.DRIVE_REQUEST_ENABLE and refuse_drive:
        line += " (refusing)"
    if decision.action == ACTION_IGNORED:
        line += f" (IGNORED, {decision.ignore_left} more to ignore)"
    return line


# ---------------------------------------------------------------- the HMI going away


class HmiPresence:
    """Whether the HMI is there, judged from its XYTwist stream.

    unseen -> present on the first sample; present -> gone after `gone_after_s`
    with no sample (tick); gone -> present on the next sample, which counts one
    return. A reset, a crash and a lost link all look the same from here; the
    gap length in the events tells them apart for whoever reads the log.
    """

    def __init__(self, gone_after_s: float = HMI_GONE_S):
        self.gone_after_s = gone_after_s
        self.last: float | None = None
        self.gone = False
        self.returns = 0

    def sample(self, now: float) -> dict | None:
        """One XYTwist arrived at `now`; returns an event when that changes anything."""
        event = None
        if self.last is None:
            event = {"ev": "hmi_seen"}
        elif self.gone:
            self.returns += 1
            event = {"ev": "hmi_back", "gap_s": round(now - self.last, 3),
                     "returns": self.returns}
        self.last = now
        self.gone = False
        return event

    def tick(self, now: float) -> dict | None:
        """Called periodically; returns the hmi_gone event once per silence."""
        if self.last is None or self.gone or now - self.last <= self.gone_after_s:
            return None
        self.gone = True
        return {"ev": "hmi_gone", "silent_s": round(now - self.last, 3)}


def state_on_hmi_gone(state: int, policy: str) -> int:
    """`keep` leaves the state alone: a MIB that stays ENABLED across an HMI reset
    (the sim's behaviour before the policy existed, and the default). `idle` is a
    MIB that times the HMI out: ENABLED drops to IDLE, anything else is kept."""
    if policy == ON_HMI_GONE_IDLE and state == spec.MIB_SYSTEM_STATE_ENABLED:
        return spec.MIB_SYSTEM_STATE_IDLE
    return state


# ---------------------------------------------------------------- XYTwist log


class XyTwistLog:
    """Per-window summaries of the XYTwist stream, plus every sample worth seeing.

    `add` returns the records to log for one sample: the previous window's summary
    when this sample starts a new window, then the sample itself when any axis is
    non-zero or the button bits changed. A window with no samples gives no summary,
    so a silence shows as a missing summary (and an hmi_gone event).
    """

    def __init__(self, window_s: float = XYTWIST_WINDOW_S):
        self.window_s = window_s
        self.start: float | None = None
        self.last_t: float | None = None
        self.last_buttons: int | None = None
        self._reset()

    def _reset(self) -> None:
        self.count = 0
        self.nonzero = 0
        self.max_abs = [0.0, 0.0, 0.0]
        self.buttons_seen: set[int] = set()

    def add(self, now: float, sample: tuple) -> list[dict]:
        out = []
        if self.start is not None and now - self.start >= self.window_s:
            summary = self.flush(now)
            if summary is not None:
                out.append(summary)
        if self.start is None:
            self.start = now
        x, y, twist, buttons = sample[0], sample[1], sample[2], int(sample[3])
        axes = (x, y, twist)
        nonzero = any(v != 0 for v in axes)  # NaN != 0: a NaN is logged too
        self.count += 1
        self.nonzero += nonzero
        for i, v in enumerate(axes):
            if math.isnan(v) or abs(v) > self.max_abs[i]:
                self.max_abs[i] = abs(v)
        self.buttons_seen.add(buttons)
        self.last_t = now
        if nonzero or buttons != self.last_buttons:
            out.append({"ev": "xytwist", "x": x, "y": y, "twist": twist, "buttons": buttons})
        self.last_buttons = buttons
        return out

    def flush(self, now: float) -> dict | None:
        """The current window's summary (None if it is empty); starts a new window.
        `span_s` runs from the window's first sample to its last, so a summary
        flushed by a silence does not count the silence."""
        if self.start is None or self.count == 0:
            self.start = None
            return None
        record = {"ev": "xytwist_summary", "span_s": round(self.last_t - self.start, 3),
                  "count": self.count, "nonzero": self.nonzero,
                  "max_abs_x": self.max_abs[0], "max_abs_y": self.max_abs[1],
                  "max_abs_twist": self.max_abs[2],
                  "buttons_seen": sorted(self.buttons_seen)}
        self.start = None
        self._reset()
        return record


# ---------------------------------------------------------------- event log


class EventLog:
    """Timestamped JSON lines: one object per line, flushed as written.

    Every record has `t` (wall clock, ISO 8601 with ms), `mono` (time.monotonic(),
    s) and `ev`. Opened for append, so one file can hold several runs; each run
    starts with an `open` record. With path None every call is a no-op. Thread-safe:
    the network thread and the stdin loop both write.
    """

    def __init__(self, path: str | None,
                 clock: Callable[[], float] = time.monotonic,
                 wall: Callable[[], datetime.datetime] = datetime.datetime.now):
        self.path = path
        self._clock = clock
        self._wall = wall
        self._lock = threading.Lock()
        self._file = None
        if path:
            directory = os.path.dirname(os.path.abspath(path))
            os.makedirs(directory, exist_ok=True)
            self._file = open(path, "a", encoding="utf-8", newline="\n")

    def write(self, ev: str, **fields: object) -> dict | None:
        if self._file is None:
            return None
        record = {"t": self._wall().isoformat(timespec="milliseconds"),
                  "mono": round(self._clock(), 3), "ev": ev, **fields}
        line = json.dumps(record, default=str)
        with self._lock:
            self._file.write(line + "\n")
            self._file.flush()
        return record

    def record(self, event: dict | None) -> None:
        """Write an event dict as produced by HmiPresence / XyTwistLog."""
        if event is not None:
            fields = dict(event)
            self.write(fields.pop("ev"), **fields)

    def close(self) -> None:
        with self._lock:
            if self._file is not None:
                self._file.close()
                self._file = None


def read_events(path: str) -> list[dict]:
    """Every complete record in an event log; a half-written last line is skipped."""
    out = []
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        return out
    for line in text.split("\n")[:-1]:  # the part after the last newline is incomplete
        if line.strip():
            out.append(json.loads(line))
    return out


# ---------------------------------------------------------------- stdin commands

MODE_HELP = """  ign <n>         ignore the next n DISABLEs: received and answered, but the
                  state stays as it is (ENABLED stays ENABLED). 'ign' alone = 0
  drop <n>        drop the next n DISABLEs: as if lost on the wire (no state
                  change, no reply, request and profile not recorded). 'drop' = 0
  ongone keep     when the HMI goes away (no XYTwist for 1 s) keep the
                  state: stays ENABLED across an HMI reset (default)
  ongone idle     when the HMI goes away, ENABLED drops to IDLE
  mark <label>    write a mark into the --event-log (prints MARK <label>)
  modes           show the fault modes"""

MODE_COMMANDS = ("ign", "drop", "ongone", "mark", "modes")


def is_mode_command(text: str) -> bool:
    words = text.split()
    return bool(words) and words[0] in MODE_COMMANDS


def parse_mode_command(text: str) -> tuple[str, object]:
    """('ign'|'drop', n) | ('ongone', policy) | ('mark', label) | ('modes', None).

    Raises ValueError with a one-line reason for a bad argument, or for text that
    is not a mode command (check is_mode_command first).
    """
    words = text.split()
    if not words or words[0] not in MODE_COMMANDS:
        raise ValueError(f"not a mode command: {text!r}")
    head, args = words[0], words[1:]
    if head in ("ign", "drop"):
        if len(args) > 1:
            raise ValueError(f"'{head}' takes one count")
        if not args:
            return head, 0
        if not args[0].isdigit() or int(args[0]) > MAX_FAULT_COUNT:
            raise ValueError(f"'{head}' needs a count 0..{MAX_FAULT_COUNT}, got {args[0]!r}")
        return head, int(args[0])
    if head == "ongone":
        if len(args) != 1 or args[0] not in ON_HMI_GONE:
            raise ValueError("'ongone' takes keep or idle")
        return head, args[0]
    if head == "mark":
        label = text.strip()[len("mark"):].strip()
        if not label:
            raise ValueError("'mark' needs a label")
        return head, label
    if args:
        raise ValueError("'modes' takes no argument")
    return head, None


# ---------------------------------------------------------------- selftest

ENABLED = spec.MIB_SYSTEM_STATE_ENABLED
IDLE = spec.MIB_SYSTEM_STATE_IDLE
ERROR = spec.MIB_SYSTEM_STATE_ERROR
INITIALIZING = spec.MIB_SYSTEM_STATE_INITIALIZING
ENABLE = spec.DRIVE_REQUEST_ENABLE
DISABLE = spec.DRIVE_REQUEST_DISABLE


def expect(what: str, got: object, want: object) -> None:
    if got != want:
        raise AssertionError(f"{what}: got {got!r}, want {want!r}")


def t_plain_requests() -> None:
    for state, request, want in ((IDLE, ENABLE, ENABLED), (ENABLED, DISABLE, IDLE),
                                 (ENABLED, ENABLE, ENABLED), (IDLE, DISABLE, IDLE)):
        d = decide_drive_command(state, request)
        expect(f"{state_name(state)}+{request_name(request)}", (d.state, d.action),
               (want, ACTION_APPLIED))
        expect("reply", d.reply, True)


def t_refusals_unchanged() -> None:
    d = decide_drive_command(IDLE, ENABLE, refuse_drive=True)
    expect("x refuses ENABLE", (d.state, d.action), (IDLE, ACTION_REFUSED))
    d = decide_drive_command(ENABLED, DISABLE, refuse_stop=True)
    expect("s refuses DISABLE", (d.state, d.action), (ENABLED, ACTION_REFUSED))
    d = decide_drive_command(ENABLED, DISABLE, refuse_drive=True)
    expect("x does not touch DISABLE", d.state, IDLE)
    d = decide_drive_command(IDLE, ENABLE, refuse_stop=True)
    expect("s does not touch ENABLE", d.state, ENABLED)


def t_error_and_init_kept() -> None:
    for state in (ERROR, INITIALIZING):
        for request in (ENABLE, DISABLE):
            d = decide_drive_command(state, request)
            expect(f"{state_name(state)}+{request_name(request)}", (d.state, d.action),
                   (state, ACTION_KEPT))


def t_ignore_counts_down() -> None:
    state, left, seen = ENABLED, 2, []
    for _ in range(3):
        d = decide_drive_command(state, DISABLE, ignore_disable=left)
        state, left = d.state, d.ignore_left
        seen.append((d.action, state_name(state), left))
    expect("ign 2 then a third DISABLE", seen,
           [(ACTION_IGNORED, "ENABLED", 1), (ACTION_IGNORED, "ENABLED", 0),
            (ACTION_APPLIED, "IDLE", 0)])
    expect("an ignored DISABLE is answered", decide_drive_command(
        ENABLED, DISABLE, ignore_disable=1).reply, True)


def t_drop_counts_down_and_is_silent() -> None:
    d = decide_drive_command(ENABLED, DISABLE, drop_disable=1)
    expect("dropped", (d.state, d.action, d.drop_left), (ENABLED, ACTION_DROPPED, 0))
    expect("not received, no reply", (d.received, d.reply), (False, False))
    d = decide_drive_command(ENABLED, DISABLE, drop_disable=d.drop_left)
    expect("the next one obeyed", (d.state, d.action), (IDLE, ACTION_APPLIED))


def t_precedence() -> None:
    d = decide_drive_command(ENABLED, DISABLE, ignore_disable=1, drop_disable=1,
                             refuse_stop=True)
    expect("drop first", (d.action, d.ignore_left, d.drop_left), (ACTION_DROPPED, 1, 0))
    d = decide_drive_command(ENABLED, DISABLE, ignore_disable=1, refuse_stop=True)
    expect("then ignore", (d.action, d.ignore_left), (ACTION_IGNORED, 0))
    d = decide_drive_command(ENABLED, DISABLE, refuse_stop=True)
    expect("then s", d.action, ACTION_REFUSED)


def t_counts_only_for_disable() -> None:
    d = decide_drive_command(IDLE, ENABLE, ignore_disable=3, drop_disable=2)
    expect("ENABLE keeps both counts", (d.state, d.ignore_left, d.drop_left), (ENABLED, 3, 2))
    d = decide_drive_command(IDLE, DISABLE, ignore_disable=1)
    expect("a DISABLE while IDLE still uses one up", (d.state, d.ignore_left), (IDLE, 0))
    d = decide_drive_command(ERROR, DISABLE, drop_disable=1)
    expect("and while ERROR", (d.state, d.action, d.drop_left), (ERROR, ACTION_DROPPED, 0))
    unknown = max(spec.DRIVE_REQUEST_NAMES) + 1
    d = decide_drive_command(ENABLED, unknown, ignore_disable=1, drop_disable=1)
    expect("an unknown request: as before (IDLE), counts kept",
           (d.state, d.ignore_left, d.drop_left), (IDLE, 1, 1))


def t_drive_line_format() -> None:
    normal = spec.DRIVE_PROFILE_NORMAL
    n = profile_name(normal)
    expect("applied", format_drive_line(ENABLE, normal, decide_drive_command(IDLE, ENABLE),
                                        False), f"[drive] ENABLE profile={n} -> ENABLED")
    expect("refused ENABLE",
           format_drive_line(ENABLE, normal,
                             decide_drive_command(IDLE, ENABLE, refuse_drive=True), True),
           f"[drive] ENABLE profile={n} -> IDLE (refusing)")
    expect("refused DISABLE",
           format_drive_line(DISABLE, normal,
                             decide_drive_command(ENABLED, DISABLE, refuse_stop=True), False),
           f"[drive] DISABLE profile={n} -> ENABLED")
    expect("ignored",
           format_drive_line(DISABLE, normal,
                             decide_drive_command(ENABLED, DISABLE, ignore_disable=2), False),
           f"[drive] DISABLE profile={n} -> ENABLED (IGNORED, 1 more to ignore)")
    expect("dropped",
           format_drive_line(DISABLE, normal,
                             decide_drive_command(ENABLED, DISABLE, drop_disable=1), False),
           f"[drive] DISABLE profile={n} DROPPED (0 more to drop): never reached this MCB")


def t_presence() -> None:
    p = HmiPresence(gone_after_s=1.0)
    expect("no sample yet: never gone", p.tick(100.0), None)
    expect("first sample", p.sample(10.0), {"ev": "hmi_seen"})
    expect("steady stream", [p.sample(10.0 + i * 0.033) for i in range(1, 30)], [None] * 29)
    expect("under 1 s is not yet gone", p.tick(10.957 + 0.9), None)
    expect("gone", p.tick(12.5), {"ev": "hmi_gone", "silent_s": 1.543})
    expect("gone only once", p.tick(15.0), None)
    expect("back after a reset", p.sample(25.0), {"ev": "hmi_back", "gap_s": 14.043,
                                                   "returns": 1})
    expect("present again", (p.gone, p.tick(25.5)), (False, None))


def t_on_hmi_gone() -> None:
    expect("keep: stays ENABLED across a reset", state_on_hmi_gone(ENABLED, "keep"), ENABLED)
    expect("idle: ENABLED -> IDLE", state_on_hmi_gone(ENABLED, "idle"), IDLE)
    for state in (IDLE, ERROR, INITIALIZING):
        expect(f"idle keeps {state_name(state)}", state_on_hmi_gone(state, "idle"), state)


def t_xytwist_log() -> None:
    log = XyTwistLog(window_s=1.0)
    out = []
    for i in range(10):
        out += log.add(0.1 * i, (0.0, 0.0, 0.0, 0))
    expect("zero samples: only the first (button bits seen for the first time)",
           out, [{"ev": "xytwist", "x": 0.0, "y": 0.0, "twist": 0.0, "buttons": 0}])
    out = log.add(1.0, (0.0, 0.25, 0.0, 0))
    expect("a new window: the summary, then the non-zero sample", [r["ev"] for r in out],
           ["xytwist_summary", "xytwist"])
    expect("summary", {k: out[0][k] for k in ("span_s", "count", "nonzero", "max_abs_y",
                                              "buttons_seen")},
           {"span_s": 0.9, "count": 10, "nonzero": 0, "max_abs_y": 0.0, "buttons_seen": [0]})
    expect("the sample", out[1]["y"], 0.25)
    out = log.add(1.1, (0.0, 0.0, 0.0, 1))
    expect("a button change is logged", [r["ev"] for r in out], ["xytwist"])
    expect("an unchanged button is not", log.add(1.2, (0.0, 0.0, 0.0, 1)), [])
    out = log.add(1.3, (0.0, -0.5, float("nan"), 1))
    expect("NaN and negative logged", len(out), 1)
    summary = log.flush(4.0)
    expect("max |y|, NaN kept, span to the last sample, not to the flush",
           (summary["max_abs_y"], math.isnan(summary["max_abs_twist"]), summary["nonzero"],
            summary["count"], summary["span_s"]), (0.5, True, 2, 4, 0.3))
    expect("empty window: no summary", log.flush(3.0), None)


def t_event_log() -> None:
    ticks = iter([1.0, 2.5, 3.25])
    wall = datetime.datetime(2026, 10, 6, 12, 0, 0, 123000)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "sub", "events.jsonl")
        log = EventLog(path, clock=lambda: next(ticks), wall=lambda: wall)
        log.write("drive_command", request="DISABLE", action=ACTION_IGNORED)
        log.record({"ev": "hmi_gone", "silent_s": 1.5})
        log.record(None)
        log.write("xytwist", x=float("nan"))
        log.close()
        with open(path, "a", encoding="utf-8") as f:
            f.write('{"ev": "half')  # a writer caught mid-line
        events = read_events(path)
    expect("records", [(e["ev"], e["mono"]) for e in events],
           [("drive_command", 1.0), ("hmi_gone", 2.5), ("xytwist", 3.25)])
    expect("wall clock", events[0]["t"], "2026-10-06T12:00:00.123")
    expect("fields", (events[0]["request"], events[0]["action"], events[1]["silent_s"]),
           ("DISABLE", "ignored", 1.5))
    expect("NaN survives", math.isnan(events[2]["x"]), True)
    expect("no path: no-op", EventLog(None).write("x"), None)
    expect("missing file: nothing", read_events(os.path.join(tempfile.gettempdir(),
                                                             "no-such-sim-log.jsonl")), [])


def t_parse_mode_command() -> None:
    expect("ign 3", parse_mode_command("ign 3"), ("ign", 3))
    expect("ign alone clears", parse_mode_command("ign"), ("ign", 0))
    expect("drop 1", parse_mode_command("drop 1"), ("drop", 1))
    expect("ongone idle", parse_mode_command("ongone idle"), ("ongone", "idle"))
    expect("ongone keep", parse_mode_command("ongone keep"), ("ongone", "keep"))
    expect("mark", parse_mode_command("mark b5e exit 1"), ("mark", "b5e exit 1"))
    expect("modes", parse_mode_command("modes"), ("modes", None))
    for bad in ("ign -1", "ign x", "ign 1 2", f"drop {MAX_FAULT_COUNT + 1}", "ongone",
                "ongone off", "mark", "modes now", "e", ""):
        try:
            parse_mode_command(bad)
        except ValueError:
            continue
        raise AssertionError(f"{bad!r} was accepted")
    expect("the old commands are not mode commands",
           [c for c in ("e", "ok", "x", "s", "p", "r", "a", "i", "z", "et boom", "q", "")
            if is_mode_command(c)], [])
    expect("the new ones are", all(is_mode_command(c) for c in
                                   ("ign 1", "drop", "ongone idle", "mark x", "modes")), True)


CASES = [
    ("SIM-001 ENABLE gives ENABLED and DISABLE gives IDLE from IDLE or ENABLED",
     t_plain_requests),
    ("SIM-002 x refuses only ENABLE and s refuses only DISABLE, as before", t_refusals_unchanged),
    ("SIM-003 a drive request neither clears ERROR nor finishes INITIALIZING",
     t_error_and_init_kept),
    ("SIM-004 ign N keeps ENABLED for the next N DISABLEs, then obeys", t_ignore_counts_down),
    ("SIM-005 drop N loses the next N DISABLEs without a reply, then obeys",
     t_drop_counts_down_and_is_silent),
    ("SIM-006 a DISABLE meets drop first, then ign, then s", t_precedence),
    ("SIM-007 only a DISABLE uses a count, in any state", t_counts_only_for_disable),
    ("SIM-008 the drive console line is unchanged for the old actions", t_drive_line_format),
    ("SIM-009 the HMI is gone after 1 s without XYTwist and back on the next sample",
     t_presence),
    ("SIM-010 ongone keep stays ENABLED across an HMI reset and idle drops to IDLE",
     t_on_hmi_gone),
    ("SIM-011 XYTwist: a summary per window plus every non-zero or button-change sample",
     t_xytwist_log),
    ("SIM-012 the event log writes one timestamped JSON object per line", t_event_log),
    ("SIM-013 the mode commands parse and reject bad arguments", t_parse_mode_command),
]


def run_cases(script: str, cases: list[tuple[str, Callable[[], None]]]) -> int:
    """Unity's format (case lines + summary), as tools/guards/guardlib.run_cases prints it."""
    fails = 0
    for name, fn in cases:
        line = inspect.getsourcelines(fn)[1]
        try:
            fn()
            print(f"{script}:{line}:{name}:PASS")
        except Exception as exc:  # noqa: BLE001 - every exception is a failed case
            fails += 1
            print(f"{script}:{line}:{name}:FAIL: {str(exc) or type(exc).__name__}")
            traceback.print_exc(file=sys.stdout)
    print("\n-----------------------")
    print(f"{len(cases)} Tests {fails} Failures 0 Ignored")
    print("OK" if fails == 0 else "FAIL")
    return 0 if fails == 0 else 1


def selftest() -> int:
    return run_cases("scripts/mcb_sim_logic.py", CASES)


if __name__ == "__main__":
    if sys.argv[1:] != ["selftest"]:
        print(__doc__)
        sys.exit(2)
    sys.exit(selftest())
