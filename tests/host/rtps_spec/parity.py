#!/usr/bin/env python3
"""The host codec's view of the RTPS spec, as C++ initialisers for the parity app
(TS-UNIT-04, TS-UNIT-07, CS-CFG-03).

scripts/rammp_rtps.py parses the C++ headers at import and keeps its own copy of every
message layout (MSG_* field lists) and of the seat rounding rule (SeatAxis.parse). This
script imports it (only it: WSL's python3 has no tkinter, so never the simulator) and
writes what it sees, so test_rtps_parity.cpp can compare it with the firmware's own C++:

  - the tables, enums, topic strings and numbers it scraped from the headers;
  - SeatAxis.parse / SeatAxis.format on fixed inputs;
  - message vectors: the values, the bytes rammp_rtps.encode() makes of them, and what
    rammp_rtps.decode() reads back from those bytes;
  - a hostile corpus per message (empty, truncated, relabelled headers, one byte forced
    to 0xFF at every payload position, seeded random bytes): whether rammp_rtps.decode()
    accepts each, and its re-encoding of what it read.

usage: parity.py --repo REPO --out FILE
"""

from __future__ import annotations

import argparse
import math
import os
import random
import struct
import sys

SEED = 20261006  # fixed: the corpus is the same on every run (TS-DET-02)
RANDOM_BODY_CASES = 48
RANDOM_FULL_CASES = 16


def load_spec(repo: str):
    sys.path.insert(0, os.path.join(repo, "scripts"))
    import rammp_rtps  # noqa: E402  (path setup must run first)

    return rammp_rtps


# ------------------------------------------------------------------ C++ literals

def c_bytes(data: bytes) -> str:
    """A std::string literal holding exactly these bytes (octal escapes: never greedy)."""
    body = "".join(ch if (ch.isascii() and ch.isalnum()) or ch == " " else f"\\{ord(ch):03o}"
                   for ch in data.decode("latin-1"))
    return f'std::string("{body}", {len(data)})'


def c_text(text: str) -> str:
    """A text the C++ side holds as const char * (tables): printable ASCII only."""
    if not all(" " <= ch <= "~" for ch in text):
        sys.exit(f"parity.py: non-printable text in a table: {text!r}")
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_str(text: str) -> str:
    """A decoded message string, as a std::string (it may hold anything)."""
    return c_bytes(text.encode("utf-8"))


def c_f32(value: float) -> str:
    return f"F(0x{struct.unpack('<I', struct.pack('<f', value))[0]:08x}u)"


def c_i32(value: int) -> str:
    return "INT32_MIN" if value == -(2 ** 31) else str(value)


def c_u8_list(data: bytes) -> str:
    return "{" + ",".join(f"0x{b:02x}" for b in data) + "}"


def f32(bits: int) -> float:
    """The float with these IEEE-754 bits (a Python float holds it exactly)."""
    return struct.unpack("<f", struct.pack("<I", bits))[0]


NAN = f32(0x7FC00000)
INF = math.inf


# ------------------------------------------------------------------ messages

def cpp_value(kind: str, v: tuple) -> str:
    """The C++ aggregate for one message, from rammp_rtps's tuple in field order."""
    if kind == "XYTwist":
        return (f"rammp::XYTwist{{{c_f32(v[0])}, {c_f32(v[1])}, {c_f32(v[2])}, "
                f"rammp::Buttons{{0x{v[3]:x}u}}}}")
    if kind == "DriveCommand":
        return f"rammp::DriveCommand{{rammp::DriveRequest{{{v[0]}}}, MIB::DriveProfile{{{v[1]}}}}}"
    if kind == "SeatCommand":
        return f"rammp::SeatCommand{{rammp::SeatAxis{{{v[0]}}}, {c_f32(v[1])}}}"
    if kind == "MibStatus":
        seat = ", ".join(c_f32(x) for x in v[2:6])
        return (f"MIB::MibStatus{{MIB::MibSystemState{{{v[0]}}}, MIB::DriveProfile{{{v[1]}}}, "
                f"MIB::seatState{{{seat}}}, {c_str(v[6])}, int64_t{{{v[7]}}}, {c_f32(v[8])}, "
                f"static_cast<int16_t>({v[9]}), static_cast<uint8_t>({v[10]}), {c_str(v[11])}, "
                f"{c_str(v[12])}}}")
    if kind == "Diagnostics":
        items = ", ".join("rammp::DiagItem{{" + ", ".join(c_i32(x) for x in item[0]) + "}}"
                          for item in v[1])
        return f"rammp::Diagnostics{{static_cast<uint8_t>({v[0]}), {{{items}}}}}"
    if kind == "UInt32":
        return f"rammp::UInt32{{0x{v[0]:x}u}}"
    if kind == "SelfTestReport":
        return (f"rammp::SelfTestReport{{{v[0]}, rammp::SelfTestKind{{{v[1]}}}, {v[2]}, {v[3]}, "
                f"rammp::SelfTestResult{{{v[4]}}}, {c_i32(v[5])}, {c_i32(v[6])}, {c_i32(v[7])}, "
                f"{c_str(v[8])}, {c_str(v[9])}, {c_str(v[10])}}}")
    raise KeyError(kind)


