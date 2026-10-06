# Plan: shrink app_main, turn the fragments into real units

Status: draft for the owner. Measured on `dev_refactor` 33f8d53 and the draft `dev_ai_refactor_drive`
6c431e4 (`C:\w\drive`). Targets (owner): `app_main` ≤300 lines (CS-LAY-01), `main.cpp` ≤1000
(CS-FIL-01), fragments become components; first, the drive adapter leaves the `main.cpp` unit
so `tools/l0/ratchet.py` passes. Today on the drive branch: `FAIL lines main/main.cpp: 4135 > 3984`.

Map script: `%TEMP%\claude\appmain-deps\deps.py <repo> [--detail --pairs --kinds --json f]`.
It strips comments, strings and preprocessor lines with ratchet's lexer, collects namespace-scope
definitions per unit (prototypes kept apart), and greps identifiers in every other unit.
Heuristic: a local with the same name as a top-level symbol counts as a use (rare here).

## 1. Dependency map (dev_refactor; nb = non-blank code lines, ratchet's count)

| Unit | nb | defs fn/var/const/type | exported to others | imported (from units) | back-refs* | top importers |
| --- | --- | --- | --- | --- | --- | --- |
| main_head | 3 | 0/2/0/0 | 2 (`lvgl_mutex`, `audio_bytes`) | 0 | 0 | brightness, stick_button, app_main, audio |
| frag_fps | 15 | 2/4/2/0 | 7 | 0 | 0 | app_main 7 |
| frag_state | 67 | 1/54/1/0 | **56** | 8 (2) | 8 | app_main 51, settings_ui 17, nav 12, refusal 7 |
| frag_haptics | 26 | 1/1/2/0 | 4 | 0 | 0 | da7280 3, actions 2, hold, drive |
| frag_status_band | 85 | 4/2/6/1 | 8 | 5 (1) | 0 | stick_config 4, app_main 4 |
| frag_stick_config | 22 | 4/0/1/0 | 5 | 4 (1) | 0 | app_main 5 |
| frag_rtps_label | 33 | 2/0/0/0 | 1 | 3 (2) | 0 | screens_on_demand, app_main |
| frag_drive_band | 35 | 5/3/0/0 | 7 | 4 (3) | 2 | app_main 7 |
| frag_rtps_poll | 39 | 2/0/1/0 | 2 | 6 (4) | 3 | app_main 2 |
| frag_brightness | 32 | 4/2/2/0 | 8 | 1 (1) | 0 | app_main 7 |
| frag_clock | 58 | 5/7/1/0 | 13 | 1 (1) | 0 | app_main 12 |
| frag_stick_button | 33 | 2/0/5/0 | 7 | 13 (5) | 7 | app_main 4, hold, lock, drive |
| frag_hold | 56 | 5/1/4/1 | 9 | 5 (4) | 1 | lock 6, drive 4, hold_poll 4 |
| frag_lock | 74 | 6/5/1/0 | 10 | 20 (10) | **10** | drive 7, app_main 4 |
| frag_refusal | 221 | 14/2/4/1 (+8 enumerators) | 22 | 12 (5) | 3 | drive 13, app_main 6, nav 4 |
| frag_drive | 120 | 9/7/0/0 | 8 | 35 (8) | 2 | nav 3, lock 2 |
| frag_drive (draft branch) | 276 | 20/1/0/1 | 7 | **40 (8)** | 3 | nav 3, lock 2, refusal 2 |
| frag_hold_poll | 51 | 3/3/3/0 | 5 | 10 (7) | 1 | seat 3, app_main 3 |
| frag_seat | 148 | 8/6/0/1 | 13 | 10 (4) | 5 | app_main 9, bench_pin 2, settings_ui 2 |
| frag_bench_pin | 54 | 4/6/5/0 | 11 | 5 (4) | 1 | app_main 9, nav 2 |
| frag_settings_ui | 370 | 27/12/15/3 | 30 | 28 (9) | 4 | app_main 19, diag 6, nav 5 |
| frag_actions | 135 | 10/5/4/1 | 6 | 11 (7) | 4 | nav 5 |
| frag_diag | 149 | 10/8/7/2 | 13 | 9 (4) | 2 | nav 7, screens_on_demand 4, app_main 4 |
| frag_nav | 568 | 39/5/6/2 (+15 enumerators) | 15 | **41 (9)** | 1 | app_main 13, state 7 |
| frag_overdraw | 47 | 4/1/0/0 | 2 | 1 (1) | 0 | rtps_poll, screens_on_demand, app_main |
| frag_screens_on_demand | 90 | 9/0/0/0 | 3 | 16 (**10**) | 0 | lock 3 (prototypes) |
| frag_display_flip | 125 | 6/11/0/0 | 8 | 1 (1) | 1 | app_main 7, settings_ui 1 |
| frag_da7280 | 208 | 5/0/10/0 | 6 | 3 (1) | 0 | app_main 6 |
| app_main | 1007 | — | — | **188 (26)** | 3 | — |
| frag_audio | 51 | 5/0/2/0 | 4 | 3 (3) | 0 | stick_button 4 (prototypes), app_main 3 |

