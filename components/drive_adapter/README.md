# drive_adapter

The drive session's adapter on the UI task: it samples what a decision needs, hands the session
(`components/drive_session`) one input, and performs the actions the session returns, in order,
through a port. Namespace `hmi::drive_adapter`. **Safety-relevant** (CS-SAF-01): it sends the
DriveCommand requests and performs the relocks the session decides.

Status: **draft, refactor only** (2026-10-06, `dev_ai_refactor_drive`). Moved out of the main.cpp
unit (`main/frag_drive.inc`) with no behaviour change: app-main-shrink.md S1 as amended by V4
(template over the port, `DrivePort`, actions in a local array, re-entry checked), V5 (two
goldens), V6 (construction) and V9 (contracts). The owner approved Phase A on the drive draft
(hazard-fixes.md §7, "Phase A": approved to start on the draft branches); it merges only with
the drive table review (CS-SAF-05, two approvals; D1: the owner is the only approver for now).

| Part | File |
| --- | --- |
| `DriveAdapter<View>`, the `DrivePort` concept, `DriveSample`, `DriveBanner` | `include/drive_adapter.hpp` |
| The firmware's port, `hmi::ui::DrivePort<DriveUi>` (the LVGL and rtps_comms calls; moved from `main/frag_drive.inc`'s MainDriveView) | `components/drive_ui/include/drive_ui/drive_port.hpp` |
| The one instance | `main/main.cpp` |
| The Drive notice (`DriveNotice`, `drive_notice()`), its own header so a port can name it | `include/drive_notice.hpp` |
| The adapter's own contract (DAD-001..005, DAD-007..011) | `test/` |
| The goldens: the hazard fix C1's hand-written scenarios at the port (GLD-101..116); GLD-001/002/004, which pinned the move, are retired and their logs kept as history | `tests/host/drive_golden` |

## Model

`input(in)`: read the clock (`now_us`), sample the port (`sample`), build the session's `Env`
(each deadline against that `now`), `step`, then perform each action in order. `tick()`: the
same for TICK_FOLLOW (`TICK_SEQUENCE[0]`); then one more clock read and sample shared by the
other sub-steps of `TICK_SEQUENCE`, in its order (TICK_EXIT_DUE, TICK_STOP_FAULT_DUE,
TICK_STOP_RESEND, TICK_WARN_DUE, TICK_GIVEUP_DUE: two Envs per tick, DRV-116).
`exit_hold_done()` is a plain input in every phase (C1: rows 42-43 replace TABLE.md U3). After
every input and tick the Drive notice (`drive_notice`: MCB did not stop > Stopping > the stick's
hold reason in its order > none) is handed to the port once per change. The adapter keeps the
deadlines (`wait_warn`, `wait_until`, `exit_until`), the exit latches, the time of the last ask,
the DriveCommand request, and (C1) the stop timer (`stop_first_us`, ARM_STOP_TIMER /
CLEAR_STOP_FAULT), the last DISABLE published (the re-send counts from it), the publish failures
and the stop faults; the session keeps the phase and its hidden variables.

## The port (`DrivePort`)

Contract for every method: **called on the LVGL task (`lv_task`) with `lvgl_mutex` held**,
from inside one of the adapter's inputs; it must not call back into the adapter (a call that
does is dropped and logged, see REQ-DAD-03).

