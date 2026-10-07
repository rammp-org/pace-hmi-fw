# hmi_ui

The UI island: everything the UI task draws (CS-UI-02).

Each view binds widgets of the SquareLine export (`components/ui`, generated) to LVGL subjects
and paints what they say. One file pair per view or concern (CS-UI-04); the logic that needs no
LVGL lives in `hmi_format` and `hmi_models` (CS-UI-03). The views move here from `main/`'s
fragments step by step (`docs/plans/app-main-shrink.md` S4-S6), each without a change in
behaviour.

## Files

| File | What |
| --- | --- |
| `include/hmi_ui/nav_port.hpp` | `NavPort`: main's navigation calls the views need, until nav moves here. main fills one constexpr table |
| `include/hmi_ui/shared_subjects.hpp` | `SharedSubjects`: the subjects main still defines (`main/frag_state.inc`) and several views read. main fills it once; it shrinks as each subject moves to its view (S7) |
| `include/hmi_ui/link_state.hpp` | `LinkState`: the values the `rtps_link` subject carries, equal to main's `RtpsLinkState` (main static_asserts it) |
| `include/hmi_ui/bench_pin_view.hpp`, `src/bench_pin_view.cpp` | `BenchPinView`: the BenchGateScreen's PIN pad, dots and line, on hmi_models' `PinModel` (from `main/frag_bench_pin.inc` and app_main's wiring) |
| `include/hmi_ui/brightness_view.hpp`, `src/brightness_view.cpp` | `BrightnessView`: the backlight level as a subject, applied on every change and saved once it settles (from `main/frag_brightness.inc`) |
| `include/hmi_ui/button_grid.hpp`, `src/button_grid.cpp` | `ButtonGrid`, `grid_key_cb`, `grid_sync_cursor`: the joystick's walk over a page of buttons (seat, bench PIN), on hmi_models' `grid_step` (from `main/frag_seat.inc`) |
| `include/hmi_ui/display_flip.hpp`, `src/display_flip.cpp` | `DisplayFlip`: DIRECT rendering into the DSI panel's two frame buffers, the 180-degree Settings "Flip screen" (PPA, CPU fallback) and the touch input that turns with it (from `main/frag_display_flip.inc`). Finds itself through the display's and the touch input's LVGL driver data. The PSRAM frame's `heap_caps_aligned_alloc` is legacy debt moved with `ratchet.py transfer` |
| `include/hmi_ui/drive_band_view.hpp`, `src/drive_band_view.cpp` | `DriveBandView`: the Drive screen's speed readout and its three drive-profile buttons (from `main/frag_drive_band.inc`) |
| `include/hmi_ui/fps_meter.hpp`, `src/fps_meter.cpp` | `FpsMeter`: render time per frame and the once-a-second `[FPS]` debug line, `CONFIG_HMI_DEBUG_FPS` only (from `main/frag_fps.inc`) |
| `include/hmi_ui/hold_gesture.hpp`, `src/hold_gesture.cpp` | `HoldGesture`, `HoldEngine`: the push-and-hold gestures (unlock, drive exit, calibrate) and the engine that fills and completes them (from `main/frag_hold.inc`) |
| `include/hmi_ui/overdraw.hpp`, `src/overdraw.cpp` | `strip_screen_overdraw`, `strip_all_overdraw`: clear the background fills nobody can see (from `main/frag_overdraw.inc`; main keeps the log line) |
| `include/hmi_ui/refusal_view.hpp`, `src/refusal_view.cpp` | `RefusalView`: the ErrorBanners that say why driving or the seat is not permitted (a refused request; a drive cut short or an exit refused), their dwell timer and the hold poll's push check (from `main/frag_refusal.inc`). `mcb_ready`, `seat_ready` and the texts table stay in main |
| `include/hmi_ui/rtps_label_view.hpp`, `src/rtps_label_view.cpp` | `RtpsLabelView`: the TopBar's RTPS label (from `main/frag_rtps_label.inc`) |
| `include/hmi_ui/seat_view.hpp`, `src/seat_view.cpp` | `SeatView`: the SeatScreen's function buttons and adjustment page, and the seat numbers on them (from `main/frag_seat.inc`, the seat parts of `frag_settings_ui.inc` and app_main's wiring). The seat command path (`seat_step`, `seat_request`, `seat_apply_state`) stays in main |
| `include/hmi_ui/status_band_view.hpp`, `src/status_band_view.cpp` | `StatusBandView`: the DriveBand's DRIVE and STATE cells, on every resident and on-demand screen (from `main/frag_status_band.inc`) |
| `include/hmi_ui/topbar_view.hpp`, `src/topbar_view.cpp` | `TopBarView`: the TopBar's clock and link labels (from `main/frag_clock.inc`). Setting the clock from the MCB is not UI and stays in main |
| `include/hmi_ui/ui_poll.hpp`, `src/ui_poll.cpp` | `UiPoll`: the island's 250 ms tick: link indicator and blink, diagnostics staleness, the drive adapter's tick, theme switch (from `main/frag_rtps_poll.inc`) |

