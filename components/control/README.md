# control

The control island: the "Read ADC" task that reads the joystick and runs its stick pipeline
(CS-LAY-02). Namespace `hmi::control`.

The control island of [docs/plans/app-main-shrink.md](../../docs/plans/app-main-shrink.md) §3:
today's "Read ADC" task, lifted out of `app_main` clean then move (V2). The cycle is that task's
code; the ADC drivers, the stick and the twist's lowpass, which were app_main's locals and
function-local statics, are members.

| File | What |
| --- | --- |
| `include/control/stick_island.hpp` | `StickIsland<Stick, Io, kCycle>`: brings the continuous ADC (X/Y) and the twist's oneshot ADC up, builds the stick on its calibration (`start`), and every `kCycle.period_ms` reads the three pots and runs one `Stick::cycle` on them, then `Io::note_cycle`. `Cycle`: the period and the twist read, a template argument so they stay compile-time constants. Its private `read_twist_mv`, the twist's oversampled oneshot read, is out of line on purpose (its comment says why) |
| `include/control/motion_guard.hpp` | Hazard fix C4 ([hazard-c4-spec.md](../../docs/plans/hazard-c4-spec.md)): `MotionGuard`, the ADC task's own check of the UI heartbeat, the link, the MibStatus age and the MCB state; its constants (`UI_HEARTBEAT_MAX_AGE_MS` 200, `MIB_STATUS_MAX_AGE_MS` 2000), the `GuardReason` verdict, the stale latch (`StampWatch`), the shared 32-bit stamps and atomics (`StampCell`, `GuardSources`, `GuardTelemetry`) and the ordered load (`load_guard_inputs`). Pure C++, time as a parameter |
| `include/control/windowed_adc.hpp` | `WindowedContinuousAdc`: espp's `ContinuousAdc` (vendored, `components/espp_adc`) plus `get_windows(configs)`, each channel's newest window (mean, max, sequence) from one publish, read lock-free from `components/adc_window` (hazard fix C2, REQ-CTL-16). Not used by the firmware yet |
| `include/control/cycle.hpp` | `ControlCycle<Watchdog, Clock>`: one ADC cycle after the reads, in C4's order: (first cycle) subscribe to the task watchdog, evaluate the guard once, hand the verdict to the Io, one `Stick::cycle`, `Io::note_cycle`, reset the watchdog. No espp or IDF type, so the host tests drive it |

**Status (2026-10-08).** `motion_guard.hpp` and `cycle.hpp` are C4's first commit: tested on
the host, not used by the firmware yet. Wiring the verdict into C1's output permit (condition
2), the UI heartbeat and MibStatus writers, the task row and the task watchdog are C4's later
commits, after C1 and C3 merge (hazard-decisions.md §4).

Header-only. `StickIsland` is a template on main's stick (`StickSlot`: the pipeline, or the
bench injection in front of it) and main's `AdcStickIo`, so it is instantiated in main's unit
and the ADC path's calls stay in one translation unit, as before the lift. The task's frames
are guarded by `-fstack-usage` at each change (at the lift: the invoker 208 B, `read_twist_mv`
240 B, as in app_main) and on the board by B3's `mem.stk_adc`.

## Requirements

