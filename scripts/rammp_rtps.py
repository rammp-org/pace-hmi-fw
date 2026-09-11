#!/usr/bin/env python3
"""Python view of the RAMMP RTPS wire spec, scraped from the C header.

``main/rammp_rtps_spec.h`` is the single source of truth for topics, type names,
enum values and message layouts shared by the joystick HMI and the Main Control
Board. Rather than keeping a hand-written copy here (which drifts the first time
someone edits only one side), this module parses the header at import time.

    import rammp_rtps as spec
    spec.TOPIC_MCB_STATUS        # 'rammp/mcb/status'
    spec.DRIVE_STATUS_ACTIVE     # 1
    spec.pack_mcb_status(spec.DRIVE_STATUS_ACTIVE, spec.STATE_OK)

Names lose the ``RAMMP_`` prefix on the way in, so ``RAMMP_TOPIC_MCB_STATUS``
becomes ``TOPIC_MCB_STATUS``. The parser matches the two shapes the header
documents — ``#define RAMMP_TOPIC_*``/``RAMMP_TYPE_*`` string defines and
``RAMMP_<GROUP>_<NAME> = <int>,`` enumerators — so new entries written in that
style appear here for free.
"""

from __future__ import annotations

import datetime
import os
import re
import struct
from typing import Dict, List, NamedTuple

HEADER_RELATIVE_PATH = os.path.join("main", "rammp_rtps_spec.h")

_DEFINE_RE = re.compile(r'^\s*#define\s+RAMMP_((?:TOPIC|TYPE)_[A-Z0-9_]+)\s+"([^"]*)"', re.M)
_ENUM_RE = re.compile(r"^\s*RAMMP_([A-Z0-9_]+)\s*=\s*(\d+)\s*,", re.M)
# Accepts decimal and hex, with the C integer suffixes: bitmasks in a wire
# spec are naturally written 0x...u, and those must scrape like any other.
# A trailing /* comment */ or // comment is allowed.
_NUMBER_RE = re.compile(
    r"^\s*#define\s+RAMMP_([A-Z0-9_]+)\s+(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*(?:/\*.*?\*/|//.*)?\s*$",
    re.M,
)


def find_header() -> str:
    """Locate rammp_rtps_spec.h by walking up from this file to the repo root."""
    directory = os.path.dirname(os.path.abspath(__file__))
    while True:
        candidate = os.path.join(directory, HEADER_RELATIVE_PATH)
        if os.path.isfile(candidate):
            return candidate
        parent = os.path.dirname(directory)
        if parent == directory:
            raise FileNotFoundError(
                f"could not find {HEADER_RELATIVE_PATH} above {os.path.dirname(__file__)}"
            )
        directory = parent


HEADER_PATH = find_header()
with open(HEADER_PATH, encoding="utf-8") as _header_file:
    _HEADER_TEXT = _header_file.read()

#: every string #define in the header, keyed without the RAMMP_ prefix
STRINGS: Dict[str, str] = {name: value for name, value in _DEFINE_RE.findall(_HEADER_TEXT)}
#: every enumerator in the header, keyed without the RAMMP_ prefix
ENUMS: Dict[str, int] = {name: int(value) for name, value in _ENUM_RE.findall(_HEADER_TEXT)}
#: every numeric #define (the timing contract), keyed without the RAMMP_ prefix
NUMBERS: Dict[str, int] = {
    name: int(value, 0) for name, value in _NUMBER_RE.findall(_HEADER_TEXT)
}

if not STRINGS or not ENUMS:
    raise RuntimeError(f"{HEADER_PATH} parsed to nothing — has its #define/enum style changed?")

globals().update(STRINGS)
globals().update(ENUMS)
globals().update(NUMBERS)


def _group(prefix: str) -> Dict[int, str]:
    """{value: SHORT_NAME} for one enum group, e.g. _group('DRIVE_STATUS_')."""
    return {
        value: name[len(prefix):] for name, value in ENUMS.items() if name.startswith(prefix)
    }


#: {0: 'INACTIVE', 1: 'ACTIVE'} — mirrors rammp_drive_status_name() in the header
DRIVE_STATUS_NAMES = _group("DRIVE_STATUS_")
#: {0: 'OK', 1: 'ERROR'} — mirrors rammp_state_name() in the header
STATE_NAMES = _group("STATE_")
#: {0: 'NORMAL', 1: 'HOLO', 2: 'AUTO'} — the HMI's drive-mode buttons
DRIVE_MODE_NAMES = _group("DRIVE_MODE_")
#: {0: 'OK', 1: 'AT_MIN', ...} — the MCB's verdict on an actuator request
ACTUATOR_RESULT_NAMES = _group("ACTUATOR_RESULT_")