def vectors(spec) -> dict[str, tuple[list, list[tuple[str, tuple]]]]:
    """{message: (rammp_rtps field list, [(name, values in field order)])}."""
    long_text = "E" * 100  # far above kErrorTextLen: the codec carries it whole
    return {
        "XYTwist": (spec.MSG_XY_TWIST, [
            ("rest", (0.0, 0.0, 0.0, 0)),
            ("full_right_back_button", (1.0, -1.0, 0.5, 1)),
            ("odd_values", (f32(0x3EAAAAAB), 0.1, -1.0, 0xFFFFFFFF)),
            ("nan_inf_negzero", (NAN, INF, -0.0, 0)),
        ]),
        "DriveCommand": (spec.MSG_DRIVE_COMMAND, [
            ("disable_low", (0, 0)),
            ("enable_high", (1, 2)),
            ("unknown_enums", (5, 7)),
        ]),
        "SeatCommand": (spec.MSG_SEAT_COMMAND, [
            ("fb_tilt_min", (0, -45.0)),
            ("lateral_12_5", (1, 12.5)),
            ("elevation_max", (2, 250.0)),
            ("translation_fraction", (3, 0.1)),
            ("unknown_axis", (200, 1.0)),
            ("nan_target", (0, NAN)),
            ("inf_target", (2, INF)),
            ("huge_target", (2, 1e30)),
        ]),
        "MibStatus": (spec.MSG_MIB_STATUS, [
            ("defaults", (0, 1, 0.0, 0.0, 0.0, 0.0, "No error", 0, 0.0, 0, 0, "", "")),
            ("error_full", (3, 2, -12.5, 7.5, 125.0, 25.0, "MOTOR FAULT", 1790000000, 1.25,
                            -300, 255, "FAULT", "Power-cycle")),
            ("long_texts", (1, 0, 0.0, 0.0, 0.0, 0.0, long_text, -1, 0.0, 840, 7, "S" * 40,
                            "F" * 40)),
            ("seat_nan_inf_huge", (1, 1, NAN, INF, 1e30, -1e30, "", 0, NAN, 0, 1, "", "")),
            ("unknown_enums", (9, 200, 0.0, 0.0, 0.0, 0.0, "", 0, 0.0, 0, 0, "", "")),
        ]),
        "Diagnostics": (spec.MSG_DIAGNOSTICS, [
            ("empty", (0, [])),
            ("three_items", (1, [([215, 123, -45],), ([216, 0, 900],), ([0, -1, 1],)])),
            ("ragged_and_limits", (255, [([],), ([-(2 ** 31), 2 ** 31 - 1, 0, 5, 6],)])),
        ]),
        "UInt32": (spec.MSG_UINT32, [
            ("zero", (0,)),
            ("run_tag", (0xC0000005,)),
            ("all_ones", (0xFFFFFFFF,)),
        ]),
        "SelfTestReport": (spec.MSG_SELFTEST_REPORT, [
            ("started", (3, 0, 0, 12, 0, 0, -(2 ** 31), 2 ** 31 - 1, "", "", "v1.2.3")),
            ("result_fail", (3, 1, 4, 12, 1, 51, 64, 9999, "mem.int_free", "KB",
                             "below limit")),
        ]),
    }


def corpus(spec, fields, base: bytes, rng: random.Random) -> list[tuple[str, bytes]]:
    """Hostile inputs built from one valid encoding (TS-UNIT-07)."""
    hdr = spec.CDR_LE_HEADER
    body = base[len(hdr):]
    cases: list[tuple[str, bytes]] = [
        ("empty", b""), ("hdr1", b"\x00"), ("hdr2", b"\x00\x01"), ("hdr3", b"\x00\x01\x00"),
        ("hdr_only", hdr),
        ("trailing_junk", base + b"\xaa" * 8),
        ("be_header", b"\x00\x00\x00\x00" + body),
        ("xcdr2_le_header", b"\x00\x07\x00\x00" + body),
        ("d_cdr2_le_header", b"\x00\x09\x00\x00" + body),
        ("pl_cdr_le_header", b"\x00\x03\x00\x00" + body),
        ("options_set", b"\x00\x01\xff\xff" + body),
    ]
    cases += [(f"prefix_{n}", base[:n]) for n in range(len(hdr) + 1, len(base))]
    for i in range(len(body)):
        mutated = bytearray(body)
        mutated[i] = 0xFF
        cases.append((f"ff_at_{i}", hdr + bytes(mutated)))
    for i in range(RANDOM_BODY_CASES):
        n = rng.randrange(0, 2 * len(body) + 8)
        cases.append((f"random_body_{i}", hdr + bytes(rng.randrange(256) for _ in range(n))))
    for i in range(RANDOM_FULL_CASES):
        n = rng.randrange(0, 2 * len(base) + 8)
        cases.append((f"random_full_{i}", bytes(rng.randrange(256) for _ in range(n))))
    return cases


