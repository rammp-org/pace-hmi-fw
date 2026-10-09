"""Pure helpers the hazard bench steps grade with: a step's recorded trace in, numbers out.

No board, no sim, no sockets: every function here works on what a step recorded (the sim's
XYTwist samples, its event log, the STATE polls and the marks), so the graders that use them
are tested on hand-written traces (hazard_selftest.py, cases BENCH-0xx) built from the
specs' pass criteria.

One clock. Samples (`t`), event-log records (`mono`), marks and STATE polls are all
time.monotonic() seconds, which on Windows is one system-wide clock: the sim child stamps
what it receives, the runner stamps what it sends and what it reads back. A mark is the
sim's own stamp of a line the runner wrote just before an action (hazard_rig.Rig.mark).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable, Iterable

Sample = tuple  # (t, x, y, twist, buttons)


@dataclass
class Trace:
    """What one hazard step recorded."""
    samples: list[Sample] = field(default_factory=list)   # every XYTwist the sim received
    events: list[dict] = field(default_factory=list)      # the sim's event log, this step
    states: list[tuple[float, dict]] = field(default_factory=list)  # (t of the reply, STATE)
    marks: dict[str, float] = field(default_factory=dict)  # label -> t (the sim's stamp)
    sent: list[tuple[float, str]] = field(default_factory=list)  # (t, command) the runner sent
    serial: str = ""                                       # serial text, when captured
    cal: dict | None = None                                # STATE's cal at the step's start

    def mark(self, label: str) -> float:
        if label not in self.marks:
            raise KeyError(f"no mark {label!r} in the trace (marks: {sorted(self.marks)})")
        return self.marks[label]


# ---------------------------------------------------------------- XYTwist samples


def is_nonzero(s: Sample) -> bool:
    """Any axis not exactly 0 (a NaN counts as non-zero: it is not a literal 0)."""
    return any(v != 0 for v in (s[1], s[2], s[3]))


def is_exact_zero(s: Sample) -> bool:
    """x = y = twist = 0 (C1 REQ-STK-10: the held command is a literal 0)."""
    return not is_nonzero(s)


def window(samples: Iterable[Sample], t0: float, t1: float = math.inf) -> list[Sample]:
    """Samples with t0 <= t < t1."""
    return [s for s in samples if t0 <= s[0] < t1]


def nonzero_in(samples: Iterable[Sample], t0: float, t1: float = math.inf) -> list[Sample]:
    return [s for s in window(samples, t0, t1) if is_nonzero(s)]


def fraction(samples: list[Sample], pred: Callable[[Sample], bool]) -> float:
    return sum(1 for s in samples if pred(s)) / len(samples) if samples else 0.0


def first_sample(samples: Iterable[Sample], pred: Callable[[Sample], bool],
                 after: float) -> Sample | None:
    for s in samples:
        if s[0] >= after and pred(s):
            return s
    return None


def rate_hz(samples: Iterable[Sample], t0: float, t1: float) -> float:
    n = len(window(samples, t0, t1))
    return n / (t1 - t0) if t1 > t0 else 0.0


def y_positive(s: Sample) -> bool:
    return s[2] > 0


def last_before(samples: Iterable[Sample], t: float) -> Sample | None:
    out = None
    for s in samples:
        if s[0] < t:
            out = s
    return out


def silence_after(samples: list[Sample], t: float, gap_s: float) -> Sample | None:
    """The last sample before the first gap of at least gap_s that starts after t."""
    ordered = sorted(s for s in samples if s[0] >= t)
    for a, b in zip(ordered, ordered[1:]):
        if b[0] - a[0] >= gap_s:
            return a
    return ordered[-1] if ordered else None


# ---------------------------------------------------------------- the sim's event log


def events_in(events: Iterable[dict], t0: float, t1: float = math.inf,
              ev: str | None = None) -> list[dict]:
    return [e for e in events if t0 <= e.get("mono", -math.inf) < t1
            and (ev is None or e.get("ev") == ev)]


def drive_commands(events: Iterable[dict], t0: float, t1: float = math.inf,
                   request: str | None = None) -> list[dict]:
    return [e for e in events_in(events, t0, t1, "drive_command")
            if request is None or e.get("request") == request]


def seat_commands(events: Iterable[dict], t0: float, t1: float = math.inf) -> list[dict]:
    return events_in(events, t0, t1, "seat_command")


def first_event(events: Iterable[dict], pred: Callable[[dict], bool],
                after: float) -> dict | None:
    for e in events:
        if e.get("mono", -math.inf) >= after and pred(e):
            return e
    return None


def gaps(times: list[float]) -> list[float]:
    return [round(b - a, 3) for a, b in zip(times, times[1:])]


def publishes(events: Iterable[dict], t0: float = -math.inf, t1: float = math.inf,
              state: str | None = None) -> list[dict]:
    """The sim's MibStatus publishes (mcb_sim_logic.mib_publish_event) that reached the HMI."""
    return [e for e in events_in(events, t0, t1, "mib_publish")
            if e.get("targets", 0) > 0 and (state is None or e.get("state") == state)]


