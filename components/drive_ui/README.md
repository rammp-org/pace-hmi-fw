# drive_ui

The drive UI on the UI task: the drive adapter's port (`DrivePort`), which sends the
DriveCommand and performs the drive session's relocks, the padlock and the stick gate it acts on
(`DriveUi`), and the stick button's edges (`StickButton`). Namespace `hmi::ui`, like the rest of
the UI code (profile, Namespace line).

**Safety-relevant** (CS-SAF-01): functions at most 60 lines (`.clang-tidy`, CS-FIL-01), no
transfer grants (ratchet V2), two approvals per change (CS-SAF-05). What it commands:

| Command | Where | Read by |
| --- | --- | --- |
| The DriveCommand (ENABLE, DISABLE, with the drive profile the user picked) | `DrivePort::publish` -> `DriveUi::publish_drive` -> main's `rtps_comms_publish_drive` (result ignored: H6) | the MCB |
| The stick gate: may the stick drive the chair now | `DriveUi::update_stick_gate` writes app_state's `stick_drives` from drive_session's `stick_drives` | the ADC task, which sends the MCB a centred stick whenever it is false |
| The stick button's raw level | `StickButton::edge` writes app_state's `joy_button_pressed` | the ADC task, which publishes it with the stick; the holds (unlock, drive exit) |
| The relocks the drive session decides (Locked screen, `locked`, the gate) | `DrivePort`'s actions | the UI |

Status: **draft, refactor only** (2026-10-08). Split out of `components/hmi_ui` with no
behaviour change, so the code that commands driving sits in a component reviewed as safety code
and the views around it do not. The owner approved the split (2026-10-08), including this
component's place on the ratchet's UI-path list (`lv_*` allowed: it runs on the UI task). The
hazard fixes change the DriveCommand path here (hazard-fixes.md C1: re-send, results checked);
DrivePort and DriveUi are the two files they touch.

## Files

