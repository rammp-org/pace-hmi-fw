#!/usr/bin/env python3
"""Python view of the RAMMP RTPS wire spec, read from the C++ header.

``messages/joystick_message.hpp`` (the shared rammp-rtps spec, a git submodule) holds
the topics, type names, enums and tables every RAMMP device shares, and
``main/hmi_rtps_spec.hpp`` what only this HMI adds (timing, limits, bench topics).
This module parses both headers at import time and mirrors its message structs,
encoded the way espp/cdr encodes them (XCDR1).

    import rammp_rtps as spec
    spec.TOPIC_MCB_STATUS        # 'rammp/mcb/status'
    spec.DRIVE_STATUS_ACTIVE     # 1
    spec.pack_mcb_status(spec.DRIVE_STATUS_ACTIVE, spec.SYSTEM_STATE_OK)

C++ names become UPPER_SNAKE: ``Topic<McbStatus> kMcbStatus`` gives TOPIC_MCB_STATUS
and TYPE_MCB_STATUS, ``DriveStatus::ACTIVE`` gives DRIVE_STATUS_ACTIVE, and
``milliseconds kMcbStatusPeriod{500}`` gives MCB_STATUS_PERIOD_MS.
"""

from __future__ import annotations

import datetime
import os
import re
import struct
from typing import Dict, List, NamedTuple

HEADER_RELATIVE_PATH = os.path.join("main", "hmi_rtps_spec.hpp")
SHARED_HEADER_RELATIVE_PATH = os.path.join(
    "external", "rammp-rtps", "components", "rammp_rtps_messages", "include", "messages",
    "joystick_message.hpp")

# inline constexpr Topic<McbStatus> kMcbStatus{"rammp/mcb/status", "rammp/msg/McbStatus"};
_TOPIC_RE = re.compile(r'Topic<(\w+)>\s+k(\w+)\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}')
# enum class DriveStatus : uint8_t { INACTIVE = 0, ... };
_ENUM_RE = re.compile(r"enum class (\w+)\s*:\s*\w+\s*\{(.*?)\};", re.S)
_MEMBER_RE = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*,", re.M)
# inline constexpr milliseconds kMcbStatusPeriod{500};  inline constexpr size_t kMcbTextLen = 16;
_NUMBER_RE = re.compile(
    r"^inline constexpr ([\w:]+) k(\w+)\s*(?:=\s*|\{)(0[xX][0-9a-fA-F]+|\d+)\}?;", re.M
)


def _snake(name: str) -> str:
    """McbStatus -> MCB_STATUS, XYTwist -> XY_TWIST, SelfTestKind -> SELFTEST_KIND."""
    name = name.replace("SelfTest", "Selftest").replace("UInt", "Uint")
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name).upper()


def find_header(relative: str = HEADER_RELATIVE_PATH) -> str:
    """Locate a spec header by walking up from this file to the repo root."""
    directory = os.path.dirname(os.path.abspath(__file__))
    while True:
        candidate = os.path.join(directory, relative)
        if os.path.isfile(candidate):
            return candidate
        parent = os.path.dirname(directory)
        if parent == directory:
            raise FileNotFoundError(
                f"could not find {relative} above {os.path.dirname(__file__)}"
                " (for the shared spec: git submodule update --init)"
            )
        directory = parent


#: this HMI's additions (timing, display limits, bench topics), beside selftest_spec.h
HEADER_PATH = find_header()
#: the shared messages, topics and tables (the rammp-rtps submodule)
SHARED_HEADER_PATH = find_header(SHARED_HEADER_RELATIVE_PATH)


def _read_spec(path: str) -> str:
    with open(path, encoding="utf-8") as spec_file:
        return spec_file.read().split("#if 0", 1)[0]  # the legacy codecs are #if 0'd out


_HEADER_TEXT = _read_spec(SHARED_HEADER_PATH) + "\n" + _read_spec(HEADER_PATH)

#: TOPIC_* (topic names) and TYPE_* (DDS type names)
STRINGS: Dict[str, str] = {}
for _message, _name, _topic, _type in _TOPIC_RE.findall(_HEADER_TEXT):
    STRINGS["TOPIC_" + _snake(_name)] = _topic
    STRINGS["TYPE_" + _snake(_message)] = _type