class Actuator(NamedTuple):
    """One row of RAMMP_ACTUATOR_TABLE in the spec header.

    Values are raw integers in the actuator's own units; `decimals` says where
    the display puts the point, so 2500 with decimals=1 reads as "250.0". The
    HMI and the MCB both index actuators by `id`, which is the row's position.
    """

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
    raise RuntimeError(f"{HEADER_PATH}: RAMMP_ACTUATOR_TABLE parsed to nothing")
if [a.id for a in ACTUATORS] != list(range(len(ACTUATORS))):
    raise RuntimeError(f"{HEADER_PATH}: actuator ids must be 0..N-1 in table order")
if len(ACTUATORS) > ACTUATOR_MAX:  # noqa: F821  (scraped into globals above)
    raise RuntimeError(f"{HEADER_PATH}: {len(ACTUATORS)} actuators exceeds RAMMP_ACTUATOR_MAX")

#: 4-byte CDR encapsulation header: little-endian classic CDR (xcdr1)
CDR_LE_HEADER = b"\x00\x01\x00\x00"


#: struct format for the payload behind the encapsulation header, matching
#: rammp_mcb_status_encode() in the spec header
_MCB_STATUS_FORMAT = (
    f"<BBBBB6B{MCB_TEXT_LEN}s{MCB_TEXT_LEN}s{ERROR_TEXT_LEN}s{ERROR_FOOTER_LEN}s"
)
_MCB_STATUS_CDR_SIZE = len(CDR_LE_HEADER) + struct.calcsize(_MCB_STATUS_FORMAT)


def _decode_field(raw: bytes) -> str:
    """Text up to the first NUL, as the C decoder reads it."""
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def encode_label(text: str, limit: int = MCB_TEXT_LEN) -> bytes:
    """ASCII-encode a text field, truncated to leave room for the NUL.

    The HMI draws these with LVGL's built-in Montserrat faces, which have no
    glyphs outside ASCII, so anything else is dropped rather than sent as bytes
    that would render blank.
    """
    return text.encode("ascii", "ignore")[: limit - 1]  # struct's 's' NUL-pads


def pack_mcb_status(drive_status: int, system_state: int, flags: int = 0, seq: int = 0,
                    speed_tenths: int = 0, drive_text: str = "", state_text: str = "",
                    error_text: str = "", error_footer: str = "",
                    clock: datetime.datetime | None = None) -> bytes:
    """Serialize a rammp_mcb_status_t, matching rammp_mcb_status_encode().

    `clock` is the MCB's local time; None sends month 0, "time unknown".
    """
    when = (0,) * 6 if clock is None else (
        clock.hour, clock.minute, clock.second, clock.day, clock.month,
        max(0, min(clock.year - 2000, 255)))
    return CDR_LE_HEADER + struct.pack(
        _MCB_STATUS_FORMAT,
        drive_status & 0xFF, system_state & 0xFF, flags & 0xFF, seq & 0xFF,
        max(0, min(int(speed_tenths), SPEED_MAX_TENTHS)), *when,
        encode_label(drive_text), encode_label(state_text),
        encode_label(error_text, ERROR_TEXT_LEN), encode_label(error_footer, ERROR_FOOTER_LEN),
    )


def unpack_mcb_status(payload: bytes):
    """(drive, state, flags, seq, speed_tenths, hour, minute, second, day, month,
    year_since_2000, drive_text, state_text, error_text, error_footer), or None
    if this isn't one."""
    if len(payload) < _MCB_STATUS_CDR_SIZE or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    fields = struct.unpack_from(_MCB_STATUS_FORMAT, payload, len(CDR_LE_HEADER))
    return (*fields[:11], *(_decode_field(raw) for raw in fields[11:]))


#: matches rammp_actuator_command_encode() in the spec header
_ACTUATOR_COMMAND_FORMAT = "<BBbB"
#: matches rammp_actuator_state_encode(); the int32 array is fixed length so a
#: state message is always the same size regardless of how many exist
_ACTUATOR_STATE_FORMAT = f"<BBBB{ACTUATOR_MAX}i"  # noqa: F821  (scraped)
_ACTUATOR_STATE_CDR_SIZE = len(CDR_LE_HEADER) + struct.calcsize(_ACTUATOR_STATE_FORMAT)
_ACTUATOR_COMMAND_CDR_SIZE = len(CDR_LE_HEADER) + struct.calcsize(_ACTUATOR_COMMAND_FORMAT)


def pack_actuator_command(req_id: int, actuator_id: int, steps: int) -> bytes:
    """Serialize a rammp_actuator_command_t (joystick -> MCB)."""
    return CDR_LE_HEADER + struct.pack(
        _ACTUATOR_COMMAND_FORMAT, req_id & 0xFF, actuator_id & 0xFF,
        max(-128, min(127, int(steps))), 0,
    )