# ---------------------------------------------------------------- STATE polls


def states_in(states: Iterable[tuple[float, dict]], t0: float,
              t1: float = math.inf) -> list[tuple[float, dict]]:
    return [(t, s) for t, s in states if t0 <= t < t1]


def first_state(states: Iterable[tuple[float, dict]], pred: Callable[[dict], bool],
                after: float, before: float = math.inf) -> tuple[float, dict] | None:
    for t, s in states:
        if after <= t < before and pred(s):
            return t, s
    return None


def field_is(key: str, value: object) -> Callable[[dict], bool]:
    return lambda s: s.get(key) == value


def screen_is(name: str) -> Callable[[dict], bool]:
    return field_is("screen", name)


def all_states(states: Iterable[tuple[float, dict]], pred: Callable[[dict], bool],
               t0: float, t1: float) -> tuple[bool, int, list]:
    """(every poll in [t0, t1) satisfies pred and there is at least one, how many, the
    first that does not as [t, state])."""
    polls = states_in(states, t0, t1)
    bad = [[round(t, 3), s] for t, s in polls if not pred(s)]
    return bool(polls) and not bad, len(polls), bad[:1]


def stick_field(key: str) -> Callable[[dict], object]:
    return lambda s: (s.get("stick") or {}).get(key)


# ---------------------------------------------------------------- the calibration


AXES = ("h", "v", "twist")


@dataclass(frozen=True)
class Cal:
    """STATE's cal: per axis (min, centre, max) mV."""
    h: tuple[int, int, int]
    v: tuple[int, int, int]
    twist: tuple[int, int, int]

    @staticmethod
    def from_state(state: dict) -> "Cal | None":
        cal = state.get("cal")
        if not isinstance(cal, dict) or not all(isinstance(cal.get(a), list)
                                                and len(cal[a]) == 3 for a in AXES):
            return None
        return Cal(*(tuple(int(round(x)) for x in cal[a]) for a in AXES))

    def centre(self) -> tuple[int, int, int]:
        return self.h[1], self.v[1], self.twist[1]

    def forward(self) -> tuple[int, int, int]:
        """Full forward: the vertical pot at its calibrated min (it reads LOWER pushed
        forward: joystick_cal kDirections, C2 §9 "Forward"), the others centred."""
        return self.h[1], self.v[0], self.twist[1]

    def vertical_at(self, mv: int) -> tuple[int, int, int]:
        return self.h[1], mv, self.twist[1]

    def horizontal_at(self, mv: int) -> tuple[int, int, int]:
        return mv, self.v[1], self.twist[1]


def clamp_mv(mv: float) -> int:
    """STICK takes 0..3300 mV."""
    return max(0, min(3300, int(round(mv))))