#: every enumerator, as ENUM_MEMBER (DRIVE_STATUS_ACTIVE)
ENUMS: Dict[str, int] = {
    f"{_snake(enum)}_{member}": int(value, 0)
    for enum, body in _ENUM_RE.findall(_HEADER_TEXT)
    for member, value in _MEMBER_RE.findall(body)
}
#: every numeric constant (timing gets a _MS suffix)
NUMBERS: Dict[str, int] = {
    _snake(name) + ("_MS" if kind == "milliseconds" else ""): int(value, 0)
    for kind, name, value in _NUMBER_RE.findall(_HEADER_TEXT)
}

if not STRINGS or not ENUMS or not NUMBERS:
    raise RuntimeError(f"{HEADER_PATH} parsed to nothing — has its C++ style changed?")

globals().update(STRINGS)
globals().update(ENUMS)
globals().update(NUMBERS)


def _group(prefix: str) -> Dict[int, str]:
    """{value: SHORT_NAME} for one enum group, e.g. _group('DRIVE_STATUS_')."""
    return {
        value: name[len(prefix):] for name, value in ENUMS.items() if name.startswith(prefix)
    }


#: {0: 'INACTIVE', 1: 'ACTIVE'} — DriveStatus
DRIVE_STATUS_NAMES = _group("DRIVE_STATUS_")
#: {0: 'OK', 1: 'ERROR'} — SystemState
STATE_NAMES = _group("SYSTEM_STATE_")
#: {0: 'NORMAL', 1: 'HOLO', 2: 'AUTO'} — the HMI's drive-mode buttons
DRIVE_MODE_NAMES = _group("DRIVE_MODE_")
#: {0: 'OK', 1: 'AT_MIN', ...} — the MCB's verdict on an actuator request
ACTUATOR_RESULT_NAMES = _group("ACTUATOR_RESULT_")


class Actuator(NamedTuple):
    """One row of RAMMP_ACTUATOR_TABLE: raw integer values, `decimals` for display."""

    id: int
    name: str
    short: str
    label: str
    min_value: int
    max_value: int
    step: int
    decimals: int
    unit: str

    def format(self, raw: int) -> str:
        """Raw units as the HMI's row draws them, e.g. 126 -> '12.6'."""
        return f"{raw / (10 ** self.decimals):.{self.decimals}f}"

    def parse(self, text: str) -> int:
        """Inverse of format(): '12.6' -> 126, rounded and clamped to range."""
        raw = int(round(float(text) * (10 ** self.decimals)))
        return max(self.min_value, min(self.max_value, raw))


# X(0, ELEVATION, "M1", "Elevation", 0, 2500, 50, 1, "mm")
_ACTUATOR_RE = re.compile(
    r"""^\s*X\(\s*(\d+)\s*,\s*([A-Z0-9_]+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,"""
    r"""\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*\)""",
    re.M,
)

#: every actuator in the header's X-macro table, in wire-id order
ACTUATORS: List[Actuator] = [
    Actuator(int(i), name, short, label, int(lo), int(hi), int(step), int(dec), unit)
    for i, name, short, label, lo, hi, step, dec, unit in _ACTUATOR_RE.findall(_HEADER_TEXT)
]

if not ACTUATORS:
    raise RuntimeError(f"{SHARED_HEADER_PATH}: RAMMP_ACTUATOR_TABLE parsed to nothing")
if [a.id for a in ACTUATORS] != list(range(len(ACTUATORS))):
    raise RuntimeError(f"{SHARED_HEADER_PATH}: actuator ids must be 0..N-1 in table order")


class DiagItem(NamedTuple):
    """One row of RAMMP_DIAG_TABLE: up to three readings, each with its unit."""

    id: int
    name: str
    short: str
    label: str
    units: tuple  # three unit labels; "" = reading unused
    decimals: tuple  # three decimal counts, display only

    def format(self, raw: int, field: int) -> str:
        """A raw reading as the HMI draws it, e.g. 2345 with 2 decimals -> '23.45'."""
        places = self.decimals[field]
        return f"{raw / (10 ** places):.{places}f}"


# D(0, TEST_1, "T1", "Test actuator 1", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)
_DIAG_RE = re.compile(
    r"""^\s*D\(\s*(\d+)\s*,\s*([A-Z0-9_]+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,"""
    r"""\s*"([^"]*)"\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*,\s*(\d+)\s*\)""",
    re.M,
)

