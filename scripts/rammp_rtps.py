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

if not STRINGS or not ENUMS:
    raise RuntimeError(f"{HEADER_PATH} parsed to nothing — has its #define/enum style changed?")

globals().update(STRINGS)
globals().update(ENUMS)


def _group(prefix: str) -> Dict[int, str]:
    """{value: SHORT_NAME} for one enum group, e.g. _group('DRIVE_STATUS_')."""
    return {
        value: name[len(prefix):] for name, value in ENUMS.items() if name.startswith(prefix)
    }


#: {0: 'INACTIVE', 1: 'ACTIVE'} — mirrors rammp_drive_status_name() in the header
DRIVE_STATUS_NAMES = _group("DRIVE_STATUS_")
#: {0: 'OK', 1: 'ERROR'} — mirrors rammp_state_name() in the header
STATE_NAMES = _group("STATE_")

#: 4-byte CDR encapsulation header: little-endian classic CDR (xcdr1)
CDR_LE_HEADER = b"\x00\x01\x00\x00"


def pack_mcb_status(drive_status: int, system_state: int, flags: int = 0, seq: int = 0) -> bytes:
    """Serialize a rammp_mcb_status_t the way espp/cdr will deserialize it."""
    return CDR_LE_HEADER + struct.pack(
        "<BBBB", drive_status & 0xFF, system_state & 0xFF, flags & 0xFF, seq & 0xFF
    )


def unpack_mcb_status(payload: bytes) -> tuple[int, int, int, int] | None:
    """(drive_status, system_state, flags, seq), or None if this isn't one."""
    if len(payload) < 8 or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    return struct.unpack_from("<BBBB", payload, 4)


def unpack_adc_xy_twist(payload: bytes) -> tuple[int, int, int] | None:
    """(x_mv, y_mv, twist_mv) from a rammp_adc_xy_twist_t sample."""
    if len(payload) < 16 or payload[:2] != CDR_LE_HEADER[:2]:
        return None
    return struct.unpack_from("<III", payload, 4)


if __name__ == "__main__":
    print(f"spec header: {HEADER_PATH}\n")
    print("topics and types:")
    for key in sorted(STRINGS):
        print(f"  {key:<24} {STRINGS[key]}")
    print("\nenums:")
    for key in sorted(ENUMS, key=lambda k: (k.rsplit("_", 1)[0], ENUMS[k])):
        print(f"  {key:<24} {ENUMS[key]}")