def unpack_actuator_command(payload: bytes) -> tuple[int, int, int] | None:
    """(req_id, actuator_id, steps), or None if this isn't one."""
    if len(payload) < _ACTUATOR_COMMAND_CDR_SIZE or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    req_id, actuator_id, steps, _reserved = struct.unpack_from(
        _ACTUATOR_COMMAND_FORMAT, payload, len(CDR_LE_HEADER)
    )
    return (req_id, actuator_id, steps)


def pack_actuator_state(values, req_id: int = 0, result: int = 0, seq: int = 0) -> bytes:
    """Serialize a rammp_actuator_state_t (MCB -> joystick).

    `values` is the raw value per actuator in table order; it sets `count`, and
    the fixed-length wire array is zero-filled beyond it.
    """
    raw = list(values)[:ACTUATOR_MAX]  # noqa: F821  (scraped)
    padded = raw + [0] * (ACTUATOR_MAX - len(raw))  # noqa: F821
    return CDR_LE_HEADER + struct.pack(
        _ACTUATOR_STATE_FORMAT, req_id & 0xFF, result & 0xFF, len(raw), seq & 0xFF, *padded
    )


def unpack_actuator_state(payload: bytes) -> tuple[int, int, int, list[int]] | None:
    """(req_id, result, seq, values) with values trimmed to `count`."""
    if len(payload) < _ACTUATOR_STATE_CDR_SIZE or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    fields = struct.unpack_from(_ACTUATOR_STATE_FORMAT, payload, len(CDR_LE_HEADER))
    req_id, result, count, seq = fields[:4]
    count = min(count, ACTUATOR_MAX)  # noqa: F821
    return (req_id, result, seq, list(fields[4:4 + count]))


def unpack_adc_xy_twist(payload: bytes) -> tuple[float, float, float, int, int] | None:
    """(x, y, twist, buttons, drive_mode) from the joystick sample.

    Axes are -1..+1, calibrated by the HMI: +x right, +y forward, deadzones
    already applied (see RAMMP_TOPIC_JOYSTICK_ADC in the spec header).
    """
    if len(payload) < 24 or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    return struct.unpack_from("<fffII", payload, 4)


# ------------------------------------------------------------------ self test

#: {0: 'PASS', 1: 'FAIL', 2: 'SKIP'}
SELFTEST_RESULT_NAMES = _group("SELFTEST_RESULT_")
#: what a report carries for "no limit on this side"
INT32_MIN = -(2 ** 31)
INT32_MAX = 2 ** 31 - 1

#: matches rammp_selftest_report_encode(): five bytes, three of padding, three
#: int32 and the three fixed text fields
_SELFTEST_REPORT_FORMAT = (
    f"<BBBBB3xiii{SELFTEST_NAME_LEN}s{SELFTEST_UNIT_LEN}s{SELFTEST_DETAIL_LEN}s"  # noqa: F821
)
_SELFTEST_REPORT_CDR_SIZE = len(CDR_LE_HEADER) + struct.calcsize(_SELFTEST_REPORT_FORMAT)


class SelfTestResult(NamedTuple):
    """One rammp_selftest_report_t sample: a check, or a run's start/summary."""

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
    return CDR_LE_HEADER + struct.pack("<I", value & 0xFFFFFFFF)


def unpack_uint32(payload: bytes) -> int | None:
    if len(payload) < len(CDR_LE_HEADER) + 4 or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    return struct.unpack_from("<I", payload, len(CDR_LE_HEADER))[0]


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
    """Serialize a rammp_selftest_report_t (HMI -> PC); for tests of the tools."""
    return CDR_LE_HEADER + struct.pack(
        _SELFTEST_REPORT_FORMAT, r.run_id & 0xFF, r.kind & 0xFF, r.index & 0xFF, r.count & 0xFF,
        r.result & 0xFF, r.value, r.lo, r.hi,
        encode_label(r.name, SELFTEST_NAME_LEN),  # noqa: F821
        encode_label(r.unit, SELFTEST_UNIT_LEN),  # noqa: F821
        encode_label(r.detail, SELFTEST_DETAIL_LEN),  # noqa: F821
    )


def unpack_selftest_report(payload: bytes) -> SelfTestResult | None:
    if len(payload) < _SELFTEST_REPORT_CDR_SIZE or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    (run_id, kind, index, count, result, value, lo, hi, name, unit,
     detail) = struct.unpack_from(_SELFTEST_REPORT_FORMAT, payload, len(CDR_LE_HEADER))
    return SelfTestResult(run_id, kind, index, count, result, value, lo, hi,
                          _decode_field(name), _decode_field(unit), _decode_field(detail))


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
    print(f"spec header: {HEADER_PATH}\n")
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