#: every item in the header's diagnostics table, in wire order
DIAGNOSTICS: List[DiagItem] = [
    DiagItem(int(i), name, short, label, (u1, u2, u3), (int(d1), int(d2), int(d3)))
    for i, name, short, label, u1, d1, u2, d2, u3, d3 in _DIAG_RE.findall(_HEADER_TEXT)
]

if not DIAGNOSTICS:
    raise RuntimeError(f"{SHARED_HEADER_PATH}: RAMMP_DIAG_TABLE parsed to nothing")
if [d.id for d in DIAGNOSTICS] != list(range(len(DIAGNOSTICS))):
    raise RuntimeError(f"{SHARED_HEADER_PATH}: diagnostics ids must be 0..N-1 in table order")

# ------------------------------------------------------------------ CDR (XCDR1)
# What espp/cdr puts on the wire for the spec's C++ structs: header 00 01 00 00,
# little-endian, each primitive aligned to its size from the first payload byte,
# string = uint32 length (NUL included) + bytes + NUL, sequence = uint32 count + items.

#: 4-byte CDR encapsulation header: little-endian classic CDR (xcdr1)
CDR_LE_HEADER = b"\x00\x01\x00\x00"

# The C++ structs, field for field. A type is a struct format char, "str",
# ("seq", element type) or a nested field list (a struct).
MSG_MCB_STATUS = [
    ("drive_status", "B"), ("system_state", "B"), ("flags", "B"), ("seq", "B"),
    ("speed_tenths", "B"), ("hour", "B"), ("minute", "B"), ("second", "B"),
    ("day", "B"), ("month", "B"), ("year", "B"),
    ("drive_text", "str"), ("state_text", "str"), ("error_text", "str"), ("error_footer", "str"),
]
MSG_XY_TWIST = [("x", "f"), ("y", "f"), ("twist", "f"), ("buttons", "I"), ("drive_mode", "I")]
MSG_ACTUATOR_COMMAND = [("req_id", "B"), ("actuator_id", "B"), ("steps", "b")]
MSG_ACTUATOR_STATE = [("req_id", "B"), ("result", "B"), ("seq", "B"), ("values", ("seq", "i"))]
MSG_DIAGNOSTICS = [("seq", "B"), ("items", ("seq", [("values", ("seq", "i"))]))]
MSG_UINT32 = [("data", "I")]
MSG_SELFTEST_REPORT = [
    ("run_id", "B"), ("kind", "B"), ("index", "B"), ("count", "B"), ("result", "B"),
    ("value", "i"), ("lo", "i"), ("hi", "i"), ("name", "str"), ("unit", "str"), ("detail", "str"),
]


def _write(out: bytearray, kind, value) -> None:
    if isinstance(kind, list):  # a struct: value is a tuple in field order
        for (_name, field_kind), field_value in zip(kind, value):
            _write(out, field_kind, field_value)
    elif isinstance(kind, tuple):  # ("seq", element)
        _write(out, "I", len(value))
        for element in value:
            _write(out, kind[1], element)
    elif kind == "str":
        data = value.encode("ascii", "ignore")
        _write(out, "I", len(data) + 1)
        out.extend(data + b"\0")
    else:
        size = struct.calcsize(kind)
        out.extend(b"\0" * (-len(out) % size))
        out.extend(struct.pack("<" + kind, value))


def _read(buf: bytes, pos: int, kind):
    """(value, position after it)."""
    if isinstance(kind, list):
        values = []
        for _name, field_kind in kind:
            value, pos = _read(buf, pos, field_kind)
            values.append(value)
        return tuple(values), pos
    if isinstance(kind, tuple):
        count, pos = _read(buf, pos, "I")
        if count > len(buf) - pos:
            raise ValueError("sequence runs past the end")
        items = []
        for _ in range(count):
            item, pos = _read(buf, pos, kind[1])
            items.append(item)
        return items, pos
    if kind == "str":
        length, pos = _read(buf, pos, "I")
        if pos + length > len(buf):
            raise ValueError("string runs past the end")
        return buf[pos:pos + length].split(b"\0", 1)[0].decode("ascii", "replace"), pos + length
    size = struct.calcsize(kind)
    pos += -pos % size
    return struct.unpack_from("<" + kind, buf, pos)[0], pos + size


def encode(fields, *values) -> bytes:
    """One message: CDR_LE_HEADER + its XCDR1 payload (values in field order)."""
    out = bytearray()
    _write(out, fields, values)
    return CDR_LE_HEADER + bytes(out)


