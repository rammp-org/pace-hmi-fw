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
| `include/hmi_ui/app_state.hpp`, `src/app_state.cpp` | The state the main unit's parts share (the Joystick view, the RTPS blink subject, the stick latches and gate, the Settings subjects and applied settings, the menu's overlay and on-arrival flag), declared once so a part can leave `main.cpp` and still reach it (from `main/frag_state.inc`, moved verbatim). Global names, as the moved code reads them. Legacy debt under ratchet transfer grants (`mutable_globals`); each piece moves on to its owner (S7) or a channel (T-H4a) |
| `include/hmi_ui/nav_port.hpp` | `NavPort`: NavView's calls the other views need (UiApp fills it: the instance's `to_key`, `use_group`, the static look) |
| `include/hmi_ui/setting_subjects.hpp`, `src/setting_subjects.cpp` | `SettingSubjects`: one subject per settings_spec.hpp parameter, what each Settings row shows and steps; `init` starts them from the saved settings, applies the theme and hands the rest to main's `setting_store_observer` (from `main/frag_state.inc`, `frag_brightness.inc` and app_main) |
| `include/hmi_ui/settings_view.hpp`, `src/settings_view.cpp` | `SettingsView`: the SettingsScreen's -/+ rows, their limits, press flash and the actuator rejection flash (from `main/frag_settings_ui.inc` and app_main's wiring). What a setting does when it changes (settings, flip, the ADC task's atomics) is UiApp's `store_setting`; the seat command path is UiApp's |
| `include/hmi_ui/shared_subjects.hpp` | `SharedSubjects`: the subjects several views read (UiApp's `locked`, `mib_state`, `rtps_link`; app_state's `rtps_blink`). UiApp fills it; it shrinks as each subject moves to its view (S7) |
| `include/hmi_ui/diagnostics_view.hpp`, `src/diagnostics_view.cpp` | `DiagnosticsView`: the DiagnosticsScreen's rows, the rate label and the stale warning (from `main/frag_diag.inc`) |
| `include/hmi_ui/link_state.hpp` | `LinkState`: the values the `rtps_link` subject carries, equal to main's `RtpsLinkState` (main static_asserts it) |
| `include/hmi_ui/actions_view.hpp`, `src/actions_view.cpp` | `ActionsView`: the SkunkWorksScreen's tiles, their grid walk and the greyed MCB tiles (from `main/frag_actions.inc`); what each tile does is UiApp's `action_run_`, over its ports |
| `include/hmi_ui/internet_view.hpp`, `src/internet_view.cpp` | `InternetView`, `WifiNetwork`, `WifiJoinResult`: the InternetScreen's three pages (from `main/internet_ui.cpp`); the network calls, the worker thread, the hand-back to the LVGL task and the restart are main's, through its Config (`main/internet_ui.cpp` keeps them) |
| `include/hmi_ui/about_view.hpp`, `src/about_view.cpp` | `AboutView`: the AboutScreen (from `main/about_ui.cpp`); what it shows comes from main through its Config (`main/about_ui.cpp` keeps the adapters) |
| `include/hmi_ui/firmware_info.hpp` | `FirmwareInfo`, `FirmwareRelease`, `FirmwareIdentity`: what the screens show about the image running (main's FwInfo, esp_app_desc) |
| `include/hmi_ui/bench_pin_view.hpp`, `src/bench_pin_view.cpp` | `BenchPinView`: the BenchGateScreen's PIN pad, dots and line, on hmi_models' `PinModel` (from `main/frag_bench_pin.inc` and app_main's wiring) |
| `include/hmi_ui/brightness_view.hpp`, `src/brightness_view.cpp` | `BrightnessView`: the backlight level as a subject, applied on every change and saved once it settles (from `main/frag_brightness.inc`) |
| `include/hmi_ui/button_grid.hpp`, `src/button_grid.cpp` | `ButtonGrid`, `grid_key_cb`, `grid_sync_cursor`: the joystick's walk over a page of buttons (seat, bench PIN), on hmi_models' `grid_step` (from `main/frag_seat.inc`) |
| `include/hmi_ui/display_flip.hpp`, `src/display_flip.cpp` | `DisplayFlip`: DIRECT rendering into the DSI panel's two frame buffers, the 180-degree Settings "Flip screen" (PPA, CPU fallback) and the touch input that turns with it (from `main/frag_display_flip.inc`). Finds itself through the display's and the touch input's LVGL driver data. The PSRAM frame's `heap_caps_aligned_alloc` is legacy debt moved with `ratchet.py transfer` |
| `include/hmi_ui/drive_band_view.hpp`, `src/drive_band_view.cpp` | `DriveBandView`: the Drive screen's speed readout and its three drive-profile buttons (from `main/frag_drive_band.inc`) |
| `include/hmi_ui/drive_port.hpp` | `DrivePort<Ui>`, `drive_screen_of`, `drive_mib_of`: the drive adapter's port (components/drive_adapter `DrivePort`), each action's LVGL and RTPS call as it has always been (from `main/frag_drive.inc`, MainDriveView). A template over the drive UI so its calls are direct (CS-SAF-08) and the drive goldens (tests/host/drive_golden) run it verbatim. **Safety-relevant** (CS-SAF-01): it sends the DriveCommand. Included by main, which makes the one DriveAdapter over it (needs `ui` and `esp_timer`, private here) |
| `include/hmi_ui/drive_ui.hpp`, `src/drive_ui.cpp` | `DriveUi`: the padlock (ring and shackle) and the one-second advance to Drive after an unlock, the stick gate's writer (`update_stick_gate`), the holds' "let go first" flags, and what DrivePort acts on (from `main/frag_lock.inc`, `frag_nav.inc` and app_main's lock wiring); `mcb_ready`, `seat_ready` (from `main/frag_refusal.inc`). UiApp owns the one instance; main owns the DriveAdapter made over it |
| `include/hmi_ui/refused.hpp` | `Refused`, `DRIVE_REFUSED_SHOW_MS`, `EXIT_REFUSED_SHOW_MS`: which request a banner says was refused, and its dwell (from `refusal_view.hpp` and `main/frag_refusal.inc`); no LVGL |
| `include/hmi_ui/fps_meter.hpp`, `src/fps_meter.cpp` | `FpsMeter`: render time per frame and the once-a-second `[FPS]` debug line, `CONFIG_HMI_DEBUG_FPS` only (from `main/frag_fps.inc`) |
| `include/hmi_ui/hold_gesture.hpp`, `src/hold_gesture.cpp` | `HoldGesture`, `HoldEngine`: the push-and-hold gestures (unlock, drive exit, calibrate) and the engine that fills and completes them (from `main/frag_hold.inc`); `touch_held_on`, the touch half of Calibrate's hold (from `main/frag_hold_poll.inc`) |
| `include/hmi_ui/hold_range.hpp` | `HOLD_MAX`: the range a hold fills, which is also the padlock ring's (DriveUi, DrivePort); no LVGL, so the drive goldens read it |
| `include/hmi_ui/joystick_view.hpp`, `src/joystick_view.cpp` | `JoystickView`: the Joystick test screen's axis bars and GPIO48 button count, and their subjects (from `main/frag_status_band.inc`, `frag_state.inc` and app_main) |
| `include/hmi_ui/log_view.hpp`, `src/log_view.cpp` | `LogView`, `LogLineLevel`: the LogScreen (from `main/log_view.cpp`); the lines come from main's log capture through its Config |
| `include/hmi_ui/nav_view.hpp`, `src/nav_view.cpp` | `NavView`: the burger key and its menu on every screen, the joystick's focus groups, nav_go and arriving on a screen (from `main/frag_nav.inc`). The stick gate's writes stay main's (`gate_update`), at the same trigger points; main's per-screen arrival wiring is its `enter_screen` |
| `include/hmi_ui/on_demand_screens.hpp`, `src/on_demand_screens.cpp` | `OnDemandScreens`: Settings, Skunk Works and Diagnostics built when opened and destroyed once left, at most one at a time (from `main/frag_screens_on_demand.inc`) |
| `include/hmi_ui/overdraw.hpp`, `src/overdraw.cpp` | `strip_screen_overdraw`, `strip_all_overdraw`: clear the background fills nobody can see (from `main/frag_overdraw.inc`; UiApp logs what was cleared, on main's "overdraw" logger) |
| `include/hmi_ui/refusal_view.hpp`, `src/refusal_view.cpp` | `RefusalView`: the ErrorBanners that say why driving or the seat is not permitted (a refused request; a drive cut short or an exit refused), their dwell timer and the hold poll's push check (from `main/frag_refusal.inc`). The texts are `refusal_texts.hpp`'s; `mcb_ready` and `seat_ready` are drive_ui.hpp's |
| `include/hmi_ui/rtps_label_view.hpp`, `src/rtps_label_view.cpp` | `RtpsLabelView`: the TopBar's RTPS label (from `main/frag_rtps_label.inc`) |
| `include/hmi_ui/seat_view.hpp`, `src/seat_view.cpp` | `SeatView`: the SeatScreen's function buttons and adjustment page, and the seat numbers on them (from `main/frag_seat.inc`, the seat parts of `frag_settings_ui.inc` and app_main's wiring), and which page is up (main's seat_page). The seat command path (`seat_step`, `seat_request`, `seat_apply_state`) is UiApp's, publishing through its link port |
| `include/hmi_ui/stick_button.hpp`, `src/stick_button.cpp` | `StickButton`: one edge of the stick button (GPIO48): the Joystick screen's pressed panel and counter, the level for the ADC task and the select key, in that order, on `stick`'s ButtonEdges (from `main/frag_stick_button.inc`). Runs on the button's task; main's caller holds lvgl_mutex |
| `include/hmi_ui/status_band_view.hpp`, `src/status_band_view.cpp` | `StatusBandView`: the DriveBand's DRIVE and STATE cells, on every resident and on-demand screen (from `main/frag_status_band.inc`) |
| `include/hmi_ui/widget_tree.hpp`, `src/widget_tree.cpp` | `for_each_descendant` (iterative, pre-order, bounded), `set_focused_recursive`, `clear_click_focusable_recursive`: helpers for rows and buttons whose look spans their children (from `main/frag_settings_ui.inc`) |
| `include/hmi_ui/update_view.hpp`, `src/update_view.cpp` | `UpdateView`, `ReleaseList`, `InstallStage`, `InstallStatus`: the UpdateScreen's three pages (from `main/update_ui.cpp`); the release list, the install, the worker thread, the hand-back to the LVGL task and the restart after an install (with its may_restart guard) are main's (`main/update_ui.cpp`) |
| `include/hmi_ui/topbar_view.hpp`, `src/topbar_view.cpp` | `TopBarView`: the TopBar's clock and link labels (from `main/frag_clock.inc`). Setting the clock from the MCB is not UI and stays in main |
| `include/hmi_ui/resident_chrome.hpp`, `src/resident_chrome.cpp` | `ScreenChrome`, `for_each_resident_chrome`: the TopBar, band, burger key and overlay of every screen ui_init builds, one list so none is forgotten (from app_main's wiring); main binds its views and nav on each |
| `include/hmi_ui/rtps_ui_bridge.hpp`, `src/rtps_ui_bridge.cpp` | `RtpsUiBridge`: what an MCB status or a diagnostics sample changes in the UI (subjects only), on the RTPS receive task with `lvgl_mutex` held by main's handlers (from app_main's RTPS callbacks); UiApp owns the one instance |
| `include/hmi_ui/ui_app.hpp`, `src/ui_app.cpp`, `src/ui_app_build.cpp` | `UiApp`: every view instance of the island, wired together, and the glue between them (arrivals, the menu's destinations, the holds, the seat command path, the Settings, Skunk Works and Diagnostics observers, the screens built on demand, the joystick's keypad read), and the UI build in app_main's order (`build`, `build_input`, `build_on_demand_parts`). Moved from `main/main.cpp` (2026-10-08). main owns the one instance (constinit) and fills its ports |
| `include/hmi_ui/app_ports.hpp` | The ports UiApp reaches the rest of the firmware through, one table of plain functions per concern: `LinkPort` (rtps_comms), `DriveInputs` (main's DriveAdapter), `CuesPort` (haptic and sound cues), `SelfTestPort`, `BoardPort` (backlight, panel, restart), `MainScreens` (the screens whose adapters stay in main). main fills each once |
| `include/hmi_ui/fn.hpp` | `Fn<R(Args...)>`, `bind<&T::m>(obj)`: a non-owning callable (a plain function, or a member function bound to its object), constant-initialisable: how one view's Config calls another without a global |
| `include/hmi_ui/refusal_texts.hpp` | `REFUSAL_TEXTS`: the refusal banners' words from `hmi_rtps_spec` (was main's kRefusalTexts) |
| `include/hmi_ui/actions_spec.h` | The Skunk Works tiles' X-macro table (moved from `main/actions_spec.h`) |
| `include/hmi_ui/ui_build.hpp`, `src/ui_build.cpp` | `build_screens`, `finish_build`, `perf_overlay_font`, `PerfOverlay` (the FPS counter slot's toggle, from `main/frag_status_band.inc`): the UI build's steps that need only what they are passed (every screen ui_init builds less the on-demand ones, the boot logo; the perf overlay hidden, the overdraw pass, the boot screen's exit; the overlay's font). Called by app_main in its order (from main.cpp's wiring) |
| `include/hmi_ui/ui_island.hpp`, `src/ui_island.cpp` | `UiIsland`: the task that runs LVGL (today's `lv_task`): one LVGL cycle every 8 ms at most, then it yields at least a tick (from app_main). The cycle itself, `lv_task_handler` under `lvgl_mutex`, is main's `lvgl_cycle`, passed in its Config, so the island takes no lock |
| `include/hmi_ui/ui_poll.hpp`, `src/ui_poll.cpp` | `UiPoll`: the island's 250 ms tick: link indicator and blink, diagnostics staleness, the drive adapter's tick, theme switch (from `main/frag_rtps_poll.inc`) |

## Rules for code here

- Only the UI task calls into a view, or `app_main` while it builds the UI before `lv_task`
  starts. Each function says which, and whether `lvgl_mutex` is held. No view takes a lock:
  callers on other tasks keep theirs in main (CS-OWN-08).
- No mutable state at namespace scope (CS-CMP-03). A view's state is its members; UiApp owns the
  one instance of each, and main owns the one UiApp, built at compile time (`constinit`, no
  global constructor: G4), and builds it from `app_main` (V6). The exception is `app_state.cpp`: main's shared state, moved
  verbatim with its debt (transfer grants), to be dissolved into its owners.
- Subjects are initialised before anything binds to them (V7); a view's `init` comes before its
  `bind`.
- Board calls (backlight, RTC), the RTPS link, the cues, the self test and main's own screens
  come in through UiApp's ports (`app_ports.hpp`) as plain function pointers, so the island
  needs no board component and includes nothing from main. One view calls another through an
  `Fn` in its Config.

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
| REQ-UI-09 | Diagnostics: one row per table item with its short label, label and each reading under its unit (a reading the item lacks is hidden); a reading reads `--` until known, else its value with the item's decimals. While `stale`, every row's labels and the rate label are red at the blink's opacity and the rate label reads "No data"; otherwise the rate label reads the rate. Up/down walk the rows (clamped), down off the last goes to the burger key. | bench B4 |
| REQ-UI-10 | Skunk Works: one tile per action with its title and subtitle; the stick walks them as the grid the panel wraps them into (columns measured, clamped, down off the last row to the burger key); a press focuses the tile and runs its action. A tile that needs the MCB is greyed (UNAVAILABLE, 40 % opacity) while main's readiness says no, stays walkable, and a press on it only plays the refusal cue. | bench B4 |
| REQ-UI-11 | Settings rows: each row shows its value scaled by its decimals (or its name), `--` while unknown; "-"/"+" grey at the range ends and both grey while unknown. Up/down walk the rows (clamped), down off the last goes to the burger key; left/right or a tap on "-"/"+" step the row: a greyed button or a locked-only row while unlocked only plays the refusal cue; otherwise the button flashes PRESSED for PRESS_FLASH_MS and the row's subject moves one step, clamped; on the actuators page main's `seat_step` is asked instead and the value moves only when the MCB reports it. A row and everything in it look focused together. | bench B4, B4b |
| REQ-UI-12 | About: version, commit, build date and time, MAC and hostname are filled once; the verdict, the SHA-256 (two lines of four groups), the link ("<name>, <state words>") and the IP are refreshed every REFRESH_MS while the screen is up and on its arrival. The mark is a green check for a known release or pre-release, a red cross with the reason otherwise, nothing while the hash is still being computed. | bench B4 |
| REQ-UI-13 | Log: the lines the capture keeps, oldest first, one per line, errors red and warnings amber (a "#" escaped so it shows); rebuilt at most every POLL_MS and only while the screen is up. It follows the newest line while the reader is at the bottom (or just arrived), and otherwise keeps the line being read in place as old lines drop out. Up/down page vertically, left/right sideways; down at the newest line calls the escape (main: the burger key). Without memory for the text the export's placeholder stays. | bench B4 |
| REQ-UI-14 | Internet: the main page shows the chosen link (Ethernet/WiFi, checked), the saved network, the link in use, its state, IP and signal, refreshed every STATUS_POLL_MS while it is up; "Restart" is shown only when a restart would change the link, and calls main's restart. "WiFi network" scans on a worker and lists the networks; an open one is joined at once, a secured one asks for its password (8+ characters, or none). One scan or join at a time; a result for a page already left is dropped. A join that fails says why and refuses; one that succeeds returns to the main page. | bench B4 (Wi-Fi in range) |
| REQ-UI-15 | Update: the list page shows the version installed (and whether it is a release), then every release newest first, the running one marked "Installed"; it is fetched again on a worker at each visit (one request at a time; a failed fetch keeps the last list and refuses). The pick page shows date, kind, size and notes, and Install only starts one with a firmware file and none running. The run page shows the stage, message, progress and log, and while the install is done but main may not restart yet, says it waits for the chair to stop. | bench B4 (list, pick); install not run on the bench |

## Tasks and dependencies

- Tasks: `UiIsland` runs the UI task, `lv_task` (16 KB, priority 20, core 1, 8 ms). Views run
  on it (LVGL timers and observers) and on `app_main` during start-up. `UiIsland::Config::task`
  is that row as a literal, copied from the code it replaced, so the G10 task dump
  (`tools/guards/baselines/tasks.json`) is unchanged. The topology's `ui` row
  (`components/topology`, CS-CON-02) is the target design; taking the config from
  `topo.task_config(...)` is a later step, made once the owner has reviewed the TASKS rows,
  and it comes with its own G10 baseline.
- Dependencies, public (the headers use their types): `drive_adapter` and `drive_session`
  (DrivePort, DriveUi), `stick` (StickButton's ButtonEdges), `lvgl`, `hmi_format` (texts,
  `StepperSpec`), `hmi_models` (`grid.hpp`, `pin.hpp`), `hmi_rtps_spec` (the refusal texts, seat units, the
  MIB timeouts), `rammp_rtps_messages`
  (`MIB::MibSystemState`, the seat axis table), `ota_parse` (`hmi::ota::Release`),
  `esp_driver_ppa`, espp `logger` and `task` (`UiIsland`), and `settings` (the spec table and the saved values). Private: `ui` (the export: widgets, component children,
  theme colours), `esp_mm` (the flip's cache sync), `esp_timer` (the FPS meter). `main`
  requires the component (`PRIV_REQUIRES hmi_ui`).
- Build: `fw_component_options(${COMPONENT_LIB})` (C++23 and the fw warning set as errors),
  called unguarded like every first-party component; the top-level CMakeLists defines it
  before the components are evaluated and fails the configure if a component leaves it out.

## Tests

None at L1 yet: every view is LVGL-bound. They are covered on the board by the bench (B4
screenshots of the TopBar and status band on every resident screen, B5 drive/lock).