| Method | Action(s) | DrivePort (drive_ui) does |
| --- | --- | --- |
| `sample()` | every input | the link live (`rtps_comms_link_state`, REQ-UI-18), `mib_state_subject`, `lv_screen_active()`, `nav_menu_open`, `joystick_cal_running()`, the stick's hold reason, in that order |
| `now_us()` | every input; SEND_ENABLE; ARM_EXIT_DEADLINE | `esp_timer_get_time()` |
| `publish(enable)` | SEND_ENABLE, SEND_DISABLE, PUBLISH_DRIVE | `rtps_comms_publish_drive(request, drive_profile_published)`; returns its result, which the adapter does not use yet (H6) |
| `ring_wait()`, `ring_rest()` | RING_WAIT, RING_REST | `lock_visual_wait()`, `lock_visual_rest()` |
| `lock_open_visual()` | LOCK_OPEN_VISUAL | ring full, shackle up, STRONG_CLICK |
| `unlock_timer_start/cancel/forget()` | START_UNLOCK_TIMER, CANCEL_UNLOCK_TIMER, UNLOCK_TIMER_DONE | `unlock_timer_start()`, `unlock_timer_cancel()`, `unlock_advance_timer = nullptr` |
| `set_locked(locked)` | SET_LOCKED, SET_UNLOCKED | `lv_subject_set_int(&locked_subject, 1/0)` |
| `gate_update()` | GATE_UPDATE | `nav_update_stick_gate()` |
| `menu_on_arrival(open)` | OPEN/CLEAR_MENU_ON_ARRIVAL | `nav_menu_on_arrival = open` |
| `go_locked_screen()`, `go_drive_screen()`, `nav_home()` | GO_LOCKED_SCREEN, GO_DRIVE_SCREEN, NAV_HOME | `locked_screen_go()`, fade to Drive over kUnlockDissolveMs, `nav_home()` |
| `show_banner(banner)` | SHOW_* | `entry_refused_show(kRefused*, dwell)` |
| `refusal_feedback()` | REFUSAL_FEEDBACK | `refusal_feedback()` |
| `show_notice(notice)` | after every input and tick, on a change | the Drive screen's notice slot (hmi_ui's DriveNoticeView) |

## Requirements

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-DAD-01 | RETIRED 2026-10-08, superseded by REQ-DAD-07. Driven by the same inputs, the adapter (through MainDriveView) makes the same port calls, clock reads, samples and lv_/rtps calls, in the same order, as the main.cpp drive code before the move | – |
| REQ-DAD-02 | A new adapter is LOCKED with DISABLE as the request and calls nothing until an input | DAD-001 |
| REQ-DAD-03 | An input that arrives while another is being performed is dropped and logged; the one in progress completes and the adapter stays usable | DAD-002 |
| REQ-DAD-04 | The warn and give-up deadlines count from the ask by the configured windows, which default to the table's kDriveAnswer (750 ms) and kDriveWait (2 s) | DAD-003, DAD-004 |
| REQ-DAD-05 | A corrupted input performs the session's safe state through the port and is logged | DAD-005 |
| REQ-DAD-06 | RETIRED 2026-10-08, superseded by REQ-DRV-25. The exit hold while locked (TABLE.md U3) sends DISABLE, and its deadline raises EXIT_REFUSED once | – |
| REQ-DAD-07 | Driven by the C1 golden scenarios, the adapter makes the port calls hazard-c1-spec.md §5.2 writes | GLD-101..114 (GLD-115: the scenarios take every row) |
| REQ-DAD-08 | The stop timer starts at the first stop of an exit and is not reset by a repeated stop; the last-DISABLE time is every DISABLE published; the Env's stop and re-send guards follow hazard-c1-spec.md §2.1 exactly at their boundaries | DAD-007, DAD-008, DAD-011 |
| REQ-DAD-09 | Every DriveCommand publish result is checked; a failure is counted and logged at most once a second, and the re-send schedule does not change | DAD-009 |
| REQ-DAD-10 | After every input and tick, the Drive notice (MCB did not stop > Stopping > the hold reason in its order > none) is handed to the port once per change | DAD-010 |

## Tasks

None. Every call runs on its caller's task, the LVGL task, with `lvgl_mutex` held. Not
thread-safe; one owner. The request is kept in a `std::atomic<bool>` as before the move.

## Construction (V6)

The instance is built at namespace scope in the main.cpp unit, where `drive_state` was, so the
static-initialisation order is unchanged. Its constructor builds only an `espp::Logger` (no
hardware, LVGL, flash or calibration); `MainDriveView` is stateless. Nothing is allocated after
construction (CS-SAF-04), the fault paths included.

## Dependencies

`drive_session` (the session and its table), espp `logger`. No LVGL, ESP-IDF or FreeRTOS calls.

## Known gaps

- The action families keep `default:` for the actions they do not own, as in the reviewed
  frag_drive.inc split (8ea169d), so `-Wswitch-enum` (CS-TYP-06) is not applied to this header.
- clang-tidy (60-line functions, `.clang-tidy`) runs only via a clang-toolchain `idf.py
  clang-check` (hazard-fixes.md §8); the longest function here is under 40 lines.