def decode(fields, payload: bytes):
    """The message's values as a tuple in field order, or None if it does not decode."""
    if len(payload) < len(CDR_LE_HEADER) or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    try:
        return _read(payload[len(CDR_LE_HEADER):], 0, fields)[0]
    except (struct.error, ValueError):
        return None


def _text(text: str, limit: int) -> str:
    """ASCII only (the HMI's fonts have nothing else), cut to what the HMI shows."""
    return text.encode("ascii", "ignore")[: limit - 1].decode("ascii")


def pack_mcb_status(drive_status: int, system_state: int, flags: int = 0, seq: int = 0,
                    speed_tenths: int = 0, drive_text: str = "", state_text: str = "",
                    error_text: str = "", error_footer: str = "",
                    clock: datetime.datetime | None = None) -> bytes:
    """A rammp::McbStatus. `clock` is the MCB's local time; None sends month 0, "unknown"."""
    when = (0,) * 6 if clock is None else (
        clock.hour, clock.minute, clock.second, clock.day, clock.month,
        max(0, min(clock.year - 2000, 255)))
    return encode(
        MSG_MCB_STATUS,
        drive_status & 0xFF, system_state & 0xFF, flags & 0xFF, seq & 0xFF,
        max(0, min(int(speed_tenths), SPEED_MAX_TENTHS)), *when,  # noqa: F821  (scraped)
        _text(drive_text, MCB_TEXT_LEN), _text(state_text, MCB_TEXT_LEN),  # noqa: F821
        _text(error_text, ERROR_TEXT_LEN), _text(error_footer, ERROR_FOOTER_LEN),  # noqa: F821
    )


def unpack_mcb_status(payload: bytes):
    """(drive, state, flags, seq, speed_tenths, hour, minute, second, day, month,
    year_since_2000, drive_text, state_text, error_text, error_footer), or None."""
    return decode(MSG_MCB_STATUS, payload)


def pack_actuator_command(req_id: int, actuator_id: int, steps: int) -> bytes:
    """A rammp::ActuatorCommand (HMI -> MCB)."""
    return encode(MSG_ACTUATOR_COMMAND, req_id & 0xFF, actuator_id & 0xFF,
                  max(-128, min(127, int(steps))))


def unpack_actuator_command(payload: bytes) -> tuple[int, int, int] | None:
    """(req_id, actuator_id, steps), or None."""
    return decode(MSG_ACTUATOR_COMMAND, payload)


def pack_actuator_state(values, req_id: int = 0, result: int = 0, seq: int = 0) -> bytes:
    """A rammp::ActuatorState (MCB -> HMI); `values` = raw value per table row."""
    return encode(MSG_ACTUATOR_STATE, req_id & 0xFF, result & 0xFF, seq & 0xFF,
                  [int(v) for v in values])


def unpack_actuator_state(payload: bytes) -> tuple[int, int, int, list[int]] | None:
    """(req_id, result, seq, values), or None."""
    return decode(MSG_ACTUATOR_STATE, payload)


def pack_diagnostics(values, seq: int = 0) -> bytes:
    """A rammp::Diagnostics (MCB -> HMI); `values` = one list of raw readings per item."""
    return encode(MSG_DIAGNOSTICS, seq & 0xFF, [([int(v) for v in row],) for row in values])


def unpack_diagnostics(payload: bytes) -> tuple[int, list[list[int]]] | None:
    """(seq, readings per item), or None."""
    message = decode(MSG_DIAGNOSTICS, payload)
    return None if message is None else (message[0], [item[0] for item in message[1]])


def unpack_xy_twist(payload: bytes) -> tuple[float, float, float, int, int] | None:
    """(x, y, twist, buttons, drive_mode): axes -1..+1, calibrated by the HMI."""
    return decode(MSG_XY_TWIST, payload)


# ------------------------------------------------------------------ self test

#: {0: 'PASS', 1: 'FAIL', 2: 'SKIP'}
SELFTEST_RESULT_NAMES = _group("SELFTEST_RESULT_")
#: what a report carries for "no limit on this side"
INT32_MIN = -(2 ** 31)
INT32_MAX = 2 ** 31 - 1


class SelfTestResult(NamedTuple):
    """One rammp::SelfTestReport: a check, or a run's start/summary."""

    run_id: int
    kind: int
    index: int
    count: int
    result: int
    value: int
    lo: int
    hi: int
    name: str
    unit: str
    detail: str


