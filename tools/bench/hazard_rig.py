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

import importlib
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

MENU_KEY = (360, 1198)                 # scripts/hmi_ui.py MENU_KEY
PROFILE_Y = 195 + 675 + 162 // 2       # scenario_hazards: DriveScreen's profile buttons
PROFILE_NORMAL = (257 + 207 // 2, PROFILE_Y)
PROFILE_LOW = (484 + 207 // 2, PROFILE_Y)
# JoystickScreen: ui_CalibrateButton 360x162 at (330, 572) in ui_JoystickContent, which
# starts at y 195 (body) + 52 (padding) + 47 (title) + 34 (row gap).
CALIBRATE_BUTTON = (330 + 360 // 2, 195 + 52 + 47 + 34 + 572 + 162 // 2)
# SkunkWorksScreen: actions_spec.h's five tiles (Restart HMI is the fifth), 320x240, two to
# a row, 20 px apart, rows centred (ui_SkunkWorksScreen.c, ui_comp_slottile.c).
RESTART_TILE_INDEX = 4
SKUNK_TILES = 5


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

    def result(self, characterisation: bool = False) -> dict:
        out = super().result(characterisation)
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
        self._seq = 0
        self._last: float | None = None
        self.lapses: list[tuple[float, float]] = []   # (t, gap) over INJECT_LAPSE_S
        self.log: list[tuple[float, int, int, int, int]] = []
        self.error: str | None = None
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _push(self, target: tuple[int, int, int, int]) -> float:
        self._seq += 1
        t = time.monotonic()
        self._send(*target, self._seq)
        if self._last is not None and t - self._last > INJECT_LAPSE_S:
            self.lapses.append((round(t, 3), round(t - self._last, 3)))
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


# ---------------------------------------------------------------- serial


class SerialWatch:
    """The board's serial log, captured without a reset on a thread (board.capture)."""

    def __init__(self, port: str, seconds: float):
        import board
        self.text = ""
        self._stop = threading.Event()

        def run() -> None:
            cap = board.capture(port, seconds, reset=False,
                                on_line=lambda c, line: self._stop.is_set())
            self.text = cap.text()

        self._thread = threading.Thread(target=run, daemon=True)
        self._thread.start()

    def stop(self) -> str:
        self._stop.set()
        self._thread.join(timeout=10.0)
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
        self.proof = proven
        self._sweep = sweep or (lambda: rtps_sweep(tree))
        self.trace = hg.Trace()
        self.cal: hg.Cal | None = None
        self.sim: peers.SimChild | None = None
        self.hmi = None
        self.inj: Injector | None = None
        self._io = threading.Lock()
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
        self.sim = peers.SimChild(self.ip, self.tree, self.out / "sim.log",
                                  event_log=self.out / "sim-events.jsonl")
        if not self.sim.wait_ready(SIM_READY_S):
            raise NotRun("bench", self.sim.not_ready_reason(SIM_READY_S))
        strays = common.stray_peers({self.sim.proc.pid})
        if strays:
            raise NotRun("bench", "another RTPS peer started beside this run's sim: "
                         + "; ".join(strays))
        self.sim.event_mark("step-start")
        self.sim.send("jstart")
        self._connect()
        return self

    def __exit__(self, *_: object) -> None:
        if self.inj is not None:
            self.inj.stop()
        if self.hmi is not None:
            self.st.records["remote_ui"] = self.hmi.stats()
            self.hmi.close()
        if self.sim is not None:
            self.sim.stop()

    def _connect(self) -> None:
        hmi = ui_client.open_hmi(self._hmi_ui, self.ip, self.out / "remote-ui.jsonl")
        raw_call = hmi._call

        def locked_call(*a, **k):
            with self._io:
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

    def restart_hmi(self) -> float:
        """The Restart HMI tile (Skunk Works, the fifth): a software reset. Returns the tap
        time. The connection is closed; wait_back() reconnects."""
        self.go("Skunk Works")
        f0 = self.focus()
        if f0 is None or f0[4] != SKUNK_TILES:
            raise NotRun("bench", f"Skunk Works' tile grid not as expected (FOCUS {f0})")
        x0, y0, w, h, _ = f0
        cols = max(1, (720 + 20) // (w + 20))
        row, col = divmod(RESTART_TILE_INDEX, cols)
        in_row = min(cols, SKUNK_TILES - row * cols)
        left = (720 - (in_row * w + (in_row - 1) * 20)) // 2
        x = left + col * (w + 20) + w // 2
        y = y0 + row * (h + 20) + h // 2
        self.st.record("restart_tile", {"tile0": f0[:4], "tap": [x, y]})
        self.inj.pause()
        t = time.monotonic()
        try:
            self.tap(x, y)
        except ui_client.RemoteUiError:
            pass  # the board may restart before answering the RELEASE
        self._drop_hmi()
        return t

    def crash(self) -> float:
        self.inj.pause()
        t = time.monotonic()
        try:
            self.hmi.crash()
        except Exception:  # noqa: BLE001 - the board aborts; its answer may be lost
            pass
        self._drop_hmi()
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
        return time.monotonic()

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
        at = self.sim.mark()
        self.sim.send(f"jsave {path}")
        if self.sim.wait_for(r"^JSAVED \d+ ", 20.0, at) is None:
            raise NotRun("bench", "the sim did not save its XYTwist samples")
        import sim_child
        self.trace.samples = sim_child.read_samples(path)
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
            self.st.record("injection_lapses", self.inj.lapses)
        (self.out / "states.jsonl").write_text(
            "".join(json.dumps([t, s]) + "\n" for t, s in self.trace.states), encoding="utf-8")
        return self.trace
