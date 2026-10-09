"""The rig every hazard bench step runs on: the simulated MCB, the remote UI and the injected
stick, with everything they see recorded for the graders. No motors: the MCB is the sim.

    with hazard_rig.Rig(ip, tree, out, groups) as rig:   # one step
        rig.begin()                  # sim IDLE, Locked, POST passed, stick centred (graded)
        rig.inject(*rig.cal.forward())
        rig.mark("x"); rig.sim("a"); rig.watch(3.0)
        ...
        trace = rig.collect()        # samples, events, STATE polls, marks
        rig.end()                    # clean-up: modes off, sim IDLE, Locked (graded)

Safety (the bench's rules, tools/bench/README.md; hazard-fixes.md §3 B1). The stick is
injected only after the proof that the simulated MCB is the only RTPS participant the board
can talk to: no other sim, self test, host or drive-game process runs on this PC
(common.stray_peers) and an RTPS sweep of the bench subnet finds no responder but the board
(the same sweep as B0). `inject` refuses until that proof passed. Every step stops its
injection at the end; the board drops an injection 300 ms after its last refresh by itself.

How the remote UI is shared. The board serves one client, so the stick refresh (every
100 ms, expiry 300 ms) and the step's own commands go over one connection: every command is
sent under one lock, and TAP is done as PRESS, 150 ms, RELEASE so no single command blocks
the refresh for long. A refresh more than INJECT_LAPSE_S after the previous one is recorded;
a step whose injection lapsed in a graded window is not graded (NOT_RUN, a bench problem).

Verdicts. A step is PASS or FAIL on its graded checks. It is NOT_RUN when the firmware lacks
what it needs (no STATE, a STATE field that is null, a verb "not wired", no stick injection)
or when the bench could not do its part (sim not ready, injection lapsed, no serial port for
a step that reads the serial log): neither is a verdict on the firmware.
"""

from __future__ import annotations

import datetime
import importlib
import itertools
import json
import math
import os
import pathlib
import sys
import threading
import time
from typing import Callable

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import hazard_grade as hg  # noqa: E402
import peers  # noqa: E402
import scenario_hazards  # noqa: E402
import ui_client  # noqa: E402

INJECT_PERIOD_S = 0.1     # C1 §6: STICK refreshed every 100 ms
INJECT_LAPSE_S = 0.28     # the board drops an injection 300 ms after its last refresh
POLL_S = 0.1              # C3 §7: STATE is polled every 100 ms
TAP_HOLD_S = 0.15         # PRESS..RELEASE: several LVGL pointer reads (every 30 ms)
HOLD_MS = 2000            # the unlock and exit holds: 1.5 s fill plus margin
SKEW_LIMIT_S = 0.25       # a mark's stamp vs the runner's send time
BACK_WITHIN_S = 90.0      # a restart: remote UI back
SIM_READY_S = 45.0
SIM_RETRY_S = 3.0
SIM_FIND_S = 90.0