def pack_uint32(value: int) -> bytes:
    """A std_msgs/UInt32 sample, as the bench counter/command topics carry."""
    return encode(MSG_UINT32, value & 0xFFFFFFFF)


def unpack_uint32(payload: bytes) -> int | None:
    message = decode(MSG_UINT32, payload)
    return None if message is None else message[0]


# The self test's run request and ping/pong ride the bench UInt32 pair, tagged
# in the top nibble; "Self test" in the spec header says why.
def pack_selftest_run(run_id: int) -> bytes:
    """Ask for a self-test run (PC -> HMI on TOPIC_HMI_COMMAND)."""
    return pack_uint32(SELFTEST_TAG_RUN | (run_id & 0xFF))  # noqa: F821


def selftest_ping_seq(value: int) -> int | None:
    """The seq of a ping seen on TOPIC_HMI_COUNTER, or None for the heartbeat."""
    if value & SELFTEST_TAG_MASK != SELFTEST_TAG_PING:  # noqa: F821
        return None
    return value & 0xFFFF


def pack_selftest_pong(seq: int, peer_rx: int) -> bytes:
    """Answer a ping (PC -> HMI on TOPIC_HMI_COMMAND); peer_rx saturates at 4095."""
    return pack_uint32(SELFTEST_TAG_PONG | (min(peer_rx, 0xFFF) << 16) | (seq & 0xFFFF))  # noqa: F821


def pack_selftest_report(r: SelfTestResult) -> bytes:
    """A rammp::SelfTestReport (HMI -> PC); for tests of the tools."""
    return encode(MSG_SELFTEST_REPORT, *r)


def unpack_selftest_report(payload: bytes) -> SelfTestResult | None:
    message = decode(MSG_SELFTEST_REPORT, payload)
    return None if message is None else SelfTestResult(*message)


def _selftest_yes_no(r: SelfTestResult) -> bool:
    return r.lo == 1 and r.hi == 1 and not r.unit


def format_selftest_value(r: SelfTestResult, value: int | None = None) -> str:
    """A value as the HMI shows it: '182 KB', 'yes', '0.7%'. Mirrors selftest.cpp."""
    value = r.value if value is None else value
    if _selftest_yes_no(r):
        return "yes" if value else "no"
    if r.unit == "0.1%":
        return f"{value / 10:.1f}%"
    return f"{value} {r.unit}" if r.unit else str(value)


def format_selftest_limits(r: SelfTestResult) -> str:
    if r.lo == r.hi:
        return "yes" if _selftest_yes_no(r) else f"= {format_selftest_value(r, r.lo)}"
    if r.lo == INT32_MIN:
        return f"<= {format_selftest_value(r, r.hi)}"
    if r.hi == INT32_MAX:
        return f">= {format_selftest_value(r, r.lo)}"
    return f"{format_selftest_value(r, r.lo)} .. {format_selftest_value(r, r.hi)}"


def format_selftest_result(r: SelfTestResult, detail: str | None = None) -> str:
    """One row of the report table, in the same columns as the HMI's serial log."""
    measured = "-" if r.result == SELFTEST_RESULT_SKIP else format_selftest_value(r)  # noqa: F821
    return (f"{SELFTEST_RESULT_NAMES.get(r.result, '?'):<4}  {r.name:<18} {measured:>14}  "
            f"{format_selftest_limits(r):<20} {r.detail if detail is None else detail}")


if __name__ == "__main__":
    print(f"spec headers: {SHARED_HEADER_PATH}\n              {HEADER_PATH}\n")
    print("topics and types:")
    for key in sorted(STRINGS):
        print(f"  {key:<24} {STRINGS[key]}")
    print("\nenums:")
    for key in sorted(ENUMS, key=lambda k: (k.rsplit("_", 1)[0], ENUMS[k])):
        print(f"  {key:<24} {ENUMS[key]}")
    print("\nnumbers:")
    for key in sorted(NUMBERS):
        print(f"  {key:<24} {NUMBERS[key]}")
    print("\nactuators:")
    for actuator in ACTUATORS:
        print(
            f"  {actuator.id}  {actuator.short:<4} {actuator.label:<16} "
            f"{actuator.format(actuator.min_value):>7} .. "
            f"{actuator.format(actuator.max_value):<7} "
            f"step {actuator.format(actuator.step)} {actuator.unit}"
        )