## Rules for code here

- Only the UI task calls into a view, or `app_main` while it builds the UI before `lv_task`
  starts. Each function says which, and whether `lvgl_mutex` is held. No view takes a lock:
  callers on other tasks keep theirs in main (CS-OWN-08).
- No mutable state at namespace scope (CS-CMP-03). A view's state is its members; main owns the
  one instance of each, built at compile time (`constinit`, no global constructor: G4), and
  binds it from `app_main` (V6).
- Subjects are initialised before anything binds to them (V7); a view's `init` comes before its
  `bind`.
- Board calls (backlight, RTC) and settings storage come in through a view's `Config` as plain
  function pointers, so the island needs no board component.

## Requirements

| ID | Requirement | Verified by |
| --- | --- | --- |
| REQ-UI-01 | DRIVE reads ACTIVE in the theme's ok colour only while unlocked (`locked` 0), the link is CONNECTED and the MIB reports ENABLED; otherwise it reads LOCKED in the alert colour. | bench B4, B5 |
| REQ-UI-02 | STATE reads `---` in the muted colour while the link is not CONNECTED. Connected, it reads OK (ok colour) for IDLE and ENABLED, STARTING (muted) for INITIALIZING and ERROR (alert) for anything else; a non-empty `state_text` from the MIB replaces the word, never the colour. | bench B4 |
| REQ-UI-03 | A band's labels repaint on every change of a subject their text depends on: DRIVE on `mib_state`, `rtps_link`, `drive_text` and `locked`; STATE on `mib_state`, `rtps_link` and `state_text` (seven observers per band, each bound to its label). | bench B4, B5; G11 census when it exists |
| REQ-UI-04 | The TopBar's RTPS label is in the alert colour for NET_FAILED and LINK_DOWN, orange (`STATUS_ORANGE`) for NO_IP, orange and blinking with `rtps_blink` (opacity, so nothing reflows) for NO_PEER, and the theme's ok colour for CONNECTED; it repaints on `rtps_link` and `rtps_blink`. | bench B4 |
| REQ-UI-05 | The backlight follows the brightness subject on every change, the boot value included. A level is saved once it has stayed unchanged for the save delay (main: 1 s), so a run of changes is one write; the boot value is not saved. `set` clamps to `min_percent`..`max_percent` (never 0); `step` goes to the next of 25/50/75/100 % above the current level, and from 100 % back to 25 %. | bench (side button, Settings row, `rtps_brightness.py`) |
| REQ-UI-06 | The TopBar clock reads `--:--` until main says the system clock holds a real time, then `HH:MM` of the system clock (hmi_format REQ-FMT-06), from the first frame and then read once a second (`CLOCK_POLL_MS`); the subject changes only when the text does. The link label reads the text main gives: the setting at boot, the link RTPS brought up after it starts. | bench B4 |
| REQ-UI-07 | The bench gate: each pad key types its digit (DIGITS) or takes one back; the dots show how many are typed; typing puts the prompt back; the fourth digit is judged by `PinModel` (REQ-MOD-05..08): the right PIN empties the entry and calls `accepted`, a wrong one empties it and shows WRONG_TEXT. `reset` (every visit) empties it and shows the prompt. The joystick walks the pad as a ButtonGrid, down off it calls `off_bottom`. | bench B4b |
| REQ-UI-08 | Seat: a live function button (one with an axis row) shows the adjustment page over the buttons, names its function and moves the joystick onto "-"; the inert pair only take focus. "-"/"+" call `step(row, -1/+1)`, a preset calls `request(axis, target)` with the number on its label in display units; nothing is asked before a function button has picked an axis. "<", or left off the page's first column, hides the page and returns the joystick to the selected button. The numbers show only the value subjects (`--` until known), and the preset at the current value is CHECKED. | bench B4b, B5 (simulated MCB) |

## Tasks and dependencies

- Tasks: none of its own. Views run on the UI task (LVGL timers and observers) and on
  `app_main` during start-up.
- Dependencies, public (the headers use their types): `lvgl`, `hmi_format` (texts,
  `StepperSpec`), `hmi_models` (`grid.hpp`, `pin.hpp`), `rammp_rtps_messages`
  (`MIB::MibSystemState`, the seat axis table), `esp_driver_ppa` and espp `logger`. Private:
  `ui` (the export: widgets, component children, theme colours), `esp_mm` (the flip's cache
  sync), `esp_timer` (the FPS meter). `main` requires the component (`PRIV_REQUIRES hmi_ui`).
- Build: `fw_component_options(${COMPONENT_LIB})` (C++23 and the fw warning set as errors),
  called unguarded like every first-party component; the top-level CMakeLists defines it
  before the components are evaluated and fails the configure if a component leaves it out.

## Tests

None at L1 yet: every view is LVGL-bound. They are covered on the board by the bench (B4
screenshots of the TopBar and status band on every resident screen, B5 drive/lock).
