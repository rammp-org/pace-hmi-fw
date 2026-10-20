#!/usr/bin/env python3
"""The MIB's side of the robot-wide e-stop: the latch, the deadman and clear attempts.

Pure logic with no I/O, so the bench simulator (rtps_mcb_sim.py) runs it and
test/estop_latch_test.py drives it through every race in docs/plans/estop.md
section 4. It is also the reference for the MIB itself (RAMMP-mib-fw): the
same rules, in the same order.

Terms: a "clear attempt" is the operator's request to end the stop; the stop is
"released" only when the attempt succeeds. Between the two the stop stays in effect.

Times are seconds on a monotonic clock, taken at receipt: staleness is never
judged by a message's stamp.

    latch = EStopLatch(watched=("hmi",))
    latch.on_message(spec.unpack_estop(payload), time.monotonic())
    latch.tick(time.monotonic())
    for msg in latch.outgoing(time.monotonic()): send(spec.pack_estop(*msg))
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import rammp_rtps as spec  # noqa: E402  (path setup must run first)


@dataclass
class Source:
    """What the latch knows of one publishing node."""
    last_pub_seq: int | None = None  # newest pub_seq received; None = forgotten
    engaged: bool = False            # what that newest message said
    last_rx: float | None = None


@dataclass
class ClearAttempt:
    """One clear attempt: clear_pub_seq per target, fixed when the attempt started."""
    clear_pub_seqs: dict[str, int]
    started: float
    turn: int = 0  # round-robin over the targets, one clear-attempt message per period


@dataclass
class EStopLatch:
    """The MIB's latch. `watched` are the sources the deadman expects to hear from."""
    watched: tuple[str, ...] = (spec.ESTOP_SELF,)
    period: float = spec.ESTOP_PERIOD_MS / 1000
    timeout: float = spec.ESTOP_TIMEOUT_MS / 1000
    clear_window: float = spec.ESTOP_CLEAR_WINDOW_MS / 1000
    deadman: bool = spec.ESTOP_DEADMAN
    name: str = spec.ESTOP_CLEARER
    # Latched at boot: nothing has been heard yet, so nothing is known to be safe.
    latched: bool = True
    cause: str = "boot"
    sources: dict[str, Source] = field(default_factory=dict)
    clear_attempt: ClearAttempt | None = None
    pub_seq: int = 0  # this MIB's own message number
    #: one line per thing worth logging; the caller drains it
    events: list[str] = field(default_factory=list)

    def _latch(self, cause: str) -> None:
        if not self.latched or self.cause != cause:
            self.events.append(f"LATCHED: {cause}")
        self.latched = True
        self.cause = cause

    def _silent(self, src: Source, now: float) -> bool:
        return src.last_rx is None or now - src.last_rx > self.timeout

    def on_message(self, msg: spec.EStop, now: float) -> None:
        """One received e-stop message, at receive time `now`."""
        if msg.source == self.name:
            return  # our own, looped back
        src = self.sources.setdefault(msg.source, Source())
        if self._silent(src, now):
            src.last_pub_seq = None  # silent past the timeout: a rebooted node restarts at 0
        src.last_rx = now
        # Engaging is never age- or pub_seq-checked: any engaged=true latches.
        if msg.engaged:
            self._latch(f"{msg.source}: {msg.reason}" if msg.reason else msg.source)
        # The source's word moves only on a newer message, so a reordered or
        # retransmitted older one cannot stand in for its newest.
        if src.last_pub_seq is None or spec.estop_seq_newer(msg.pub_seq, src.last_pub_seq):
            src.last_pub_seq = msg.pub_seq
            src.engaged = msg.engaged

    def start_clear_attempt(self, now: float) -> bool:
        """The operator asks to end the stop. Fixes one clear_pub_seq per known source
        (its last_pub_seq). False if not latched, or a watched source has never been
        heard, so the attempt could never succeed."""
        if not self.latched:
            return False
        seqs = {name: src.last_pub_seq for name, src in self.sources.items()
                if src.last_pub_seq is not None}
        missing = [name for name in self.watched if name not in seqs]
        if self.deadman and missing:
            self.events.append(f"clear attempt refused: never heard from {', '.join(missing)}")
            return False
        self.clear_attempt = ClearAttempt(clear_pub_seqs=seqs, started=now)
        self.events.append("clear attempt: " + ", ".join(
            f"{n} clear_pub_seq {s}" for n, s in seqs.items()))
        return True

    def engage(self, reason: str) -> None:
        """The MIB's own reason to stop (a bench button, a fault). Ends any clear attempt."""
        self.clear_attempt = None
        self._latch(f"{self.name}: {reason}")

    def tick(self, now: float) -> None:
        """Deadman and clear-attempt progress. Call at least every period."""
        if self.deadman:
            for name in self.watched:
                if self._silent(self.sources.get(name, Source()), now):
                    if self.clear_attempt is not None:
                        self.events.append(f"clear attempt expired: {name} silent")
                        self.clear_attempt = None
                    self._latch(f"{name} silent (deadman)")
        if self.clear_attempt is None:
            return
        # Release: every target's newest message is engaged=false, sent after the
        # attempt started (pub_seq > clear_pub_seq), and received recently.
        released = all(
            (src := self.sources.get(name)) is not None and src.last_pub_seq is not None
            and not src.engaged and spec.estop_seq_newer(src.last_pub_seq, clear_pub_seq)
            and not self._silent(src, now)
            for name, clear_pub_seq in self.clear_attempt.clear_pub_seqs.items())
        if released:
            self.events.append("RELEASED")
            self.latched = False
            self.cause = ""
            self.clear_attempt = None
        elif now - self.clear_attempt.started > self.clear_window:
            self.events.append("clear attempt expired: not completed within the clear window")
            self.clear_attempt = None

    def outgoing(self, _now: float) -> list[tuple]:
        """The message(s) to send this period, as pack_estop() arguments before `stamp`:
        (engaged, source, reason, pub_seq, clear_target, clear_pub_seq)."""
        self.pub_seq = (self.pub_seq + 1) & 0xFFFFFFFF
        if self.clear_attempt is not None and self.clear_attempt.clear_pub_seqs:
            names = sorted(self.clear_attempt.clear_pub_seqs)
            name = names[self.clear_attempt.turn % len(names)]
            self.clear_attempt.turn += 1
            return [(False, self.name, "clear attempt", self.pub_seq, name,
                     self.clear_attempt.clear_pub_seqs[name])]
        return [(self.latched, self.name, self.cause if self.latched else "", self.pub_seq, "", 0)]

    def describe(self, now: float) -> str:
        heard = ", ".join(
            f"{n}={'?' if s.last_pub_seq is None else s.last_pub_seq}{'!' if s.engaged else ''}"
            f"{' silent' if self._silent(s, now) else ''}" for n, s in self.sources.items())
        state = f"LATCHED ({self.cause})" if self.latched else "released"
        if self.clear_attempt is not None:
            state += " [clear attempt]"
        return f"e-stop {state}; deadman {'on' if self.deadman else 'off'}; heard: {heard or '-'}"