MENU_KEY = (360, 1198)                 # scripts/hmi_ui.py MENU_KEY
PROFILE_Y = 195 + 675 + 162 // 2       # scenario_hazards: DriveScreen's profile buttons
PROFILE_NORMAL = (257 + 207 // 2, PROFILE_Y)
PROFILE_LOW = (484 + 207 // 2, PROFILE_Y)
# SkunkWorksScreen: actions_spec.h's five tiles (Restart HMI is the fifth), 320x240, two to
# a row, 20 px apart, rows centred (ui_SkunkWorksScreen.c, ui_comp_slottile.c).
RESTART_ACTION = "RESTART_HMI"
# A command never makes the stick wait: a refresh goes first once the last is this old (a
# WiFi stall of 0.27 + 0.20 s on two commands in a row lapsed it on 2026-10-09 at 50 ms).
PRE_REFRESH_S = 0.02


def skunk_actions(tree: pathlib.Path) -> list[str]:
    """The Skunk Works tiles in draw order, from the tree's actions_spec.h (the image's
    own table: the grid's size and Restart HMI's place come from it, not from a constant)."""
    import re
    path = tree / "components" / "hmi_ui" / "include" / "hmi_ui" / "actions_spec.h"
    return re.findall(r"^\s*X\((\w+),", path.read_text(encoding="utf-8"), re.M)


def tile_centre(index: int, count: int, tile0: tuple[int, int, int, int],
                width: int = 720, gap: int = 20) -> tuple[int, int]:
    """Where tile `index` of `count` sits in the wrapping, centred flex grid whose first
    tile FOCUS reported at (x, y, w, h)."""
    _, y0, w, h = tile0
    cols = max(1, (width + gap) // (w + gap))
    row, col = divmod(index, cols)
    in_row = min(cols, count - row * cols)
    left = (width - (in_row * w + (in_row - 1) * gap)) // 2
    return left + col * (w + gap) + w // 2, y0 + row * (h + gap) + h // 2


class NotRun(Exception):
    """The step cannot be graded: `kind` is "firmware" (the image lacks what the step
    needs) or "bench" (the rig could not do its part)."""

    def __init__(self, kind: str, reason: str):
        super().__init__(f"{kind}: {reason}")
        self.kind = kind
        self.reason = reason


class HazardStep(scenario_hazards.Step):
    """scenario_hazards.Step plus NOT_RUN."""

    def __init__(self, name: str, out: pathlib.Path):
        super().__init__(name, out)
        self.not_verified = []
        self.not_run: str | None = None
        self.details: list[str] = []  # verdict details shown with the verdict
        # A criterion the step could only observe indirectly (B5''-18b's fallback): the step
        # is never PASS on it; with every graded check passed it is NOT_RUN, "partial: ...".
        self.partial: str | None = None

    def result(self, characterisation: bool = False) -> dict:
        out = super().result(characterisation)
        if self.details:
            out["detail"] = "; ".join(self.details)
        if self.partial is not None:
            out["partial"] = self.partial
            if out["verdict"] == "PASS":
                out["verdict"] = "NOT_RUN"
                out["reason"] = f"partial: {self.partial}"
        if self.not_run is not None:
            out["verdict"] = "NOT_RUN"
            out["reason"] = self.not_run
        return out


# ---------------------------------------------------------------- the proof


def rtps_sweep(tree: pathlib.Path) -> list[str]:
    """RTPS responders on the bench subnet (as run_bench.Run.rtps_sweep, B0)."""
    import ipaddress
    sys.path.insert(0, str(tree / "scripts"))
    rtps_net = importlib.import_module("rtps_net")
    hosts = [str(h) for h in ipaddress.IPv4Network(common.SUBNET).hosts()
             if str(h) not in (common.PC_IP, *common.SWEEP_SKIP)]
    found: set[str] = set()
    for i in range(0, len(hosts), 64):
        found |= set(rtps_net._run_listener(hosts[i:i + 64], 6.0, common.PC_IP,
                                            stop_on_first=False))
    return sorted(found)


def sole_sim_proof(ip: str, sweep: Callable[[], list[str]],
                   strays: Callable[[], list[str]] = common.stray_peers) -> tuple[bool, str]:
    """Before any stick injection (and before this run's sim starts): no other RTPS peer
    process on this PC, and no RTPS responder on the subnet but the board itself."""
    found_strays = strays()
    if found_strays:
        return False, "other RTPS peer processes on this PC: " + "; ".join(found_strays)
    found = sweep()
    others = [a for a in found if a != ip]
    if others:
        return False, f"RTPS responders other than the board {ip}: {others}"
    return True, f"no stray process; RTPS responders {found} (the board only)"


# ---------------------------------------------------------------- the injected stick


class Injector:
    """Refreshes one STICK target every INJECT_PERIOD_S on its own thread."""

    def __init__(self, send: Callable[[int, int, int, int, int], object]):
        self._send = send
        self._lock = threading.Lock()
        self._target: tuple[int, int, int, int] | None = None
        self._once: tuple[int, int, int, int] | None = None
        self._seq = itertools.count(1)  # thread-safe enough: next() is atomic in CPython
        self._last: float | None = None
        # (sent, gap) over INJECT_LAPSE_S between STICK sends, from the remote-UI log
        # (stick_lapses, at collect); pauses (wall clock) end a run of refreshes on purpose.
        self.lapses: list[tuple[str, float]] = []
        self.pauses: list[datetime.datetime] = []
        self.log: list[tuple[float, int, int, int, int]] = []
        self.error: str | None = None
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def due(self, age: float) -> tuple[int, int, int, int] | None:
        """The standing target when its last refresh is at least `age` s old, else None."""
        target, last = self._target, self._last
        if target is None or last is None or time.monotonic() - last < age:
            return None
        return target

    def push_inline(self, target: tuple[int, int, int, int]) -> float:
        """A refresh sent by the thread that holds the connection, just before its own
        command (Rig: no command makes the stick wait out a slow round trip)."""
        return self._push(target)

    def _push(self, target: tuple[int, int, int, int]) -> float:
        t = time.monotonic()
        self._send(*target, next(self._seq))
        self._last = t
        self.log.append((round(t, 3), *target))
        return t

    def set(self, h: int, v: int, tw: int, mask: int = 0, once: bool = False) -> float:
        """Send now and keep refreshing; with `once`, this message carries `mask` and the
        refreshes after it go back to the standing target. Returns the send time."""
        with self._lock:
            msg = (hg.clamp_mv(h), hg.clamp_mv(v), hg.clamp_mv(tw), mask)
            if once:
                if self._target is None:
                    raise NotRun("bench", "a one-cycle message needs a standing target")
                t = self._push(msg)
            else:
                self._target = msg
                t = self._push(msg)
            self._wake.set()
            return t

    def pause(self) -> None:
        """Stop refreshing (the board lets go 300 ms later); set() starts again."""
        with self._lock:
            self._target = None
            self._last = None
            self.pauses.append(datetime.datetime.now())

    def _run(self) -> None:
        while not self._stop.is_set():
            self._wake.wait(INJECT_PERIOD_S)
            self._wake.clear()
            with self._lock:
                if self._target is None or self._stop.is_set():
                    continue
                if self._last is not None and time.monotonic() - self._last < INJECT_PERIOD_S * 0.8:
                    continue
                try:
                    self._push(self._target)
                except Exception as e:  # noqa: BLE001 - the step reads it
                    self.error = f"{type(e).__name__}: {e}"
                    self._target = None

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()
        self._thread.join(timeout=2.0)


def stick_lapses(rows: list[dict], pauses: list[datetime.datetime],
                 limit: float = INJECT_LAPSE_S) -> list[tuple[str, float]]:
    """Gaps over `limit` between consecutive STICK sends in the remote-UI log (rows with
    "cmd" and "sent", ui_client's CommandLog), except across a pause. The send times are
    what the board sees (its injection expires 300 ms after the last one): the injector's
    own clock counts the wait for the connection too, and on 2026-10-09 it reported a
    0.48 s lapse where the log shows no send gap over 0.27 s."""
    sends = [datetime.datetime.fromisoformat(r["sent"]) for r in rows
             if str(r.get("cmd", "")).startswith("STICK") and "error" not in r]
    out = []
    for a, b in zip(sends, sends[1:]):
        gap = (b - a).total_seconds()
        if gap > limit and not any(a <= p <= b for p in pauses):
            out.append((b.isoformat(timespec="milliseconds"), round(gap, 3)))
    return out


SIM_RESTART_NOTE = "sim restarted after HMI reboot (RTPS rediscovery not supported by the sim)"
STATE_COMMANDS = {"ENABLED": "a", "IDLE": "ok", "ERROR": "e", "INITIALIZING": "z"}


def carry_over(modes_line: str, state: str | None, paused: bool) -> list[str]:
    """The stdin commands that give a fresh sim the old one's modes and MCB state: its
    `modes` answer (rtps_mcb_sim.describe_modes), the last MibStatus state it published,
    and whether MibStatus was paused. Sent before the new sim has found the HMI, so the
    HMI's first MibStatus after its reboot already carries the old state."""
    import re
    m = re.search(r"refuse ENABLE=(True|False) refuse DISABLE=(True|False) ignore next "
                  r"(\d+) DISABLE\(s\), drop next (\d+) DISABLE\(s\), on HMI gone: (\w+)",
                  modes_line)
    if m is None:
        raise NotRun("bench", f"cannot read the sim's modes to carry over: {modes_line!r}")
    out = [f"ongone {m.group(5)}", f"ign {m.group(3)}", f"drop {m.group(4)}"]
    if m.group(1) == "True":
        out.append("x")  # a new sim starts with both refusals off: one toggle sets each
    if m.group(2) == "True":
        out.append("s")
    if state is not None:
        if state not in STATE_COMMANDS:
            raise NotRun("bench", f"cannot carry the sim's state {state!r} over")
        out.append(STATE_COMMANDS[state])
    if paused:
        out.append("p")
    return out


# ---------------------------------------------------------------- serial


class SerialWatch:
    """The board's serial log, captured without a reset on a thread (board.capture).
    `lines` keeps each line's arrival time on the runner's clock (time.monotonic()), the
    clock the sim's log and the STATE polls use."""

    def __init__(self, port: str, seconds: float, capture: Callable | None = None,
                 reset: bool = False):
        import board
        self.text = ""
        self.lines: list[tuple[float, str]] = []
        self._stop = threading.Event()
        capture = capture or board.capture

        def keep(cap, line: str) -> bool:
            # Kept as it arrives: stop() never depends on the capture thread finishing.
            self.lines.append((round(time.monotonic(), 4), line))
            return self._stop.is_set()

        def run() -> None:
            # `stop` ends the capture within one read timeout and frees the port for the
            # next watch (on 2026-10-09 a watch on a quiet board held it for its whole
            # window, so the next boot's watch read nothing).
            capture(port, seconds, reset=reset, on_line=keep, stop=self._stop)

        self._thread = threading.Thread(target=run, daemon=True)
        self._thread.start()

    def stop(self) -> str:
        self._stop.set()
        self._thread.join(timeout=10.0)
        self.text = "\n".join(line for _, line in self.lines) + ("\n" if self.lines else "")
        return self.text


# ---------------------------------------------------------------- the rig


class Rig:
    def __init__(self, ip: str, tree: pathlib.Path, out: pathlib.Path, step: HazardStep,
                 groups: set[str], port: str | None = None, proven: str | None = None,
                 sweep: Callable[[], list[str]] | None = None, params: dict | None = None):
        self.ip = ip
        self.params = dict(params or {})  # per-run settings a step may read (C2-15's soak_s)
        self.tree = tree
        self.out = out
        self.st = step
        self.groups = set(groups)
        self.port = port
        self.restart_watch: SerialWatch | None = None  # restart_hmi(serial_s) from Drive
        self._sample_parts: list[pathlib.Path] = []  # samples of sims replaced after a reboot
        self._carry: list[str] = []
        self._carry_state: str | None = None
        self.proof = proven
        self._sweep = sweep or (lambda: rtps_sweep(tree))
        self.trace = hg.Trace()
        self.cal: hg.Cal | None = None
        self.sim: peers.SimChild | None = None
        self.hmi = None
        self.inj: Injector | None = None
        self._io = threading.RLock()  # re-entered by the inline refresh before a command
        self._hmi_ui = None
        self._t0 = time.monotonic()

    # --- lifetime ---------------------------------------------------------------

    def __enter__(self) -> "Rig":
        self.out.mkdir(parents=True, exist_ok=True)
        if self.proof is None:
            ok, detail = sole_sim_proof(self.ip, self._sweep)
            if not ok:
                raise NotRun("bench", f"injection preflight: {detail}")
            self.proof = detail
        self.st.record("sole_sim_proof", self.proof)
        sys.path.insert(0, str(self.tree / "scripts"))
        self._hmi_ui = importlib.import_module("hmi_ui")
        self.sim = self._start_sim()
        try:
            if not self.sim.wait_ready(SIM_READY_S):
                raise NotRun("bench", self.sim.not_ready_reason(SIM_READY_S))
            # This run's sim is a process tree under a venv (launcher + base interpreter).
            strays = common.stray_peers(exclude_trees={self.sim.proc.pid})
            if strays:
                raise NotRun("bench", "another RTPS peer started beside this run's sim: "
                             + "; ".join(strays))
            self.sim.event_mark("step-start")
            self.sim.send("jstart")
            self._connect()
        except BaseException:
            # __exit__ does not run when __enter__ raises: a sim left running here kept the
            # board's XYTwist and starved every later step's sim (bench run 2026-10-08).
            self.__exit__(None, None, None)
            raise
        return self

    def _start_sim(self, carry: list[str] | None = None) -> peers.SimChild:
        """A sim for this step. Until it has found the board (its "board <ip> via" line) it
        may exit at once ("Could not find the board": the board still booting); it is then
        started again, for up to SIM_FIND_S. `carry` (carry_over's commands) goes to it as
        soon as it has found the board, before it can publish to the HMI."""
        deadline = time.monotonic() + SIM_FIND_S
        tries = 0
        while True:
            tries += 1
            log = self.out / ("sim.log" if tries == 1 and carry is None
                              else f"sim-{len(self._sample_parts)}-{tries}.log")
            sim = peers.SimChild(self.ip, self.tree, log, event_log=self.out / "sim-events.jsonl")
            found = sim.wait_for(r"^board \S+ via ", min(15.0, max(1.0, deadline - time.monotonic())))
            if found and sim.proc.poll() is None:
                for cmd in carry or []:
                    sim.send(cmd)
                if tries > 1:
                    self.st.record("sim_tries", tries)
                return sim
            reason = sim.not_ready_reason()
            sim.stop()
            if time.monotonic() >= deadline:
                raise NotRun("bench", f"the sim could not find the board in {SIM_FIND_S:.0f} s "
                             f"({tries} tries): {reason}")
            time.sleep(SIM_RETRY_S)  # the board is still coming back

    def __exit__(self, *_: object) -> None:
        if self.inj is not None:
            self.inj.stop()
        if self.hmi is not None:
            self.st.records["remote_ui"] = self.hmi.stats()
            self.hmi.close()
        if self.sim is not None:
            self.sim.stop()
            self.sim = None

    def _connect(self) -> None:
        hmi = ui_client.open_hmi(self._hmi_ui, self.ip, self.out / "remote-ui.jsonl")
        raw_call = hmi._call

        def locked_call(*a, **k):
            with self._io:
                inj = self.inj
                if inj is not None and a and not str(a[0]).startswith("STICK"):
                    target = inj.due(PRE_REFRESH_S)
                    if target is not None:
                        inj.push_inline(target)
                return raw_call(*a, **k)

        hmi._call = locked_call
        hmi.tap = self.tap  # hmi_ui's go/home/open_menu tap through PRESS/RELEASE
        self.hmi = hmi
        if self.inj is None:
            self.inj = Injector(self._stick)

    def _stick(self, h: int, v: int, tw: int, mask: int, seq: int) -> None:
        try:
            self.hmi.stick(h, v, tw, mask, seq)
        except RuntimeError as e:
            text = str(e)
            if "needs CONFIG_HMI_BENCH_STICK_INJECT" in text:
                raise NotRun("firmware", "not a stick-injection build (STICK refused)") from None
            raise

    # --- the sim ----------------------------------------------------------------

    def sim_cmd(self, cmd: str) -> None:
        self.trace.sent.append((round(time.monotonic(), 3), f"sim {cmd}"))
        if not self.sim.command(cmd):
            raise NotRun("bench", f"the sim did not take {cmd!r}")

    def sim_toggle(self, cmd: str, want: bool) -> None:
        """`x` and `s` toggle: send until the sim reports the wanted state."""
        regex = {"x": r"refusing drive requests: (True|False)",
                 "s": r"refusing stop requests: (True|False)"}[cmd]
        for _ in range(2):
            m = self.sim.reply(cmd, regex)
            if m and (m.group(1) == "True") == want:
                self.trace.sent.append((round(time.monotonic(), 3), f"sim {cmd}={want}"))
                return
        raise NotRun("bench", f"could not set the sim's '{cmd}' to {want}")

    def sim_count(self, cmd: str, n: int) -> None:
        """`ign N` / `drop N`."""
        word = "ignoring" if cmd == "ign" else "dropping"
        if self.sim.reply(f"{cmd} {n}", rf"{word} the next {n}") is None:
            raise NotRun("bench", f"the sim did not take '{cmd} {n}'")
        self.trace.sent.append((round(time.monotonic(), 3), f"sim {cmd} {n}"))

    def sim_ongone(self, policy: str) -> None:
        """`ongone keep|idle`: what the sim does when the HMI goes away (a restart)."""
        if self.sim.reply(f"ongone {policy}", rf"on HMI gone: {policy}") is None:
            raise NotRun("bench", f"the sim did not take 'ongone {policy}'")
        self.trace.sent.append((round(time.monotonic(), 3), f"sim ongone {policy}"))

    def mark(self, label: str) -> float:
        """A mark in the sim's log just before an action; returns the runner's send time
        (the grader uses the sim's stamp of it, from the log)."""
        t = time.monotonic()
        if not self.sim.event_mark(label):
            raise NotRun("bench", f"the sim did not log mark {label!r}")
        self.trace.sent.append((round(t, 3), f"mark {label}"))
        return t

    def y_now(self) -> float | None:
        at = self.sim.mark()
        self.sim.send("jlast")
        m = self.sim.wait_for(r"^XYLAST (.*)$", 5.0, at)
        if not m:
            return None
        last = json.loads(m.group(1))
        return None if last is None else float(last[2])

    def wait_y(self, pred: Callable[[float], bool], within: float) -> bool:
        deadline = time.monotonic() + within
        while time.monotonic() < deadline:
            y = self.y_now()
            if y is not None and not math.isnan(y) and pred(y):
                return True
            time.sleep(0.05)  # poll period, bounded by the deadline
        return False

    # --- the remote UI -------------------------------------------------------------

    def state(self) -> dict:
        try:
            s = self.hmi.state()
        except self._hmi_ui.BenchVerbError as e:
            if e.not_in_firmware or "unknown command" in e.reply:
                raise NotRun("firmware", f"STATE not answered: {e.reply}") from None
            raise
        self.trace.states.append((round(time.monotonic(), 3), s))
        return s

    def need(self, *fields: str, state: dict | None = None) -> dict:
        """The STATE fields this step reads: a null one means its hook is not wired."""
        s = state if state is not None else self.state()
        missing = [f for f in fields if s.get(f) is None]
        if missing:
            raise NotRun("firmware", f"STATE fields not wired in this firmware: {missing}")
        return s

    def watch(self, seconds: float, until: Callable[[dict], bool] | None = None) -> dict | None:
        """Poll STATE every POLL_S for `seconds`; with `until`, stop at the first poll that
        satisfies it and return that state (None if none did)."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            s = self.state()
            if until is not None and until(s):
                return s
            time.sleep(POLL_S)  # the poll period
        return None

    def bench_verb(self, call: Callable[[], str], what: str) -> None:
        try:
            call()
        except self._hmi_ui.BenchVerbError as e:
            if e.not_in_firmware:
                raise NotRun("firmware", f"{what}: {e.reply}") from None
            raise
        self.trace.sent.append((round(time.monotonic(), 3), what))

    def permit(self, what: str, value: str) -> None:
        self.bench_verb(lambda: self.hmi.permit(what, value), f"PERMIT {what.upper()} {value}")

    def tap(self, x: int, y: int) -> str:
        self.hmi.command(f"PRESS {x} {y}")
        time.sleep(TAP_HOLD_S)  # a press several pointer reads long
        out = self.hmi.command("RELEASE")
        time.sleep(TAP_HOLD_S)
        return out

    def hold_button(self, ms: int = HOLD_MS) -> float:
        """The stick button held `ms` (unlock, exit or calibrate hold); returns the press time."""
        t = time.monotonic()
        self.hmi.command("BTN 1")
        time.sleep(ms / 1000.0)  # the hold itself
        self.hmi.command("BTN 0")
        return t

    def burger(self) -> None:
        self.tap(*MENU_KEY)

    def go(self, row: str) -> None:
        self.hmi.go(row)

    def home(self) -> None:
        self.hmi.home()

    def focus(self) -> tuple[int, int, int, int, int] | None:
        import re
        reply = self.hmi.command("FOCUS")
        m = re.search(r"keypad:(-?\d+),(-?\d+),(\d+)x(\d+),state=\d+,groupsize=(\d+)", reply)
        return tuple(int(v) for v in m.groups()) if m else None

    # --- the stick --------------------------------------------------------------------

    def read_cal(self) -> hg.Cal:
        s = self.need("cal")
        cal = hg.Cal.from_state(s)
        if cal is None:
            raise NotRun("firmware", f"STATE cal is not three [min, centre, max]: {s.get('cal')}")
        self.cal = cal
        self.trace.cal = s["cal"]
        return cal

    def inject(self, h: int, v: int, tw: int, mask: int = 0, once: bool = False) -> float:
        if self.proof is None:
            raise NotRun("bench", "no stick injection before the sole-sim proof")
        t = self.inj.set(h, v, tw, mask, once)
        if self.inj.error:
            raise NotRun("bench", f"stick injection failed: {self.inj.error}")
        return t

    def centre(self) -> float:
        return self.inject(*self.cal.centre())

    def forward(self) -> float:
        return self.inject(*self.cal.forward())

    # --- restarts ---------------------------------------------------------------------

    def restart_hmi(self, serial_s: float = 0.0) -> float:
        """The Restart HMI tile (Skunk Works, the fifth): a software reset. Returns the tap
        time. The connection is closed; wait_back() reconnects."""
        self.restart_watch = None
        if self.state().get("screen") == "DriveScreen":
            return self._restart_by_port(serial_s)
        actions = skunk_actions(self.tree)
        if RESTART_ACTION not in actions:
            raise NotRun("bench", f"no {RESTART_ACTION} in the tree's actions_spec.h")
        self.go("Skunk Works")
        if self.watch(3.0, until=hg.screen_is("SkunkWorksScreen")) is None:
            raise NotRun("bench", "could not open Skunk Works")
        f0 = self.focus()
        # The keypad group may hold more than the tiles (it did on 9e0fb77: 6 for 5 tiles);
        # the first tile is what places the grid.
        if f0 is None or f0[4] < len(actions):
            raise NotRun("bench", f"Skunk Works' tile grid not as expected (FOCUS {f0})")
        x, y = tile_centre(actions.index(RESTART_ACTION), len(actions), f0[:4])
        self.st.record("restart_tile", {"tile0": f0[:4], "tap": [x, y]})
        self.inj.pause()
        t = time.monotonic()
        try:
            self.tap(x, y)
        except ui_client.RemoteUiError:
            pass  # the board may restart before answering the RELEASE
        self._drop_hmi()
        self._retire_sim()
        return t

    def _restart_by_port(self, serial_s: float) -> float:
        """From DriveScreen the menu is out of reach (the burger key asks to stop there):
        reset the chip through the port (B2's RTS pulse; reset reason USB, a clean one).
        The same capture keeps reading the boot for `serial_s` (restart_watch): the port
        has one owner at a time (a second open was refused, 2026-10-09)."""
        if not self.port:
            raise NotRun("bench", "restarting from DriveScreen needs the board's port")
        self.st.record("restart", "RTS reset through the port (from DriveScreen)")
        self.inj.pause()
        self._drop_hmi()
        t = time.monotonic()
        watch = SerialWatch(self.port, max(serial_s, 2.0), reset=True)
        self._retire_sim()
        if serial_s > 0:
            self.restart_watch = watch
        else:
            time.sleep(1.0)  # the reset pulse is sent; nothing to read
            watch.stop()
        return t

    def crash(self) -> float:
        self.inj.pause()
        t = time.monotonic()
        try:
            self.hmi.crash()
        except Exception:  # noqa: BLE001 - the board aborts; its answer may be lost
            pass
        self._drop_hmi()
        self._retire_sim()
        return t

    def _drop_hmi(self) -> None:
        if self.hmi is not None:
            self.st.records.setdefault("remote_ui_before_restart", []).append(self.hmi.stats())
            try:
                self.hmi.close()
            except OSError:
                pass

    def wait_back(self, within: float = BACK_WITHIN_S) -> float:
        """After a restart: the remote UI answers PING again; reconnect. Returns the time."""
        time.sleep(2.0)  # the old connection's board is going down
        ok, detail = ui_client.ping(self.ip, within)
        if not ok:
            raise NotRun("bench", f"the board did not come back: {detail}")
        self._connect()
        if self.sim is None:
            self._relaunch_sim()
        return time.monotonic()

    def _save_samples(self, path: pathlib.Path) -> None:
        at = self.sim.mark()
        self.sim.send(f"jsave {path}")
        if self.sim.wait_for(r"^JSAVED \d+ ", 20.0, at) is None:
            raise NotRun("bench", "the sim did not save its XYTwist samples")

    def _retire_sim(self) -> None:
        """Right after an HMI reboot is triggered. The sim hears nothing more from a
        restarted board (its RTPS discovery does not re-match a restarted participant,
        2026-10-09), so it is replaced: this one's samples are kept for collect(), its
        modes and MCB state are kept for the next (carry_over), and it stops now, so the
        HMI's first MibStatus after its boot (and its boot DISABLE) is the new sim's."""
        old = self.sim
        if old is None:
            return
        part = self.out / f"xytwist-{len(self._sample_parts) + 1}.jsonl"
        self._save_samples(part)
        self._sample_parts.append(part)
        modes = old.reply("modes", r"(refuse ENABLE=.*)$")
        events = old.events()
        state = next((e["state"] for e in reversed(events) if e.get("ev") == "mib_publish"),
                     None)
        pauses = [e["ev"] for e in events if e.get("ev") in ("pause", "resume")]
        self._carry = carry_over(modes.group(1) if modes else "", state,
                                 bool(pauses) and pauses[-1] == "pause")
        self._carry_state = state
        old.stop()
        self.sim = None

    def _relaunch_sim(self) -> None:
        """The new sim after a reboot, with the old one's modes and state (an
        approximation, shown in the verdict's detail)."""
        commands = self._carry
        state = self._carry_state
        self.sim = self._start_sim(commands)
        if not self.sim.wait_ready(SIM_READY_S):
            raise NotRun("bench", "after the HMI reboot: " + self.sim.not_ready_reason())
        strays = common.stray_peers(exclude_trees={self.sim.proc.pid})
        if strays:
            raise NotRun("bench", "another RTPS peer beside the restarted sim: " + "; ".join(strays))
        self.sim.event_mark("sim-restarted")
        self.sim.send("jstart")
        self.st.record("sim_restart", {"carried": commands, "state": state})
        if SIM_RESTART_NOTE not in self.st.details:
            self.st.details.append(SIM_RESTART_NOTE)

    # --- the start and end every step shares ----------------------------------------

    def begin(self, enter_locked: bool = True) -> None:
        """C1 §6 / C3 §7 start: stick centred, every sim mode off, sim IDLE, POST passed
        (C3 landed: STATE post = PASS, F6; else PERMIT POST pass), Locked."""
        self.read_cal()
        self.centre()
        self._modes_off()
        self.sim_cmd("ok")
        if "c3" in self.groups:
            s = self.watch(5.0, until=hg.field_is("post", "PASS"))
            self.st.check("set-up: STATE post = PASS (C3 F6)", s is not None,
                          f"post {self.state().get('post')}")
        else:
            self.permit("post", "pass")
        if enter_locked:
            self.home()
            s = self.watch(5.0, until=lambda s: s.get("screen") == "LockedScreen"
                           and s.get("menu_open") is not True)
            self.st.check("set-up: Locked", s is not None,
                          f"screen {self.state().get('screen')}")
        failed = [c["check"] for c in self.st.checks if c["check"].startswith("set-up")
                  and not c["ok"]]
        if failed:
            # A step does nothing on a board that is not where it starts (on 2026-10-09 a
            # B5''-21 whose start failed went on to CRASH the board for the next step).
            raise NotRun("bench", f"set-up failed: {failed}")

    def _modes_off(self) -> None:
        self.sim_count("ign", 0)
        self.sim_count("drop", 0)
        self.sim_toggle("s", False)
        self.sim_toggle("x", False)
        self.sim_ongone("keep")
        self.sim_cmd("r")

    def end(self) -> None:
        """Graded clean-up: centre, modes off, sim IDLE, LockedScreen, menu closed."""
        try:
            if self.cal is not None:
                self.centre()
            self._modes_off()
            self.sim_cmd("ok")
            s = self.watch(3.0, until=hg.screen_is("LockedScreen"))
            if s is not None and s.get("menu_open"):
                self.home()
                s = self.watch(3.0, until=lambda s: s.get("screen") == "LockedScreen"
                               and not s.get("menu_open"))
            self.st.check("clean-up: sim IDLE -> Locked", s is not None,
                          f"screen {self.state().get('screen')}")
        finally:
            if self.inj is not None:
                self.inj.pause()

    # --- the record ---------------------------------------------------------------

    def collect(self) -> hg.Trace:
        """The step's samples and sim events into the trace; the marks from the log."""
        path = self.out / "xytwist.jsonl"
        self._save_samples(path)
        import sim_child
        self.trace.samples = [s for part in [*self._sample_parts, path]
                              for s in sim_child.read_samples(part)]
        events = self.sim.events("step-start")
        self.trace.events = events
        self.trace.marks = {e["label"]: e["mono"] for e in events if e.get("ev") == "mark"}
        skews = []
        for t, what in self.trace.sent:
            if what.startswith("mark ") and what[5:] in self.trace.marks:
                skews.append(abs(self.trace.marks[what[5:]] - t))
        self.st.record("clock_skew_max_s", round(max(skews), 3) if skews else None)
        if skews and max(skews) > SKEW_LIMIT_S:
            raise NotRun("bench", f"the sim's clock and the runner's differ by "
                         f"{max(skews):.3f} s (> {SKEW_LIMIT_S} s)")
        if self.inj is not None:
            log = self.out / "remote-ui.jsonl"
            rows = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()
                    if line.strip()] if log.exists() else []
            self.inj.lapses = stick_lapses(rows, self.inj.pauses)
            self.st.record("injection_lapses", self.inj.lapses)
        (self.out / "states.jsonl").write_text(
            "".join(json.dumps([t, s]) + "\n" for t, s in self.trace.states), encoding="utf-8")
        return self.trace
