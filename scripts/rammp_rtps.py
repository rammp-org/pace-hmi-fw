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

import os
import re
import struct
from typing import Dict

HEADER_RELATIVE_PATH = os.path.join("main", "rammp_rtps_spec.h")

_DEFINE_RE = re.compile(r'^\s*#define\s+RAMMP_((?:TOPIC|TYPE)_[A-Z0-9_]+)\s+"([^"]*)"', re.M)
_ENUM_RE = re.compile(r"^\s*RAMMP_([A-Z0-9_]+)\s*=\s*(\d+)\s*,", re.M)
# Accepts decimal and hex, with the C integer suffixes: bitmasks in a wire
# spec are naturally written 0x...u, and those must scrape like any other.
_NUMBER_RE = re.compile(
    r"^\s*#define\s+RAMMP_([A-Z0-9_]+)\s+(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*$", re.M
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

#: 4-byte CDR encapsulation header: little-endian classic CDR (xcdr1)
CDR_LE_HEADER = b"\x00\x01\x00\x00"


#: struct format for the payload behind the encapsulation header, matching
#: rammp_mcb_status_encode() in the spec header
_MCB_STATUS_FORMAT = (
    f"<BBBBB{MCB_TEXT_LEN}s{MCB_TEXT_LEN}s{ERROR_TEXT_LEN}s{ERROR_FOOTER_LEN}s"
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
                    error_text: str = "", error_footer: str = "") -> bytes:
    """Serialize a rammp_mcb_status_t, matching rammp_mcb_status_encode()."""
    return CDR_LE_HEADER + struct.pack(
        _MCB_STATUS_FORMAT,
        drive_status & 0xFF, system_state & 0xFF, flags & 0xFF, seq & 0xFF,
        max(0, min(int(speed_tenths), SPEED_MAX_TENTHS)),
        encode_label(drive_text), encode_label(state_text),
        encode_label(error_text, ERROR_TEXT_LEN), encode_label(error_footer, ERROR_FOOTER_LEN),
    )


def unpack_mcb_status(payload: bytes):
    """(drive, state, flags, seq, speed_tenths, drive_text, state_text,
    error_text, error_footer), or None if this isn't one."""
    if len(payload) < _MCB_STATUS_CDR_SIZE or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    drive, state, flags, seq, speed, drive_raw, state_raw, err_raw, foot_raw = struct.unpack_from(
        _MCB_STATUS_FORMAT, payload, len(CDR_LE_HEADER)
    )
    return (drive, state, flags, seq, speed,
            _decode_field(drive_raw), _decode_field(state_raw),
            _decode_field(err_raw), _decode_field(foot_raw))


def unpack_adc_xy_twist(payload: bytes) -> tuple[int, int, int, int, int] | None:
    """(x_mv, y_mv, twist_mv, buttons, drive_mode) from the joystick sample."""
    if len(payload) < 24 or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    return struct.unpack_from("<IIIII", payload, 4)


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
