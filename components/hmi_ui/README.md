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

## Tasks and dependencies

- Tasks: none of its own. Views run on the UI task (LVGL timers and observers) and on
  `app_main` during start-up.
- Dependencies: `lvgl` (public: the headers use its types). `main` requires the component
  (`PRIV_REQUIRES hmi_ui`).

## Tests

None at L1 yet: every view is LVGL-bound. They are covered on the board by the bench (B4
screenshots of the TopBar and status band on every resident screen, B5 drive/lock).
