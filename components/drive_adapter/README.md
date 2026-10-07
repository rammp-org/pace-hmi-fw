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
| The firmware's port, `MainDriveView` (the LVGL and rtps_comms calls) and the one instance | `main/frag_drive.inc` |
| The adapter's own contract (DAD-001..006) | `test/` |
| The goldens: the drive code before the move vs after, at the port and at the lv_/rtps boundary (GLD-001..004) | `tests/host/drive_golden` |

## Model

`input(in)`: read the clock (`now_us`), sample the port (`sample`), build the session's `Env`
(each deadline against that `now`), `step`, then perform each action in order. `tick()`: the
same for TICK_FOLLOW; then one more clock read and sample shared by TICK_EXIT_DUE, TABLE.md U3's
locked exit deadline, TICK_WARN_DUE and TICK_GIVEUP_DUE (two Envs per tick, DRV-022).
`exit_hold_done()` while locked is TABLE.md U3, kept as it was and outside the table (parked
for the owner). The adapter keeps the deadlines (`wait_warn`, `wait_until`, `exit_until`), the
exit latches, the time of the last ask and the DriveCommand request; the session keeps the
phase and its hidden variables.

## The port (`DrivePort`)

Contract for every method: **called on the LVGL task (`lv_task`) with `lvgl_mutex` held**,
from inside one of the adapter's inputs; it must not call back into the adapter (a call that
does is dropped and logged, see REQ-DAD-03).

| Method | Action(s) | MainDriveView does |
| --- | --- | --- |
| `sample()` | every input | `rtps_link_subject`, `mib_state_subject`, `lv_screen_active()`, `nav_menu_open`, in that order |
| `now_us()` | every input; SEND_ENABLE; ARM_EXIT_DEADLINE; U3 | `esp_timer_get_time()` |
| `publish(enable)` | SEND_ENABLE, SEND_DISABLE, PUBLISH_DRIVE | `rtps_comms_publish_drive(request, drive_profile_published)` (result ignored: H6) |
| `ring_wait()`, `ring_rest()` | RING_WAIT, RING_REST | `lock_visual_wait()`, `lock_visual_rest()` |
| `lock_open_visual()` | LOCK_OPEN_VISUAL | ring full, shackle up, STRONG_CLICK |
| `unlock_timer_start/cancel/forget()` | START_UNLOCK_TIMER, CANCEL_UNLOCK_TIMER, UNLOCK_TIMER_DONE | `unlock_timer_start()`, `unlock_timer_cancel()`, `unlock_advance_timer = nullptr` |
| `set_locked(locked)` | SET_LOCKED, SET_UNLOCKED | `lv_subject_set_int(&locked_subject, 1/0)` |
| `gate_update()` | GATE_UPDATE | `nav_update_stick_gate()` |
| `menu_on_arrival(open)` | OPEN/CLEAR_MENU_ON_ARRIVAL | `nav_menu_on_arrival = open` |
| `go_locked_screen()`, `go_drive_screen()`, `nav_home()` | GO_LOCKED_SCREEN, GO_DRIVE_SCREEN, NAV_HOME | `locked_screen_go()`, fade to Drive over kUnlockDissolveMs, `nav_home()` |
| `show_banner(banner)` | SHOW_* | `entry_refused_show(kRefused*, dwell)` |
| `refusal_feedback()` | REFUSAL_FEEDBACK | `refusal_feedback()` |

## Requirements

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-DAD-01 | Driven by the same inputs, the adapter (through MainDriveView) makes the same port calls, clock reads, samples and lv_/rtps calls, in the same order, as the main.cpp drive code before the move | GLD-001, GLD-002, GLD-004 (GLD-003: the scenarios take all 41 rows) |
| REQ-DAD-02 | A new adapter is LOCKED with DISABLE as the request and calls nothing until an input | DAD-001 |
| REQ-DAD-03 | An input that arrives while another is being performed is dropped and logged; the one in progress completes and the adapter stays usable | DAD-002 |
| REQ-DAD-04 | The warn and give-up deadlines count from the ask by the configured windows, which default to the table's kDriveAnswer (750 ms) and kDriveWait (2 s) | DAD-003, DAD-004 |
| REQ-DAD-05 | A corrupted input performs the session's safe state through the port and is logged | DAD-005 |
| REQ-DAD-06 | The exit hold while locked (TABLE.md U3) sends DISABLE, and its deadline raises EXIT_REFUSED once | DAD-006 |

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