The Read ADC task's read, pipeline, gate and publish stay on this task (app-main-shrink V10).
The pipeline's own requirements are `components/stick`'s. The rows below are hazard fix C4's
(hazard-c4-spec.md §6, reconciled in hazard-fixes.md §10); test IDs `CTL-0nn` are cases of the
host app `test/` (`tests/manifest.d/control.yaml`, L1-CTL). Cases CTL-012, 013, 015, 016, 020
and 021 run the guard through C1's output permit and come with the wiring commit (C4 commit
3); B-steps are bench checks (hazard-c4-spec.md §8). REQ-CTL-15 and 16 are hazard fix C2's
(hazard-c2-spec.md §7.2); CTL-022 and 023 come with C2's island commit.

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-CTL-01 | Every ADC cycle evaluates the guard once, after the three reads and before the pipeline, on valid, invalid and calibrating cycles alike; that cycle's gate uses that verdict. | CTL-013, CTL-014, CTL-019 |
| REQ-CTL-02 | A cycle whose verdict is not OK withholds C1's output permit (condition 2): the command is the literal 0 of REQ-STK-10, whatever the other conditions say. XYTwist is still published on every valid cycle (neutral, not silence); the button bit is unchanged. | CTL-012, CTL-015 |
| REQ-CTL-03 | `UI_STALE`: the UI heartbeat's age is ≥ `UI_HEARTBEAT_MAX_AGE_MS` (200), or it was never written. | CTL-002, CTL-004 |
| REQ-CTL-04 | `MIB_STALE`: the newest MibStatus's age is ≥ `MIB_STATUS_MAX_AGE_MS` (2000), or none arrived since boot. `rtps_comms` stamps it on the receive task, state first, before any handler or lock. | CTL-003, CTL-004, CTL-026 |
| REQ-CTL-05 | `LINK_DOWN`: net failed, no link, or no IP. With REQ-CTL-04 this is "not CONNECTED". | CTL-005 |
| REQ-CTL-06 | `MCB_NOT_ENABLED`: the newest MibStatus's `systemState` is not ENABLED, out-of-range values included. | CTL-006 |
| REQ-CTL-07 | Ages are `uint32` ms modulo 2^32. A source seen stale stays stale until its stamp changes; a stamp ahead of the clock is stale. | CTL-008, CTL-009, CTL-010 |
| REQ-CTL-08 | `WDT_MISSING`: if the ADC or UI task failed to subscribe to the TWDT, the verdict is never OK. | CTL-011, CTL-019 |
| REQ-CTL-09 | The guard reads none of: `drive_request`, `DISABLE_PENDING`, the exit hold, the burger key, the drive phase (G4). | CTL-016, review |
| REQ-CTL-10 | The reported reason follows the order e, a, b, c, d; onset counters per reason and `ui_stalls_drive` only go up; `ui_age_max_ms` is the largest heartbeat age seen since the first heartbeat. All are written by the ADC task only. | CTL-007, CTL-017, CTL-025 |
| REQ-CTL-11 | The guard and the gate allocate nothing, log nothing, take no lock and make no indirect call; every shared value is an atomic with `is_always_lock_free` asserted. | CTL-018, CTL-026, static_assert, SU, review |
| REQ-CTL-12 | The ADC task subscribes to the TWDT at its first cycle and resets it at the end of every cycle, valid or not. | CTL-019, B5j |
| REQ-CTL-13 | `Read ADC` runs at priority 21, pinned to core 0, with a 6144 B stack. | G10, B3 |
| REQ-CTL-14 | After any non-OK verdict, output resumes only through C1's neutral latch: the non-OK verdict clears it, and it sets again only after `kNeutralHold` of neutral cycles (REQ-STK-12). | CTL-020, CTL-021 |
| REQ-CTL-15 | Each cycle the island reads both X/Y windows (mean, max, sequence) in one call, the twist's reads (mean, max, count), samples `calibrating` once, steps the monitor once, then runs the pipeline with the same `calibrating`. On valid and invalid cycles alike. (Hazard fix C2; C2 commit 4.) | CTL-022 |
| REQ-CTL-16 | The vendored `ContinuousAdc` keeps espp's mean per window and adds, per channel, the window's largest conversion in mV and a `uint32` sequence that advances only for a window holding at least one conversion of that channel. Its `README.md` lists every change from espp. (Hazard fix C2.) | CTL-024, CTL-027 |

How the guard reads the spec where it leaves a choice (C4 §2, §3):

- **Never written.** Every stamp starts at `kStampInitial` (0), and the guard starts with each
  source seen stale at that stamp, so a source stays stale until its stamp first changes. A
  writer whose first stamp happens to be 0 is seen stale until its next store (conservative).
- **Onsets** (`trips`, `ui_stalls_drive`): a condition's onset is a cycle where it holds and did
  not hold the cycle before, counted per condition whatever reason is reported. Before the first
  cycle every condition counts as holding, so a source stale from boot is no trip until it has
  been fresh once.
- **Before the first cycle** the guard's reason and the published reason are `WDT_MISSING`,
  never OK.

The owner accepted these choices on 2026-10-08.

One difference from hazard-c2-spec.md §10.3: the X/Y windows are read without a lock (a
sequence lock in `components/adc_window`, CTL-027), not by one take of espp's `data_mutex_`. A
lock in our code trips the L0 ratchet, and the owner's rule (2026-10-08) is to fix the code, not
the check. The stick task then takes no lock to read the ADC at all.

## Tasks

| Task | Name | Stack | Priority | Core | Period |
| --- | --- | --- | --- | --- | --- |
| the island's own | `Read ADC` | 4096 B | 0 (runs at IDF's pthread default, 5) | -1 (pinned by its first FPU use: core 0 on the board) | 33 ms |

`Config::task` is that row as a literal, espp's defaults written out, copied from the code it
replaced, so the G10 task dump (`tools/guards/baselines/tasks.json`) is unchanged. The
topology's `control` row (`components/topology`, CS-CON-02) is the target design: another
name, priority, core and stack (hazard H11, C4). Taking `Config::task` from
`topo.task_config(...)` is a later step, made once the owner has reviewed the TASKS rows; it
changes the task's behaviour, so it comes with its own G10 baseline. The continuous ADC's own
task is espp's (`ContinuousAdc`, priority 5).

## Dependencies

espp `adc`, vendored as `components/espp_adc` (continuous and oneshot), `adc_window` (the window
bookkeeping), `filters` (the lowpass), `logger`, `task`;
`hmi_rtps_spec` (the MibStatus timeout and the ENABLED state the guard checks against). The
stick pipeline (`components/stick`) and main's Io come in as template arguments.

## Tests

`test/` is the host L1 app L1-CTL (`make test`, `make coverage`): the guard on fake time, and
`ControlCycle` with the real stick pipeline and fakes for its Io, the watchdog and the clock,
under the host allocation guard. Coverage of `motion_guard.hpp` and `cycle.hpp`: 100 % lines
and branches without the sanitizers (`make coverage SAN_FLAGS= BUILD_DIR=$HOME/hmi-build/control_nosan`);
with ASan/UBSan the branches gcov misses are UBSan's own checks.

## Ratchet

Clean: no transfer grants. The task's wait is the espp Task idiom (CS-OWN-08).