\* back-refs: symbols used before their definition, i.e. carried by the 33 forward declarations
(state 8, lock 9, stick_button 7, rtps_poll 3, seat 3, others 1). They are today's de-facto interface.

Totals: 468 namespace-scope definitions; 285 used across units, **112 of them mutable variables**;
111 symbols are used by `app_main` alone (they are wiring state, they go with the view that wires).

**Strongly coupled pairs** (symbols both ways): nav↔state 19, settings_ui↔state 17, drive↔refusal 14,
drive↔lock 9, diag↔nav 8, drive↔state 7, refusal↔state 7, actions↔nav 7, hold↔lock 6,
nav↔settings_ui 6, diag↔settings_ui 6, seat↔settings_ui 5, diag↔screens_on_demand 5.

**Clusters**
- **K1 drive/lock**: drive, lock, hold, hold_poll, refusal, stick_button (gesture half), drive_band.
- **K2 navigation/screens**: nav, screens_on_demand, actions, diag, settings_ui, seat, bench_pin.
  `screens_on_demand` reads 10 units; `nav` reads 9. Moves last, as one cluster.
- **frag_state**: not a cluster but the global bag (54 mutable: subjects, atomics, groups). It is
  dissolved into owners step by step; it never becomes a shared header.
- **Split artefacts**: about 20 constants sit one fragment early, e.g. `kHoldMax/kHoldMs/kBarGraceMs/
  kUnlockAdvanceMs` in stick_button (used by hold, lock, drive), stick dead zones in status_band
  (used by stick_config), `kMenu*Y/kMenuSlideMs/kRowPressMs` in diag (used by nav),
  `kClockPollMs/kClockMaxDriftS` in brightness, `logger_nav` in display_flip.
- **Leaves** (≤1 imported unit, exports only to app_main or prototypes): fps, haptics, da7280,
  display_flip, overdraw, stick_config, audio, brightness, clock: 584 nb lines.

## 2. Conversion order

