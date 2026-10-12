# seat

The seat gate and the seat reading: what decides whether a seat press may become a SeatCommand,
and whether a seat value the MCB reports is known (hazard fixes H8, H10 seat, H6 seat;
`docs/plans/hazard-seat-spec.md`, approved by the owner on 2026-10-11 with Q1-Q15 as
recommended).

Pure C++ and header-only: `constexpr` functions, no allocation, no LVGL, no IDF, no lock. It
builds for the host and the target. Safety-relevant (CS-SAF-01): the HMI's only seat request
path (`hmi_ui`'s `UiApp::seat_request`) asks it about every press.

The seat sign-off still waits for the MCB team's answers S1-S3 (does the MCB stop a seat move
when the HMI goes silent, can SeatCommand get a stop, does the MCB refuse seat moves unless
IDLE; the spec's §8). Until then the residual hazards of the spec's §1 stand as written.

## Interface

| Header | What |
| --- | --- |
| `seat/seat_reading.hpp` | `seat_reading(spec, value)`: one MibStatus seat field in, the raw value or `kValueUnknown` out. The only firmware caller of hmi_rtps_spec's `seat_raw` (SEAT-011 guards it) |
| `seat/seat_gate.hpp` | `SeatReason`, `SeatInputs`, `SeatAsk`, `SeatDecision`; `ready` (conditions 1-4, the menu row), `gate` (1-5), `step_target`, `preset_target`, `decide` (the gate, then the target); `kGateOrder`, the reason order, `static_assert`ed |

The caller reads the inputs live at the press, on the UI task: the link (`LinkPort::state`),
the MCB state (`mib_state`), the POST gate and stick health (acquire), the axis value.

## Requirements

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-SEAT-01 | `seat_reading(spec, v)` is known only when v is finite, \|v × scale\| < 2^23 and the rounded raw value is within [min − step, max + step]; then it returns that raw value. Otherwise `VALUE_UNKNOWN`. No input bit pattern reaches undefined behaviour | SEAT-001..004 |
| REQ-SEAT-02 | The gate returns ALLOW only when the link is CONNECTED, the MCB state is IDLE, the POST gate is PASS, stick health is not FAULT and is inside its enum, and the axis value is known; otherwise the first failing reason in the order LINK_DOWN, MCB_NOT_IDLE, POST_NOT_PASSED, STICK_FAULT, POSITION_UNKNOWN | SEAT-005, SEAT-009 |
| REQ-SEAT-03 | A step's target is clamp(value + dir × step, min, max). It is refused with AT_LIMIT unless the target is strictly beyond the value in the press' direction. An allowed step's target is at most one step from the value | SEAT-006 |
| REQ-SEAT-04 | A preset's target is clamp(preset, min, max); it needs the axis value known | SEAT-007 |
| REQ-SEAT-05 | Every allowed target, in whole units, is finite and within [min, max] / scale | SEAT-008 |
| REQ-SEAT-06 | The component is pure: no allocation, no LVGL, no IDF, no lock; the gate and the reading are `constexpr` | SEAT-009, SEAT-010 |

## Tests

`tests/host/seat` (L1-SEAT, `tests/manifest.d/seat.yaml`): SEAT-001..010, with ASan and UBSan
(a NaN or an infinity that reached undefined behaviour stops the run) and the host allocation
guard. Expected values are written from the spec, never from the code's output.
