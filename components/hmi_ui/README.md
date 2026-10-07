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
| `include/hmi_ui/status_band_view.hpp`, `src/status_band_view.cpp` | `StatusBandView`: the DriveBand's DRIVE and STATE cells, on every resident and on-demand screen (from `main/frag_status_band.inc`) |

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

## Tasks and dependencies

- Tasks: none of its own. Views run on the UI task (LVGL timers and observers) and on
  `app_main` during start-up.
- Dependencies: `lvgl` (public: the headers use its types); private: `ui` (the export: widgets,
  component children, theme colours), `rammp_rtps_messages` (`MIB::MibSystemState`). `main`
  requires the component (`PRIV_REQUIRES hmi_ui`).
- Build: `fw_component_options` really applies here (C++23 and the warning set as errors).
  CMakeLists.txt includes `cmake/fw_standards.cmake` itself, because the top-level file includes
  it only after `project()`, when the `if(COMMAND ...)` guard the other components use has
  already been evaluated as false.

## Tests

None at L1 yet: every view is LVGL-bound. They are covered on the board by the bench (B4
screenshots of the TopBar and status band on every resident screen, B5 drive/lock).