Rule: a fragment leaves the unit when each symbol it imports is reachable through a header or its
`Config`, and it exports functions, not variables. New paths start clean in the ratchet: 0
`mutable_globals`, 0 `locks`, 0 `log_direct`, 0 `lv_outside_ui` (only `main/*_ui.cpp` and
`components/ui/` may call `lv_*` today; a `components/hmi_ui/` entry in `UI_FILE_RES` is a tooling
prerequisite, owner's call, CS-UI-02 permits it).

| # | Step | Moves | Unit nb after (est.) | Effort |
| --- | --- | --- | --- | --- |
| S0 | Re-home split artefacts: constants to their user, prototypes into one block. Amend the `split_main.py` range map; still one TU | nothing leaves | 0 change, binary-equal | 2 h |
| S1 | **Drive adapter** (§2.1) to `components/drive_adapter` | lv-free core of frag_drive | 4135 → ~3940 (≤3984: PASS, ~45 margin) | 1 d |
| S2 | Leaves A: `haptics` (frag_haptics + frag_da7280; `haptic` pointer becomes a member; `fmt::print` → espp Logger) | 234 | −215 | 0.5 d |
| S3 | Leaves B: `display_flip` (+fps), `overdraw`, `audio` (UI-component files) | 238 | −200 | 1 d |
| S4 | Leaves C: clock, brightness, stick_config, rtps_label, status_band, drive_band, rtps_poll as `hmi_ui` chrome views | 304 | −250 | 1 d |
| S5 | K1 rest: lock, hold, hold_poll, refusal, stick_button UI half → `LockedView`, `DriveView` (CS-UI-04) | 435 | −380 | 2 d |
| S6 | K2: bench_pin, seat (models exist: `hmi_models`), then diag, actions, settings_ui, then nav + screens_on_demand | 1,514 | −1,350 | 5 d |
| S7 | frag_state dissolved: each subject to its view; stick/drive atomics to channels (§3) | 67 | −60 | in S5/S6 |

After S6 the unit is app_main plus wiring: target ≤1000 nb (CS-FIL-01), app_main ≤300 after §3-§4.

### 2.1 Where the drive adapter lands (S1)

Cut at the session boundary, not the cluster: lock, refusal, nav and hold **stay** in the unit and
are reached through a port. The adapter is lv-free and includes nothing from `main/`, so it is
host-testable (CS-UI-03) and passes `lv_outside_ui` without a tooling change.

| Goes to `components/drive_adapter/` (~200 nb) | Stays in the unit (~80 nb) |
| --- | --- |
| `DriveState` → `DriveAdapter` members (deadlines, exit latches, `ask_at_us`, session, logger) | `drive_screen_of` (nav's gate uses it), `drive_mib_of` (MIB types live in `main/hmi_rtps_spec.hpp`) |
| `drive_publish/ask` (request kept as an atomic), `drive_env` | `lock_open_visual` → frag_lock; `entry_refused_show` → frag_refusal (its subject and timer live there) |
| `drive_perform_*` (5 families), `drive_session_apply`, `input`, `drive_wait_poll` (tick), U3 paths, both hold-done handlers | `drive_exit_gesture` (its `applies` reads `lv_screen_active`); `completed` calls the adapter |
| `kDriveAnswerUs`, `kDriveWaitUs` (only drive uses them) as `Config` defaults | `MainDriveView` (the port, below) and one adapter instance |

`drive_adapter.hpp` (the only new header):
- `struct DriveSample { bool link_connected; MibState mib; Screen screen; bool menu_open; };`
- `enum class DriveBanner { REFUSED_DRIVE, REFUSED_SEAT, NOT_GRANTED, DRIVE_STOPPED, EXIT_REFUSED, DRIVE_LOST, REFUSED_DRIVE_MENU };`
- `class DriveView` (pure virtual, all UI task): `sample()`, `now_us()`, `publish(bool enable)`,
  `ring_wait()`, `ring_rest()`, `lock_open_visual()`, `unlock_timer_start()`, `unlock_timer_cancel()`,
  `unlock_timer_forget()`, `set_locked(bool)`, `gate_update()`, `menu_on_arrival(bool)`,
  `go_locked_screen()`, `go_drive_screen()`, `nav_home()`, `show_banner(DriveBanner)`, `refusal_feedback()`.
- `class DriveAdapter { struct Config { DriveView *view; int64_t answer_us, wait_us; Verbosity log_level; };`
  `bool input(Input); void tick(); void unlock_hold_done(); void exit_hold_done(); bool locked() const; }`

No variable is exposed. The unit keeps one mutable global for the instance (−1 `drive_state`,
+1 `drive_adapter`); `MainDriveView` is stateless and `const`. Callers change one token each:
lock (2), refusal (1), nav (2), drive_band (1), rtps_poll (1), the gesture (1). No `app_state.hpp`.
Prerequisites: `main/CMakeLists.txt` gets `drive_adapter` (orchestrator's file).

## 3. Task lifting (each a refactor commit; the bracketed change is a separate, parked commit)

| Task (today) | Captures / reads | Becomes (§2.1) | State lives in | Lifetime and order to keep |
| --- | --- | --- | --- | --- |
| `lv_task` (1187-1238; prio 20, core 1, 16 KB, 8 ms) | none (`[]`); `lvgl_mutex`, `kFps*`, `fps_*` | `ui` island loop, `UiIsland` | fps counters as members | starts after the whole UI is built and after touch init; app_main early `return`s destroy it (UI freezes) — keep the owner in app_main scope |
| "Data Display Task" (1261-1403; prio 10, core 1, 6 KB, 20 ms) | `[&]`: `label`, `line0`, `line1`, `madgwick_filter_fn`; statics `tab5`, `imu` | `housekeeping` island | IMU, battery, RTC readers; filters as members | after audio load; feeds `imu_accel_mg` for the self test, keep 20 ms. [The demo label/lines on the hidden screen: delete, own commit] |
| `adc_task_fn` "Read ADC" (1459-1621; espp defaults: prio 0, unpinned; 33 ms) | `[&adc, &channels]`; statics `twist_adc`, `twist_channel`, `stick`, `twist_lowpass`, `engaged`; atomics `stick_*`, `drive_speed`, `stick_drives`, `joy_*`, `remote_key`; `adc_*_subject` under `try_lock` | `control` island, `StickIsland` (`components/stick`) | ADC drivers, `Joystick`, filter, Schmitt state as members; settings atomics → one `StickSettings` mailbox | after `joystick_cal_load`, before `rtps_comms_start`; copy its TaskConfig literally (pin the defaults) |
| — H4 (gate decided on UI) | `stick_drives` written by `nav_update_stick_gate` | T-H4a: UI posts `UiContextMsg{locked, screen, menu_open}` at the same sites; control evaluates `hmi::drive_session::stick_drives()` each cycle | control | refactor: same inputs, same cycle. [T-H4b: heartbeat age ≤200 ms and MIB age, §3.3: behaviour, two approvals] |
| `touch_callback` (246-266; BSP touch task) | `[&]`: `tab5`, `logger`; statics `previous_touchpad_data`, `was_pressed` | `touch` adapter | the two statics as members | started at 1174, after `selftest_init`. [Click → sound request queue: H16] |
| `button_callback` (374-382; BSP button task) | `[&]`: `logger`; calls `brightness_step` (takes `lvgl_mutex`) | `side_button` adapter | none | started before `ui_init` (H15: keep as-is in the refactor) |
| GPIO48 `espp::Button` (699-710; "Button", prio 5, 4 KB) | `gpio48_button_callback` (frag_stick_button) | `stick_button` adapter | `joy_button_pressed` atomic → `StickButtonCh` | constructed after its subjects are initialised: keep after 688-689 |
| keypad `read` (717-745; runs on lv_task) | none; `select_key`, `joy_key`, `joy_flick`, `selftest_ui_*` | `ui` island input | latches become channels from control/stick_button/remote_ui | static, created before `lv_task` starts |
| RTPS callbacks (1629-1661; rtps task) | none; subjects under `lvgl_mutex` | `rtps_rx` adapter | `RtpsUiBridge` functions in the UI component | registered before `rtps_comms_start`. [Mailbox to UI: step 12, cut] |
| Config lambdas: selftest (1019-1070), internet/update UI, remote_ui (1679-1689) | copies: `found_addresses`, `direct_render` | wiring helpers (`selftest_config(...)`) | values in the Config | `selftest_init` before `rtps_comms_start`; `remote_ui_start` last |

Order to preserve exactly: log capture → storage migrate → OTA report → I2C probe → haptics →
expanders → LCD → display (DIRECT) → IMU → SD → RTC → battery → audio → side button → `ui_init`
→ settings → subjects → GPIO48 task → keypad → views → selftest_init → touch → **lv_task** →
audio load → housekeeping → `joystick_selftest` → ADC → control → RTPS handlers → `rtps_comms_start`
→ `fw_info_start` → remote UI. Each lifted task keeps its name, priority, core and stack.

## 4. Per-screen view init (main.cpp lines on 33f8d53)

| Lines | Wires | Target |
| --- | --- | --- |
| 128-183, 317-370 | log, storage, OTA report, I2C, haptic tests, expanders, LCD, SD, RTC, battery, audio | stays in app_main (board) |
| 189-244 | DIRECT render, panel buffers, refresh period, FPS hooks | `display_flip.initialize(...)` (S3) |
| 268-315 | IMU filters + IMU init | `housekeeping` |
| 384-450 | demo screen (bg, label, lines, rotate button) | delete (dead, own commit) |
| 452-503 | `ui_init`, on-demand destroys, boot logo | `UiIsland` build |
| 505-547 | settings load, theme, brightness, settings subjects, save timer | `SettingsModel` + brightness view |
| 549-560, 670-710 | Joystick screen: bars, calibration, GPIO48 counter | `JoystickView` |
| 562-641 | MCB subjects, chrome of 9 screens, clock, Drive speed + profile buttons, refusal timer | chrome views (S4), `DriveView` |
| 643-668 | error banners (Drive, Seat, Locked, 6 others), diagnostics subjects, poll timer | `RefusalBanner` view, `DiagnosticsView` |
| 712-785 | keypad indev, groups, lost-focus timer, repeat times | `Nav` (S6) |
| 787-810 | padlock, three hold gestures, hold poll timer | `LockedView` (S5) |
| 812-817 | Log screen | `log_view` (exists) |
| 819-908, 950-976 | Seat grids, groups, focus style, values, function label | `SeatView` (S6) |
| 910-948 | Internet, About, Update screens, OTA confirm timer | existing `*_ui_init` |
| 978-1012 | screen-loaded hooks, perf monitor, overdraw, boot → Locked | `UiIsland` build tail |
| 1072-1133 | BenchGate PIN pad | `BenchPinView` (S6) |
| 1135-1171 | Settings/SkunkWorks/Diagnostics persistent parts, perf font | their views (S6) |

app_main after §3-§4: ~250 lines (board ~200, task starts ~30, RTPS and remote UI wiring ~20).

## 5. Guards per step (the binary changes; these replace the binary-equal guard)

| Guard | How | Steps |
| --- | --- | --- |
| G1 Ratchet | `ratchet.py check` passes, then `update` locks the lower `lines` and `mutable_globals` | all |
| G2 Exported symbols | `nm -C --defined-only` on each new `.obj`: external symbols == the header's API, nothing else (no accidental external linkage); `nm` on the ELF: each moved function defined once | all |
| G3 ODR | every type used by 2+ TUs (`HoldGesture`, `ButtonGrid`, `DriveBanner`, `StatusKind`…) in exactly one header; a grep fails on a duplicate `struct X {` | S2-S7 |
| G4 Static init | `-Wglobal-constructors` listing per TU; no initializer reads another TU's object (only loggers, `lvgl_mutex`, `audio_bytes`, `rd_pin`, the adapter have dynamic init) | all |
| G5 Host L1 | S1: golden **action-trace** test: for every (input, sample) of the reviewed table, the ordered `DriveView` calls equal the trace recorded from today's `drive_perform` on host (stubbed lv/rtps); the existing oracle (DRV-*) still passes | S1, S5 |
| G6 Init-order log | B2 capture, timestamps stripped: the ordered list of `I/W/E (tag)` lines up to `Got IP` equals the baseline's (allow the documented nondeterministic lines) | every task lift, S3 |
| G7 Board | B0-B5 + B4b PASS; B5 is the drive/lock wiring; B4b the PIN pad and Seat walk | every step |
| G8 Stack and cadence | B3 `compare_selftest.py`: `mem.stk_lvgl`, `mem.stk_adc` ≥ baseline (inlining changes frames), ADC cadence and XYTwist p99 in band | S1, every task lift |
| G9 Same task, same call site | L0 grep (§3.2 of refactor.md) updated to the new paths: gate written only from the UI side until T-H4a, publish only from control; no blocking lock on the ADC path | S1, T-H4a |
| G10 Task table | a script compares each `espp::Task` config (name, prio, core, stack) with the baseline table | task lifts |

## 6. Risks and effort

| Risk | Where | Mitigation |
| --- | --- | --- |
| Static-init order across TUs | `lvgl_mutex` (dynamic init), loggers, `rd_pin`, adapter instance | instances stay in the main unit until their island owns them; G4 |
| ODR / silent duplicate types | types and unscoped enums (`kRefused*`, `NavDest`) shared by fragments | one header per type, `enum class` when moved; G3 |
| `static` in a header | helpers made `static` in a header give each TU a copy | headers declare non-static functions in a namespace; G2 |
| Lambda lifetimes | `[&]` captures of app_main locals (`tab5`, `logger`, `label`, `line0/1`, `adc`, `channels`, `bg`) valid only because app_main never returns; early `return`s destroy `lv_task` | owners keep app_main scope in the refactor; changing that is a behaviour commit |
| LVGL lock assumptions | functions "called under `lvgl_mutex`" (set_locked's successors, RTPS callbacks, `brightness_step`) | port methods documented "UI task"; no new lock sites; G9 |
| Ratchet blocks lifted tasks | the espp cv `unique_lock` counts as `locks`, `fmt::print` as `log_direct`, in a new path | a `fw_core` task-loop helper (CS-OWN-10) or an owner-approved ratchet allowance first |
| Task priorities / defaults | "Read ADC" runs on espp defaults (prio 0, unpinned, H11) | copy the config literally; G10; raising it is H11's fix, not this |
| Inlining and timing | static→external calls on the ADC and render paths | G8 bands; ADC path calls stay in one TU until T-H4a |
| Safety review | S1 touches the drive path (draft, Q4) | S1 rides `dev_ai_refactor_drive`; merged only with the table review |

Effort: S0 2 h; S1 1 d (adapter 3 h, port 1 h, golden trace 3 h, board 1 h); S2-S4 2.5 d;
S5 2 d; S6 5 d; task lifts 3 d (lv_task 0.5, housekeeping 0.5, ADC/control 1, T-H4a 0.5,
adapters 0.5); view-init moves inside S4-S6. Total ≈ 14 agent-days, plus one board run per step.
