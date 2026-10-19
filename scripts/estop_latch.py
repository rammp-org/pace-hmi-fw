#!/usr/bin/env python3
"""The MIB's side of the robot-wide e-stop: the latch, the deadman and the clear.

Pure logic with no I/O, so the bench simulator (rtps_mcb_sim.py) runs it and
test/estop_latch_test.py drives it through every race in docs/plans/estop.md
section 4. It is also the reference for the MIB itself (RAMMP-mib-fw): the
same rules, in the same order.

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
    seq: int | None = None   # newest seq seen; None = forgotten (new, or silent too long)
    engaged: bool = False    # what that newest message said
    last_rx: float | None = None


@dataclass
class Clearing:
    """One clear attempt: ack_seq per source, fixed when it was decided."""
    acks: dict[str, int]
    started: float
    turn: int = 0            # round-robin over acks, one clear message per period


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
    clearing: Clearing | None = None
    seq: int = 0
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
        """One received EStop, at receive time `now`."""
        if msg.source == self.name:
            return  # our own, looped back
        src = self.sources.setdefault(msg.source, Source())
        if self._silent(src, now):
            src.seq = None  # silent longer than the timeout: a rebooted node restarts at 0
        src.last_rx = now
        # Engaging is never age- or seq-checked: any true latches.
        if msg.engaged:
            self._latch(f"{msg.source}: {msg.reason}" if msg.reason else msg.source)
        # The source's state moves only on a newer message, so a reordered or
        # retransmitted older one cannot stand in for its newest word.
        if src.seq is None or spec.estop_seq_newer(msg.seq, src.seq):
            src.seq = msg.seq
            src.engaged = msg.engaged

    def request_clear(self, now: float) -> bool:
        """The operator asks to clear. Fixes one ack_seq per known source; False if
        there is nothing to clear, or a watched source has never been heard."""
        if not self.latched:
            return False
        acks = {name: src.seq for name, src in self.sources.items() if src.seq is not None}
        missing = [name for name in self.watched if name not in acks]
        if self.deadman and missing:
            self.events.append(f"clear refused: never heard from {', '.join(missing)}")
            return False
        self.clearing = Clearing(acks=acks, started=now)
        self.events.append("clearing: " + ", ".join(f"{n} ack {a}" for n, a in acks.items()))
        return True

    def engage(self, reason: str) -> None:
        """The MIB's own reason to stop (a bench button, a fault)."""
        self.clearing = None
        self._latch(f"{self.name}: {reason}")

    def tick(self, now: float) -> None:
        """Deadman and clear progress. Call at least every period."""
        if self.deadman:
            for name in self.watched:
                if self._silent(self.sources.get(name, Source()), now):
                    if self.clearing is not None:
                        self.events.append(f"clear dropped: {name} silent")
                        self.clearing = None
                    self._latch(f"{name} silent (deadman)")
        if self.clearing is None:
            return
        done = all(
            (src := self.sources.get(name)) is not None and src.seq is not None
            and not src.engaged and spec.estop_seq_newer(src.seq, ack)
            and not self._silent(src, now)
            for name, ack in self.clearing.acks.items())
        if done:
            self.events.append("RELEASED")
            self.latched = False
            self.cause = ""
            self.clearing = None
        elif now - self.clearing.started > self.clear_window:
            self.events.append("clear dropped: not taken up within the clear window")
            self.clearing = None

    def outgoing(self, _now: float) -> list[tuple]:
        """The message(s) to send this period, as pack_estop() arguments after `seq`:
        (engaged, source, reason, seq, ack_source, ack_seq)."""
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        if self.clearing is not None and self.clearing.acks:
            names = sorted(self.clearing.acks)
            name = names[self.clearing.turn % len(names)]
            self.clearing.turn += 1
            return [(False, self.name, "clear", self.seq, name, self.clearing.acks[name])]
        return [(self.latched, self.name, self.cause if self.latched else "", self.seq, "", 0)]

    def describe(self, now: float) -> str:
        heard = ", ".join(
            f"{n}={'?' if s.seq is None else s.seq}{'!' if s.engaged else ''}"
            f"{' silent' if self._silent(s, now) else ''}" for n, s in self.sources.items())
        state = f"LATCHED ({self.cause})" if self.latched else "clear"
        if self.clearing is not None:
            state += " [clearing]"
        return f"e-stop {state}; deadman {'on' if self.deadman else 'off'}; heard: {heard or '-'}"
