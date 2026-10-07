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
| `include/hmi_ui/shared_subjects.hpp` | `SharedSubjects`: the subjects main still defines (`main/frag_state.inc`) and several views read. main fills it once; it shrinks as each subject moves to its view (S7) |
| `include/hmi_ui/link_state.hpp` | `LinkState`: the values the `rtps_link` subject carries, equal to main's `RtpsLinkState` (main static_asserts it) |
| `include/hmi_ui/brightness_view.hpp`, `src/brightness_view.cpp` | `BrightnessView`: the backlight level as a subject, applied on every change and saved once it settles (from `main/frag_brightness.inc`) |
| `include/hmi_ui/display_flip.hpp`, `src/display_flip.cpp` | `DisplayFlip`: DIRECT rendering into the DSI panel's two frame buffers, the 180-degree Settings "Flip screen" (PPA, CPU fallback) and the touch input that turns with it (from `main/frag_display_flip.inc`). Finds itself through the display's and the touch input's LVGL driver data. The PSRAM frame's `heap_caps_aligned_alloc` is legacy debt moved with `ratchet.py transfer` |
| `include/hmi_ui/fps_meter.hpp`, `src/fps_meter.cpp` | `FpsMeter`: render time per frame and the once-a-second `[FPS]` debug line, `CONFIG_HMI_DEBUG_FPS` only (from `main/frag_fps.inc`) |
| `include/hmi_ui/overdraw.hpp`, `src/overdraw.cpp` | `strip_screen_overdraw`, `strip_all_overdraw`: clear the background fills nobody can see (from `main/frag_overdraw.inc`; main keeps the log line) |
| `include/hmi_ui/rtps_label_view.hpp`, `src/rtps_label_view.cpp` | `RtpsLabelView`: the TopBar's RTPS label (from `main/frag_rtps_label.inc`) |
| `include/hmi_ui/status_band_view.hpp`, `src/status_band_view.cpp` | `StatusBandView`: the DriveBand's DRIVE and STATE cells, on every resident and on-demand screen (from `main/frag_status_band.inc`) |
| `include/hmi_ui/topbar_view.hpp`, `src/topbar_view.cpp` | `TopBarView`: the TopBar's clock and link labels (from `main/frag_clock.inc`). Setting the clock from the MCB is not UI and stays in main |

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

## Tasks and dependencies

- Tasks: none of its own. Views run on the UI task (LVGL timers and observers) and on
  `app_main` during start-up.
- Dependencies: `lvgl`, `esp_driver_ppa` and espp `logger` (public: the headers use their
  types); private: `ui` (the export: widgets, component children, theme colours),
  `rammp_rtps_messages` (`MIB::MibSystemState`), `hmi_format` (the clock text), `esp_mm` (the
  flip's cache sync), `esp_timer` (the FPS meter). `main` requires the component
  (`PRIV_REQUIRES hmi_ui`).
- Build: `fw_component_options(${COMPONENT_LIB})` (C++23 and the fw warning set as errors),
  called unguarded like every first-party component; the top-level CMakeLists defines it
  before the components are evaluated and fails the configure if a component leaves it out.

## Tests

None at L1 yet: every view is LVGL-bound. They are covered on the board by the bench (B4
screenshots of the TopBar and status band on every resident screen, B5 drive/lock).