| File | What |
| --- | --- |
| `include/drive_ui/drive_port.hpp` | `DrivePort<Ui>`, `drive_screen_of`, `drive_mib_of`: the drive adapter's port (components/drive_adapter `DrivePort`), each action's LVGL and RTPS call as it has always been (from `main/frag_drive.inc`, MainDriveView). A template over the drive UI, so its calls are direct (CS-SAF-08) and the drive goldens (tests/host/drive_golden) run it verbatim. Header-only; it needs `ui` and `esp_timer`, private here: an includer requires them too. Included by main, which makes the one DriveAdapter over UiApp's DriveUi, and by hmi_ui's `ui_app.cpp` (the seat refusal's banner) |
| `include/drive_ui/drive_ui.hpp`, `src/drive_ui.cpp` | `DriveUi`: the padlock (ring and shackle) and the one-second advance to Drive after an unlock, the stick gate's writer (`update_stick_gate`), the holds' "let go first" flags, and what DrivePort acts on (from `main/frag_lock.inc`, `frag_nav.inc` and app_main's lock wiring); `mcb_ready`, `seat_ready` (from `main/frag_refusal.inc`). hmi_ui's UiApp owns the one instance |
| `include/drive_ui/stick_button.hpp`, `src/stick_button.cpp` | `StickButton`: one edge of the stick button (GPIO48): the Joystick screen's pressed and count subjects, the level for the ADC task and the select key, in that order, on `stick`'s ButtonEdges (from `main/frag_stick_button.inc`). Runs on the button's task; main's caller holds lvgl_mutex. UiApp owns the one instance |
| `include/drive_ui/hold_range.hpp` | `HOLD_MAX`: the range a hold fills (hmi_ui's hold_gesture.hpp), which is also the padlock ring's |
| `include/drive_ui/link_state.hpp` | `LinkState`: the values the `rtps_link` subject carries, equal to main's `RtpsLinkState` (main static_asserts it) |
| `include/drive_ui/refused.hpp` | `Refused`, `DRIVE_REFUSED_SHOW_MS`, `EXIT_REFUSED_SHOW_MS`: which request a banner says was refused, and its dwell; no LVGL |
| `include/drive_ui/shared_subjects.hpp` | `SharedSubjects`: the subjects several views and DriveUi read (UiApp's `locked`, `mib_state`, `rtps_link`; app_state's `rtps_blink`). UiApp fills it |
| `include/drive_ui/fn.hpp` | `Fn<R(Args...)>`, `bind<&T::m>(obj)`: a non-owning callable (a plain function, or a member function bound to its object), constant-initialisable: how a Config calls another object without a global |

The last five are vocabulary hmi_ui shares with the drive UI. They live here because hmi_ui
requires drive_ui (UiApp owns a DriveUi and a StickButton) and drive_ui cannot require hmi_ui.

## Requirements

As-is: each restates what the moved code did before the move; none is new behaviour.

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-DUI-01 | Driven by the drive adapter, DrivePort makes the same LVGL, esp_timer and DriveCommand calls, in the same order and with the same arguments, as main.cpp's drive code before it moved (MainDriveView) | GLD-001, GLD-002 (GLD-003: the scenarios take all 41 rows) |
| REQ-DUI-02 | The DriveCommand is sent only by `DrivePort::publish`, with the request it is given and the drive profile the user picked, read at the call (`drive_profile_published`); the result is ignored (H6) | GLD-001, GLD-002 |
| REQ-DUI-03 | `update_stick_gate` stores drive_session's `stick_drives(locked, screen, menu open)` with each read at the call (REQ-DRV-19); it runs only at the GATE_TRIGGERS sites (the session's GATE_UPDATE, NavView's `gate_update`) | review; bench B5, B5a-c (XYTwist 0 while locked) |
| REQ-DUI-04 | One stick button edge sets the pressed subject, then stores the raw level, then, as ButtonEdges says at the time read now, latches the select key (a short press's release) and adds one to the count (a counted press) (H14) | review; bench B4, B5 |
| REQ-DUI-05 | `mcb_ready` is true only with the link CONNECTED and the MIB IDLE or ENABLED; `seat_ready` only with the link CONNECTED and the MIB IDLE | review; bench B5 (refused hold) |
| REQ-DUI-06 | The unlock arms one advance to Drive, UNLOCK_ADVANCE_MS later, which hands the session UNLOCK_TIMER; a re-lock before it fires deletes it | GLD-002 (the port's calls); bench B5 |

## Tasks

None. DrivePort and DriveUi run on the UI task (`lv_task`) with `lvgl_mutex` held, or in
app_main while it builds the UI before `lv_task` starts. `StickButton::edge` runs on the
button's interrupt task (espp's "Button") or the remote UI's, with `lvgl_mutex` held by main's
caller. No lock here (CS-OWN-08).

## Dependencies

Public: `drive_adapter` (the port's types), `drive_session` (the session's Screen, MibState,
Input and `stick_drives`), `lvgl`, `rammp_rtps_messages` (`MIB::MibSystemState`,
`MIB::DriveProfile`, `rammp::DriveRequest`), `stick` (ButtonEdges). Private: `ui` (the
export's padlock and screens), `esp_timer` (the clock). Required by `hmi_ui` (UiApp owns the
instances) and `main` (the DriveAdapter over DrivePort).

## Known gaps

- No L1 test of DriveUi or StickButton: they are LVGL-bound. DrivePort runs on the host in the
  drive goldens; the rest is covered on the board (bench B4, B5, B5a-e).
- CS-SAF-04 (no allocation after start-up): `lock_visual_wait` starts an LVGL animation and
  `unlock_timer_start` creates an LVGL timer, both from LVGL's pool, as before the move.
- What asks the drive session for a change (the unlock and exit holds, the burger key, the
  readiness checks around them) stays in hmi_ui (HoldEngine, NavView, UiApp), as does the seat
  command path (UiApp's `seat_request`).