# ------------------------------------------------------------------ emit

def emit(spec, out: str) -> None:
    lines = ["// generated by tests/host/rtps_spec/parity.py from scripts/rammp_rtps.py; "
             "do not edit", "namespace py {"]

    lines.append("inline constexpr PySeatAxis kSeatAxes[] = {")
    for a in spec.SEAT_AXES:
        lines.append(f"  {{{a.id}, {c_text(a.name)}, {c_text(a.short)}, {c_text(a.label)}, "
                     f"{a.min_value}, {a.max_value}, {a.step}, {a.decimals}, {c_text(a.unit)}}},")
    lines.append("};")

    lines.append("inline constexpr PyDiagItem kDiagItems[] = {")
    for d in spec.DIAGNOSTICS:
        units = ", ".join(c_text(u) for u in d.units)
        decs = ", ".join(str(x) for x in d.decimals)
        lines.append(f"  {{{d.id}, {c_text(d.name)}, {c_text(d.short)}, {c_text(d.label)}, "
                     f"{{{units}}}, {{{decs}}}}},")
    lines.append("};")

    for array, table in (("kEnums", spec.ENUMS), ("kNumbers", spec.NUMBERS)):
        lines.append(f"inline constexpr PyNamed {array}[] = {{")
        lines += [f"  {{{c_text(k)}, {v}LL}}," for k, v in sorted(table.items())]
        lines.append("};")
    lines.append("inline constexpr PyText kStrings[] = {")
    lines += [f"  {{{c_text(k)}, {c_text(v)}}}," for k, v in sorted(spec.STRINGS.items())]
    lines.append("};")
    lines.append(f"inline constexpr double kMphPerMps = {spec.MPH_PER_MPS!r};")

    # the seat rounding rule's host copy: SeatAxis.parse (text -> raw) and .format
    parse_inputs = ["0", "12.5", "12.25", "-12.25", "12.35", "0.05", "-0.05", "0.15", "1e6",
                    "-1e6", "90.04", "90.06", "-45.05"]
    lines.append("inline constexpr PySeatParse kSeatParse[] = {")
    for a in spec.SEAT_AXES:
        for text in parse_inputs:
            lines.append(f"  {{{a.id}, {c_text(text)}, {a.parse(text)}}},")
    lines.append("};")
    lines.append("inline constexpr PySeatFormat kSeatFormat[] = {")
    for a in spec.SEAT_AXES:
        for raw in (a.min_value, a.max_value, 0, 1, -1, 5, -5, 125, a.step):
            lines.append(f"  {{{a.id}, {raw}, {c_text(a.format(raw))}}},")
    lines.append("};")

    rng = random.Random(SEED)
    hostile: list[str] = []
    for kind, (fields, cases) in vectors(spec).items():
        lines.append(f"inline const Vec<{cpp_kind(kind)}> k{kind}[] = {{")
        for name, values in cases:
            data = spec.encode(fields, *values)
            back = spec.decode(fields, data)
            decoded = cpp_value(kind, back) if back is not None else f"{cpp_kind(kind)}{{}}"
            lines.append(f"  {{{c_text(name)}, {cpp_value(kind, values)}, {c_u8_list(data)}, "
                         f"{'true' if back is not None else 'false'}, {decoded}}},")
        lines.append("};")
        base = spec.encode(fields, *cases[1][1] if len(cases) > 1 else cases[0][1])
        for name, data in corpus(spec, fields, base, rng):
            back = spec.decode(fields, data)
            reenc = spec.encode(fields, *back) if back is not None else b""
            hostile.append(f"  {{Msg::{kind}, {c_text(name)}, {c_u8_list(data)}, "
                           f"{'true' if back is not None else 'false'}, {c_u8_list(reenc)}}},")
    lines.append("inline const Hostile kCorpus[] = {")
    lines += hostile
    lines.append("};")
    lines.append("} // namespace py")

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def cpp_kind(kind: str) -> str:
    return {"MibStatus": "MIB::MibStatus"}.get(kind, f"rammp::{kind}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    emit(load_spec(args.repo), args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
