# hmi_rtps_spec

What only this HMI adds to the shared RTPS spec (`external/rammp-rtps`, `messages/*.hpp`): the
names of the MIB's enums, the seat fields and their wire units, the timing it expects of the MIB,
its display limits, its bench-only self-test topics and the warning texts it raises itself.
Namespace `rammp` (the shared spec's). One header, `include/hmi_rtps_spec.hpp`, constexpr data
and helpers only: no state, no allocation, no logging.

Moved here from `main/hmi_rtps_spec.hpp` unchanged (2026-10-08), so the UI component (`hmi_ui`)
can word its banners from it instead of main filling them in. Its `#if 0` legacy C codecs were
deleted first (dead code, CS-LAY-10).

**Safety-relevant in part** (CS-SAF-01): `seat_units` scales the target every SeatCommand
carries, and the drive session's windows are held to `kMibStatusPeriod` and `kMibStatusTimeout`
(static_asserts in main). Functions at most 60 lines (`.clang-tidy`, CS-FIL-01).

## Interface

| Part | What |
| --- | --- |
| `to_string(MibSystemState)`, `to_string(DriveProfile)`, `index_of(SeatAxis)` | names for labels, banners and logs ("?" for a value this firmware does not know) |
| `seat_field`, `scale_of`, `seat_raw`, `seat_units` | a seat axis' MIB field, and the one conversion each way between wire units and the screens' raw integers |
| `kMibStatusPeriod`, `kMibStatusTimeout`, `kDiagPeriod`, `kDiagTimeout` | what this HMI expects of the MIB |
| `kMcbTextLen`, `kErrorTextLen`, `kErrorFooterLen`, `kMphPerMps`, `kSpeedMaxTenths` | display limits |
| `UInt32`, `SelfTestReport`, `SelfTestResult`, `SelfTestKind`, `SelfTestTag`, `kHmi*` topics, `tag_of`, `tagged` | the bench PC's topics (a production MCB ignores them) |
| `kHmi*Title`, `kHmi*Text`, `kHmi*Footer`, `kHmiMcbNoTextFmt` | the warnings the HMI raises itself, sized for the banner it shares with MCB faults |

`scripts/rammp_rtps.py` parses this header (and the shared ones) for the PC tools.

## Requirements and tests

No REQ IDs are written for it yet. The L1 app `tests/host/rtps_spec` (L1-RTPS, cases
RTPS-001..065) characterises every helper, the H8 hazard inputs of `seat_raw` (NaN and huge
values), the names and tags, and parity with the host codec.

## Tasks and dependencies

- Tasks: none. Any task may call it (pure functions and constants).
- Dependencies: `rammp_rtps_messages` (the shared spec). Required by `main` (`rtps_comms`, the
  self test, main.cpp).
- Diagrams: none of its own (D3 shows the component).
