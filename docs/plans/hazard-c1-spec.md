# Spec: hazard fix C1 (the MCB is the authority on driving)

Status: **spec for the owner's approval** (2026-10-08). No code, test data, table or golden
has been changed. A different agent implements it after approval (CS-SAF-05; D1: the owner is
the only approver for now). Base: `dev_refactor` + the owner's decisions, 4a20866.

Inputs: `docs/plans/hazard-fixes.md` §9 (M1, G1-G5, D4), which supersedes C1 v2 (§4);
hazards H1, H5, H6, H10 (`docs/plans/refactor.md` §1); the drive table
(`components/drive_session`), its adapter (`components/drive_adapter`), the port
(`components/hmi_ui/include/hmi_ui/drive_port.hpp`) and the stick pipeline
(`components/stick`).

Row numbers are today's `TRANSITIONS` index + 1 (TABLE.md §2). New rows are appended (42-49),
so every existing row keeps its number.

## 1. Exec summary

What changes for the user:

- The Tab5 follows the MCB. MCB says ENABLED: the Tab5 goes to Drive by itself, from any
  screen. MCB stops being ENABLED, or goes silent for 2 s: the Tab5 goes back to Locked.
- After entering Drive, the stick does nothing until it has been centred for 0.3 s. The Drive
  screen says "Centre the joystick to drive" meanwhile.
- Stop (exit hold or burger key): the Tab5 asks the MCB to stop every 0.25 s and shows
  "Stopping". After 5 s it shows the fault "MCB did not stop" and keeps asking once a second.
  It stays on Drive, and the stick keeps driving, until the MCB stops.
- No valid calibration: the Drive screen says "The joystick must be calibrated first" and the
  stick does nothing.
- A calibration can only be started while locked. The Tab5 never changes screen during one.
- Every return to Locked sends one DISABLE (H5).
- C1 and C3 are implemented and merged together (C3 §0): C1's POST hook alone would block the
  stick in every normal build (§3.2).

What the HMI does in each situation:

| MCB / link | HMI state | HMI does |
| --- | --- | --- |
| ENABLED, fresh (link CONNECTED, MibStatus < 2 s) | Locked, no calibration running, Boot screen gone | Unlocks without a request; Drive screen 1 s later. Stick output 0 until centred ≥ 300 ms, POST passed, calibrated, no stick fault |
| ENABLED, fresh | Locked, calibration running | Stays on its screen, output 0. Enters Drive when the calibration ends, if still ENABLED |
| IDLE, ERROR or INITIALIZING, fresh | Unlocked | Locked screen on the next 250 ms tick. Gate shut. One DISABLE. Banner "stopped" unless the user asked |
| MibStatus stale (≥ 2 s) or link down | Unlocked | Same, banner "lost". One DISABLE (best effort) |
| Back to ENABLED, fresh, after a relock | Locked | Enters Drive again (M1); stick output 0 until centred |
| User stop; MCB stops | Drive | DISABLE at once, re-sent each tick. Locked when the MCB reports not ENABLED. Menu if the burger key asked |
| User stop; MCB stays ENABLED | Drive | Stays on Drive, **stick keeps driving**. DISABLE every 250 ms, "Stopping". At 5 s: "MCB did not stop", DISABLE every 1 s, until the MCB leaves ENABLED or the link goes stale |
| Any | Drive, never calibrated | Output 0, "The joystick must be calibrated first" |
| Any | Drive, POST not passed (C3) or stick fault (C2) | Output 0, the reason shown |
| ENABLED, fresh | Drive, profile tapped | DriveCommand ENABLE with the new profile |
| Not ENABLED (as last seen) | Drive, profile tapped | Nothing sent; the next tick relocks |

**Owner-accepted risk, stated plainly.** The HMI's stop is a request. If the MCB never gets the
DISABLE, or ignores it, the chair keeps driving: the HMI stays on Drive, the stick keeps
commanding motion, and only the MCB (or the user letting go of the stick) stops it. The HMI also
enters Drive whenever the MCB says ENABLED, even if nobody asked on this HMI (H1, accepted by
design). Residual, smaller: a profile tap in the ≤ ~0.75 s between the MCB stopping and the HMI
seeing it re-sends ENABLE (§6.4, Q10).

## 2. The drive-table change

### 2.1 New vocabulary (`drive_session_types.hpp`)

Appended at the end of each enum, so existing values keep their numbers.

| Kind | New | Meaning |
| --- | --- | --- |
| Input | `TICK_STOP_FAULT_DUE` | tick sub-step: has the 5 s stop window passed |
| Input | `TICK_STOP_RESEND` | tick sub-step: is a DISABLE re-send due |
| Guard, env | `CALIBRATING` | `joystick_cal_running()` at the sample |
| Guard, env | `ON_BOOT_SCREEN` | the screen is BootScreen (`Screen::BOOT`, already in `Env`) |
| Guard, env | `STOP_FAULT_ELAPSED` | stop timer armed and now ≥ first stop + `kStopFaultAfter` |
| Guard, env | `RESEND_FAST_DUE` | now ≥ last DISABLE + `kStopResend` − `kStopResendSlack` |
| Guard, env | `RESEND_SLOW_DUE` | now ≥ last DISABLE + `kStopResendSlow` − `kStopResendSlack` (implies FAST) |
| Guard, hidden | `STOP_FAULT` | "MCB did not stop" has been raised for this stop |
| Action | `ARM_STOP_TIMER` | adapter: first stop time := now |
| Action | `RAISE_STOP_FAULT` | sets STOP_FAULT; logs the fault once; counts it |
| Action | `CLEAR_STOP_FAULT` | clears STOP_FAULT; adapter: first stop time := 0 |

`Env` gains `calibrating` (bool), `stop_fault_elapsed` (bool) and `resend` (`NOT_DUE`, `FAST`,
`SLOW`; SLOW sets both bits, so "SLOW without FAST" cannot be built). Counts: inputs 11 → 13,
guards 15 → 21 (env 10 → 15, hidden 5 → 6), actions 36 → 39, `kMaxActions` 11 → 12.

### 2.2 Named constants (D4), in `drive_session_table.hpp`

| Constant | Value | Note |
| --- | --- | --- |
| `kStopResend` | 250 ms | `static_assert(kStopResend == kTickPeriod)`: the re-send rides the tick |
| `kStopFaultAfter` | 5000 ms | counted from the first stop of this exit, not reset by a repeated stop |
| `kStopResendSlow` | 1000 ms | after the fault |
| `kStopResendSlack` | `kTickPeriod / 2` = 125 ms | tick jitter: FAST fires on every tick, SLOW on every 4th |
| `kMibStatusTimeout` | 2000 ms | exists; "fresh" is `age < 2000 ms` (rtps_comms) |
| `kNeutralHold` | 300 ms | in `components/stick` (§3) |

The stop constants and `stop_notice` (§2.6) go into the fingerprint.

### 2.3 TRANSITIONS, row by row

F1, F2, ASK and ADV are TABLE.md §2's action groups. `+X` = X true, `!X` = X false.

Changed rows:

| # | from, input | Old guard → new guard | Old actions → new actions | Why |
| --- | --- | --- | --- | --- |
| 1 | LOCKED, TICK_FOLLOW | `+DOK` → `+DOK !CALIBRATING !ON_BOOT_SCREEN` | F1 (unchanged) | M1 kept (H1 accepted); G5 no screen switch while calibrating; U4: no entry behind the Boot screen |
| 2 | ASKING, TICK_FOLLOW | same as row 1 | F1 (unchanged) | same |
| 3, 5 | UNLOCKING / DRIVING, TICK_FOLLOW | `+LINK !DOK` (unchanged) | F2 + SHOW_DRIVE_STOPPED → F2 + **SEND_DISABLE** + SHOW_DRIVE_STOPPED | M1 exit; H5: one DISABLE, request := DISABLE |
| 4, 6 | UNLOCKING / DRIVING, TICK_FOLLOW | `!DOK !LINK` (unchanged) | F2 + SHOW_DRIVE_LOST → F2 + **SEND_DISABLE** + SHOW_DRIVE_LOST | G2 (stale = not ENABLED); H5 |
| 7 | EXITING, TICK_FOLLOW | `+THEN_MENU !DOK` (unchanged) | F2 (menu) → F2 (menu) + **SEND_DISABLE**, **CLEAR_STOP_FAULT** | every relock sends DISABLE; the stop ends |
| 8 | EXITING, TICK_FOLLOW | `!DOK !THEN_MENU` (unchanged) | F2 → F2 + **SEND_DISABLE**, **CLEAR_STOP_FAULT** | same; link loss ends the stop (Q3) |
| 9 | EXIT_REFUSED, TICK_FOLLOW | `!DOK` (unchanged) | F2 → F2 + **SEND_DISABLE**, **CLEAR_STOP_FAULT** | same |
| 21, 22 | UNLOCKING / DRIVING, EXIT_HOLD_DONE | — | ASK(false) → ASK(false) + **ARM_STOP_TIMER** | G4: the 5 s counts from the first stop |
| 25, 26 | UNLOCKING / DRIVING, MENU_KEY_DRIVE | — | ASK(true) → ASK(true) + **ARM_STOP_TIMER** | same |
| 31, 32 | UNLOCKING / DRIVING, PROFILE_CLICK | — → `+DOK` | PUBLISH_DRIVE → **SEND_ENABLE** | H5: never re-send a stale request; after an unasked entry the request is DISABLE, so a profile tap must not send it (it would stop the chair) |

In rows 3-9, SEND_DISABLE comes right after GATE_UPDATE (the gate shuts first), before the
banner. CLEAR_STOP_FAULT is last. Rows 3-9 keep their banners, menu flags and order otherwise.

Added rows (appended):

| # | from | input | guard | to | actions | Why |
| --- | --- | --- | --- | --- | --- | --- |
| 42 | LOCKED | EXIT_HOLD_DONE | — | LOCKED | SEND_DISABLE, CLEAR_GIVEUP | replaces the adapter's U3 path: a stop while locked sends one DISABLE, no latch, no deadline, no banner |
| 43 | ASKING | EXIT_HOLD_DONE | — | LOCKED | SEND_DISABLE, CLEAR_WARN, CLEAR_GIVEUP, RING_REST | the user's stop wins over their own ask |
| 44 | EXITING | TICK_STOP_FAULT_DUE | `+STOP_FAULT_ELAPSED !STOP_FAULT` | EXITING | RAISE_STOP_FAULT | G4: "MCB did not stop" at 5 s |
| 45 | EXIT_REFUSED | TICK_STOP_FAULT_DUE | `+STOP_FAULT_ELAPSED !STOP_FAULT` | EXIT_REFUSED | RAISE_STOP_FAULT | same |
| 46 | EXITING | TICK_STOP_RESEND | `+RESEND_FAST_DUE !STOP_FAULT` | EXITING | SEND_DISABLE | G4: every 250 ms |
| 47 | EXITING | TICK_STOP_RESEND | `+RESEND_SLOW_DUE +STOP_FAULT` | EXITING | SEND_DISABLE | G4: 1 Hz after the fault |
| 48 | EXIT_REFUSED | TICK_STOP_RESEND | `+RESEND_FAST_DUE !STOP_FAULT` | EXIT_REFUSED | SEND_DISABLE | same as 46 |
| 49 | EXIT_REFUSED | TICK_STOP_RESEND | `+RESEND_SLOW_DUE +STOP_FAULT` | EXIT_REFUSED | SEND_DISABLE | same as 47 |

Removed rows: none. `kTransitionCount` 41 → 49.

The re-send never stops on its own: it runs while the phase is EXITING or EXIT_REFUSED, which
ends only when a relock row (7-9) fires, that is when the MCB is seen not ENABLED or the link
goes stale. That is G4's "while the MCB reports ENABLED".

### 2.4 Every existing row checked against M1 and G1-G5

| Rows | Verdict | Reason |
| --- | --- | --- |
| 1-2 | changed | above |
| 3-9 | changed | above |
| 10-11 (Seat, no MCB: home + refusal) | unchanged | need `!DOK`; no conflict with M1. Calibration is on the Joystick screen, so `ON_SEAT` and `CALIBRATING` never meet |
| 12-13 (ring rests after the warn) | unchanged | no screen change, nothing sent |
| 14 (exit deadline → EXIT_REFUSED, 2 s banner) | unchanged | the phase stays (rows 24, 28 depend on it). It no longer means "no re-send": rows 48-49 re-send from EXIT_REFUSED. Q8: keep the 2 s banner next to the persistent notice? |
| 15 (NOT_GRANTED) | unchanged | |
| 16-17 (give-up DISABLE) | unchanged | a late ENABLED after the give-up still unlocks (row 1): M1 |
| 18 (unlock hold → ENABLE) | unchanged | owner's text does not refuse it when uncalibrated or before POST (Q5) |
| 19-20 | unchanged | |
| 23, 24, 28 (stop again) | unchanged | send at once and re-arm the 750 ms deadline; the 5 s stop timer is kept (not re-armed) |
| 27 (burger key while EXITING: nothing) | unchanged | the re-send runs anyway |
| 29, 30, 33, 34 (profile tap: re-publish the request) | unchanged | with rows 3-9 clearing the request, LOCKED can only hold ENABLE while its own ask is outstanding (after rows 12-13, until the give-up); ASKING holds the user's ask; the exit phases hold DISABLE |
| 35-37 (advance timer → Drive screen) | unchanged | a calibration cannot run in these phases (§2.8) |
| 38-41 (refusal banners) | unchanged | |

G1 and G3 are not table guards: they live on the ADC task (§3), because they need the stick's
position every ADC cycle (33 ms wait, 35 ms measured). G2 is already in the table: `DRIVING_OK` and `MCB_READY` need
`LINK_CONNECTED`, and CONNECTED means a MibStatus less than 2 s old (rtps_comms). §2.9 makes the
sample exact.

### 2.5 Other table data

| Item | Change |
| --- | --- |
| `TICK_SEQUENCE` | TICK_FOLLOW, TICK_EXIT_DUE, **TICK_STOP_FAULT_DUE**, **TICK_STOP_RESEND**, TICK_WARN_DUE, TICK_GIVEUP_DUE. FOLLOW still runs on the Env at the tick's start; the five others share one Env sampled after FOLLOW's actions. FOLLOW first: a relock ends the stop before any re-send. EXIT_DUE before the stop steps: the phase they read is final. FAULT before RESEND: on the fault tick the rate switches to 1 Hz at once |
| `PHASE_INVARIANTS` | `STOP_FAULT` must be false in LOCKED, ASKING, UNLOCKING, DRIVING |
| `ACTION_EFFECTS` | RAISE_STOP_FAULT sets STOP_FAULT; CLEAR_STOP_FAULT clears it |
| `INPUT_PRECONDITIONS` | EXIT_HOLD_DONE: all phases (was unlocked only; U3 is now rows 42-43). The two new tick inputs: all phases, always |
| `SAFE_STATE_ACTIONS` | + CLEAR_STOP_FAULT (12 actions) |
| `HOLD_TRANSITIONS`, `UNLOCK_APPLIES`, `EXIT_APPLIES`, `HOLD_POLL_SEQUENCE` | unchanged |
| `stick_drives()` | unchanged |
| `stick_scale()` | removed: replaced by the stick's output permit (§3). REQ-DRV-20 retired |
| `GATE_TRIGGERS` | unchanged. The gate atomic still moves only at its five triggers. The new conditions (neutral, calibration, POST, stick fault) are read by the ADC task every cycle, so they need no trigger |
| `KNOWN_GAPS` | stays empty. EXIT_REFUSED leaves only on error (row 9): by design under G4 |
| Static checks | add: ARM_STOP_TIMER only on rows from UNLOCKING/DRIVING to EXITING; every row from an unlocked phase to LOCKED has SEND_DISABLE; SEND_ENABLE only in rows 18, 31, 32, and 31-32 need `+DOK`; every row from EXITING/EXIT_REFUSED to LOCKED has CLEAR_STOP_FAULT |

### 2.6 Stop notice (new pure function in the table header)

`stop_notice(phase, hidden)`: `MCB_DID_NOT_STOP` in EXITING or EXIT_REFUSED with STOP_FAULT;
`STOPPING` in EXITING or EXIT_REFUSED without it; `NONE` otherwise. The adapter shows it (§2.7).
It clears at the relock; the fault stays in the log (Q7).

### 2.7 The adapter (`drive_adapter.hpp`)

- New state: `stop_first_us_` (ARM_STOP_TIMER, cleared by CLEAR_STOP_FAULT), `last_disable_us_`
  (set by every DISABLE published), `publish_failures_`, `stop_faults_`, the last notice.
- Env: `calibrating` from the sample; `stop_fault_elapsed` and `resend` from those times and
  `now` (§2.1).
- `tick()`: TICK_FOLLOW on Env 1, then the other five `TICK_SEQUENCE` steps on Env 2, in order.
  The U3 code (`exit_ask_while_locked`, `exit_due_while_locked`) is removed: rows 42-43 replace
  it. `exit_hold_done()` becomes a plain `input(EXIT_HOLD_DONE)`.
- Publish results are checked (H6): `DrivePort::publish` returns the bool from
  `rtps_comms_publish_drive`. A failure is counted and logged at most once a second. It does not
  change the re-send schedule: the next re-send is the retry.
- The Drive notice: after every input and tick, the adapter combines `stop_notice` with the
  stick's hold reason (§3.3) and calls `view.show_notice(n)` when it changed. Order: MCB did
  not stop > Stopping > the hold reason in §3.3's order > none.

### 2.8 Calibration and G5

- The calibrate hold applies only while locked (`locked` subject 1), on the Joystick screen,
  with no menu (hmi_ui, REQ-UI-16). Today it has no lock condition.
- With rows 1-2 needing `!CALIBRATING`, and only rows 1-2 leaving the locked phases, a
  calibration only ever runs in LOCKED or ASKING. So no row that loads a screen (3-9, 10-11,
  35-37) can fire during one. The oracle still enumerates CALIBRATING in every phase; TABLE.md
  records the precondition.
- Leaving the Joystick screen still cancels a run (as today). Nothing in the table can cause that
  during a run any more.

### 2.9 The port (`drive_port.hpp`)

- `sample()` adds `calibrating` and the stick's hold reason, and computes `link_connected` live
  (the same `rtps_comms_link_state()` the 250 ms poll uses), not from the `rtps_link` subject.
  Today an input between two polls can see a status up to 2.25 s old; after this, every
  decision sees < 2 s (G2).
- `publish(enable)` returns bool. New `show_notice(Notice)`.

### 2.10 Fingerprint

`TABLE_FINGERPRINT` changes (0xD8AAB0E61BE44A91 → a new value computed by the implementer, in
the same commit as the rows). The owner's approval of §2 is the approval of that value. The
stop constants and `stop_notice` are added to the hash.

TABLE.md is rewritten to match: §2 rows, §2.2 exclusions (U3 gone), §5 D4 diagram, §6 ways out,
§7 hazards (H1 accepted by M1; H5, H6 closed; H7 open), §10 (U3 closed, U4 closed by row 1-2's
guard).

## 3. Stick output permit (G1, G3, G5) on the ADC task

### 3.1 The rule

Every ADC cycle, the command is the mounted position × the speed scale only when all of these
hold; otherwise it is a literal (+0.0, +0.0, +0.0), not a multiply. This is the **one** permit
for all four fixes: C3, C4 and C2 fill their conditions here and add no other gate. The order
is the hold-reason order (§3.3).

1. the gate (`stick_drives`, unchanged);
2. the motion guard's verdict is OK (G2 on the ADC side, filled by C4: UI heartbeat, link,
   MibStatus age, MCB state);
3. no calibration running;
4. a measured calibration is in use (G5): loaded valid from flash, or completed this boot (Q5);
5. POST gate is PASS (G3, filled by C3);
6. stick health is not FAULT (G3, filled by C2);
7. stick health is not CHECK (filled by C2: the monitor is starting, or a sample was
   implausible and it has not recovered);
8. the neutral latch is set (G1).

The neutral latch: set once every valid cycle for at least `kNeutralHold` (300 ms, by the ADC
task's clock) had x = y = twist = 0.0 after mapping, that is inside the stick's own dead zones
(XY radius 0.10, twist ± 60 mV, REQ-STK-08). A NaN is never neutral. A non-neutral or invalid
cycle before 300 ms restarts the wait. Any of 1-7 failing clears the latch. So **every** change
from "held" to "allowed" (entering Drive, a menu closed, POST passing, a fault clearing) needs
the stick centred first. Once set, the latch stays while 1-7 hold. The stick button bit still
reaches XYTwist as today (STK-017). Neutral XYTwist keeps flowing while held.

### 3.2 The G3 hooks before C3 and C2 (flagged, Q4)

| Hook | Type | C1 value until its fix lands | Effect in C1 |
| --- | --- | --- | --- |
| Motion guard | the C4 verdict (`OK` or one of C4's reasons) | `OK` (not fitted) | **passes**: until C4, G2 acts through the table only (rows 3-6 shut the gate on the UI tick) |
| POST gate | `PostGate{NOT_RUN, PENDING, PASS, FAIL}` in an atomic | `NOT_RUN` | **blocks**: no stick output in a normal build until C3 wires POST. Drive notice "Start-up check not run". CS-SAF-03: no motion after a reset until POST passes |
| Stick health | `StickHealth{NOT_MONITORED, OK, CHECK, FAULT}` in an atomic | `NOT_MONITORED` | **passes**: no detector exists before C2 (as today; H3 stays open until C2). Logged once at boot: "stick fault detection not fitted (C2)". Only C2's monitor writes CHECK |

Only C3 writes the POST gate, only C4 the verdict and only C2 stick health. In C1 the only other
writer is the bench verb `PERMIT` (bench builds only, §5).

### 3.3 Hold reason

The ADC task stores the first failing condition each cycle in an atomic for the UI, in §3.1's
order: `GATE_SHUT`, `MOTION_GUARD`, `CALIBRATING`, `NOT_CALIBRATED`, `POST_NOT_PASSED`,
`STICK_FAULT`, `STICK_CHECK`, `CENTRE_FIRST`, or `NONE`. The Drive notice uses it (§2.7). This
is the only hold-reason list; C3, C4 and C2 use it as it stands.

Texts (all in `hmi_rtps_spec`, beside the existing warning texts; the owner approves the words,
Q9). Each text has one owner spec:

| Notice or hold reason | Text | Owner |
| --- | --- | --- |
| stop fault | "MCB did not stop" | C1 |
| stopping | "Stopping: waiting for the MCB" | C1 |
| `GATE_SHUT`, `CALIBRATING` | none (the Drive screen is not up, or the calibration screen is) | — |
| `MOTION_GUARD` | "Waiting for the MCB" | C4 |
| `NOT_CALIBRATED` | "The joystick must be calibrated first" (G5's words) | C1 |
| `POST_NOT_PASSED` | "Start-up check not run" for NOT_RUN; the blocking check's text otherwise | C1 (NOT_RUN), C3 §2.8 (the rest) |
| `STICK_FAULT` | "Joystick fault" until C2; C2's text from then | C2 (O11) |
| `STICK_CHECK` | "Checking the joystick" | C2 (O11) |
| `CENTRE_FIRST` | "Centre the joystick to drive" | C1 |

### 3.4 Where it lives

A pure class `hmi::stick::OutputPermit` (components/stick): inputs, the mounted position, valid
or not, and `now_ms`; out: allowed and the hold reason. `now_ms` is the ADC task's clock: a
`uint32_t` ms count from one clock function that `main` injects; durations are taken modulo
2^32. C4's guard and C2's monitor take the same clock (C4 §3.1). `StickPipeline::cycle` asks its Io
`output_permit(mounted)` where it asks `stick_drives()` today, and sends literal zeros when it is
false. Main's `AdcStickIo` owns the `OutputPermit` and calls it; `note_cycle(valid = false)`
restarts the neutral wait.

## 4. Requirements

Format: the README tables (`tests/reqmatrix.py`). A retired row stays, starts with
`RETIRED 2026-10-xx, superseded by REQ-...`, lists no tests, and no test may cite it.

### 4.1 Retired

| ID | Successor | Why |
| --- | --- | --- |
| REQ-DRV-02 | REQ-DRV-22 | entry gains the calibration and Boot-screen guards |
| REQ-DRV-03 | REQ-DRV-23 | "no DISABLE sent (H5)" is no longer true |
| REQ-DRV-05 | REQ-DRV-24 | "DISABLE not re-sent (H6)" is no longer true |
| REQ-DRV-09 | REQ-DRV-25 | stop timer; exit hold in every phase |
| REQ-DRV-10 | REQ-DRV-27 | stop timer on the burger key |
| REQ-DRV-11 | REQ-DRV-30 | profile tap no longer re-sends a stale request |
| REQ-DRV-18 | REQ-DRV-31 | safe state also clears the stop fault |
| REQ-DRV-20 | REQ-STK-11 | the scale moves to the stick's permit |
| REQ-DRV-21 | REQ-DRV-32 | six tick sub-steps |
| REQ-DAD-01 | REQ-DAD-07 | the move is done; the frozen goldens are retired (§5.3) |
| REQ-DAD-06 | REQ-DRV-25 | U3 is now table rows 42-43 |
| REQ-STK-04 | REQ-STK-10 | held output is a literal 0, not a multiply |

Unchanged: REQ-DRV-01, 04, 06, 07, 08, 12-17, 19; REQ-DAD-02..05; REQ-STK-01..03, 05..09.

### 4.2 New

| ID | Component | Requirement |
| --- | --- | --- |
| REQ-DRV-22 | drive_session | On a tick, LOCKED or ASKING with the MCB ENABLED on a CONNECTED link unlocks (F1), asked or not (M1), unless a calibration is running or the Boot screen is up (rows 1-2) |
| REQ-DRV-23 | drive_session | On a tick, an unlocked session whose MCB is not ENABLED on a CONNECTED link relocks: the menu-on-arrival flag is set when the burger key asked for the exit and cleared otherwise, Locked screen, gate updated, one DISABLE sent (the request is DISABLE after), and the banner DRIVE_STOPPED (link up) or DRIVE_LOST (link down) unless the user asked for the exit. From the exit phases it also clears the stop fault (rows 3-9) |
| REQ-DRV-24 | drive_session | On a tick, an exit not seen granted by its 750 ms deadline moves to EXIT_REFUSED with the exit-refused banner (row 14) |
| REQ-DRV-25 | drive_session | A completed exit hold sends DISABLE in every phase. From UNLOCKING or DRIVING: latch the exit, clear the menu flag, arm the exit deadline and the stop timer. From EXITING or EXIT_REFUSED: the same but the stop timer is kept. From LOCKED: clear the give-up. From ASKING: withdraw the ask (warn, give-up cleared), ring at rest, LOCKED (rows 21-24, 42, 43) |
| REQ-DRV-26 | drive_session | In EXITING and EXIT_REFUSED, each tick re-sends DISABLE when at least `kStopResend − kStopResendSlack` has passed since the last DISABLE; after the stop fault, when at least `kStopResendSlow − kStopResendSlack` has (rows 46-49). It stops only when the session leaves those phases |
| REQ-DRV-27 | drive_session | The burger key on Drive, unlocked, asks to stop and then open the menu, and from UNLOCKING or DRIVING arms the stop timer; while an exit deadline is armed it does nothing (rows 25-28) |
| REQ-DRV-28 | drive_session | The stop fault is raised once, on the first tick at least `kStopFaultAfter` after the first stop of the exit, and cleared by the relock (rows 7-9, 44-45) |
| REQ-DRV-29 | drive_session | `stop_notice` is MCB_DID_NOT_STOP in an exit phase with the stop fault, STOPPING in an exit phase without it, NONE otherwise |
| REQ-DRV-30 | drive_session | A profile tap re-publishes the request as it stands in LOCKED, ASKING, EXITING and EXIT_REFUSED. In UNLOCKING and DRIVING it sends ENABLE with the profile only when the MCB is ENABLED on a CONNECTED link, else nothing (rows 29-34) |
| REQ-DRV-31 | drive_session | A corrupted phase or input gives the safe state: LOCKED, DISABLE sent, waits, exit latch, menu flag, menu-on-arrival, advance timer and stop fault cleared, ring at rest, Locked screen, gate updated; `step` returns false |
| REQ-DRV-32 | drive_session | A tick decides TICK_FOLLOW on the Env at its start and the five other sub-steps, in `TICK_SEQUENCE` order, on one Env sampled after TICK_FOLLOW's actions |
| REQ-DRV-33 | drive_session | No row sends ENABLE except the unlock hold (row 18) and a profile tap with the MCB ENABLED (rows 31-32) |
| REQ-DRV-34 | drive_session | Every transition from an unlocked phase to LOCKED sends DISABLE |
| REQ-DAD-07 | drive_adapter | Driven by the C1 golden scenarios, the adapter makes the port calls written in this spec (§5.2) |
| REQ-DAD-08 | drive_adapter | The stop timer starts at the first stop of an exit and is not reset by a repeated stop; the last-DISABLE time is every DISABLE published; the Env's stop and re-send guards follow §2.1 exactly at their boundaries |
| REQ-DAD-09 | drive_adapter | Every DriveCommand publish result is checked; a failure is counted and logged at most once a second, and the re-send schedule does not change |
| REQ-DAD-10 | drive_adapter | After every input and tick, the Drive notice (§2.7 order) is handed to the port once per change |
| REQ-STK-10 | stick | When the output permit is withheld, the command is exactly (+0.0, +0.0, +0.0), whatever the position (NaN and negatives included) |
| REQ-STK-11 | stick | The output permit is the gate AND the motion guard's verdict OK AND no calibration AND a measured calibration AND POST PASS AND stick health allowing output AND the neutral latch |
| REQ-STK-12 | stick | The neutral latch sets after ≥ `kNeutralHold` of valid cycles all at x = y = twist = 0.0; a non-neutral, NaN or invalid cycle restarts the wait; any other permit condition failing clears it; once set it stays while they hold |
| REQ-STK-13 | stick | The hold reason is the first failing condition in the order GATE_SHUT, MOTION_GUARD, CALIBRATING, NOT_CALIBRATED, POST_NOT_PASSED, STICK_FAULT, STICK_CHECK, CENTRE_FIRST, else NONE |
| REQ-STK-14 | stick | POST gate NOT_RUN, PENDING and FAIL withhold output; PASS allows. Stick health FAULT (reason STICK_FAULT) and CHECK (reason STICK_CHECK) withhold; NOT_MONITORED and OK allow. The motion guard's verdict allows only OK; until C4 it is always OK |
| REQ-CAL-08 | joystick_cal | `joystick_cal_measured()` is true when the calibration in use was loaded valid from flash or completed in this boot, false while the compiled-in defaults are in use |
| REQ-UI-16 | hmi_ui | The calibrate hold applies only while locked, on the Joystick screen, with no menu |
| REQ-UI-17 | hmi_ui | The Drive screen shows the Drive notice in its own slot, separate from the refusal banner, within 250 ms of a change |
| REQ-UI-18 | hmi_ui | `DrivePort::sample` computes link CONNECTED at the sample (MibStatus age < 2 s) |
| REQ-RUI-05 | remote_ui | The bench verbs `STATE`, `PERMIT` and `CAL UNSAVED` exist only with `CONFIG_HMI_BENCH_STICK_INJECT`; a release ELF has none of their symbols |

## 5. Tests that prove it

Written from this spec. No expected value is generated from the new code (CORE never-list).

### 5.1 Oracle and table checks

| ID | Test |
| --- | --- |
| DRV-101..113 | by-input oracle, unchanged in shape, over the new table; new dimensions: CALIBRATING (2), STOP_FAULT_ELAPSED (2), resend (3), STOP_FAULT (2); ON_BOOT_SCREEN comes from the existing screen dimension |
| DRV-114 | TICK_STOP_FAULT_DUE by input: the table's row or no change |
| DRV-115 | TICK_STOP_RESEND by input: the table's row or no change |
| DRV-116 | a tick runs the six sub-steps in order on two Envs (replaces DRV-022): a re-send due between the two samples is sent in the same tick; a relock on Env 1 means no re-send on Env 2 |
| DRV-023 | MCB ENABLED while calibrating or on the Boot screen: no row, nothing; the next tick after either clears: F1 |
| DRV-024 | session-level stop timeline with scripted Envs: FAST due each tick → DISABLE; STOP_FAULT_ELAPSED → fault once; then DISABLE only on SLOW due; relock clears the fault |
| DRV-025 | `stop_notice` over every phase × STOP_FAULT |
| DRV-026 | every relock from an unlocked phase sends DISABLE and leaves the request DISABLE |
| DRV-027 | over all states: ENABLE is sent only by rows 18, 31, 32, and 31-32 only with DRIVING_OK |
| DRV-028 | exit hold in LOCKED (row 42) and ASKING (row 43) |
| DRV-013, 015, 017 | updated expectations (relock DISABLE; new invariant; safe state list) |
| DSO-001, 004, 005, 006, 012 | updated sizes, invariants and fingerprint |
| DSO-014 | STOP_FAULT only in exit phases; every exit-phase → LOCKED row clears it |
| DSO-015 | every unlocked → LOCKED row has SEND_DISABLE after GATE_UPDATE |
| DSO-016 | SEND_ENABLE only in rows 18, 31, 32; rows 31-32 need `+DOK` |
| DSO-017 | rows 1-2 read `!CALIBRATING !ON_BOOT_SCREEN`; ARM_STOP_TIMER only on UNLOCKING/DRIVING → EXITING |
| DSO-018 | `TICK_SEQUENCE` is exactly the §2.5 order |
| DSO-019 | the D4 constants: 250, 5000, 1000, 125 ms, and `kStopResend == kTickPeriod` |

### 5.2 New goldens (hand-written, at the port)

Each GLD-1xx compares a filtered port log with the expected lines below. The log keeps only:
`P(D|E[,profile])` publish, `L` Locked screen, `Dv` Drive screen, `B:x` banner, `N:x` notice,
`lock(0|1)`, `gate`, `ring_rest`, `ring_wait`, `open`, `menu(0|1)` (menu on arrival). Fake
clock; t is in ms after T0, the clock at the scenario's first step (T0 > 0: the adapter uses 0
for "not armed"). A tick at t uses t for both Envs. "DRIVING" = link up, MIB ENABLED, Locked
screen, tick (row 1), +1000 ms, timer fires (row 35), Drive screen loaded; t = 0 is the end of
that opening. Fake stick hold reason: NONE unless stated.

| ID | Steps | Expected |
| --- | --- | --- |
| GLD-101 | link up, MIB IDLE, Locked; tick; MIB ENABLED; tick; +1000; timer | 1st tick: nothing. 2nd: `open, lock(0), gate`. Timer: `Dv`. No `P` at all |
| GLD-102 | DRIVING; MIB IDLE; tick; tick | `menu(0), ring_rest, L, lock(1), gate, P(D), B:DRIVE_STOPPED`. 2nd tick: nothing |
| GLD-103 | DRIVING; link down; tick; link up, MIB ENABLED; tick | `menu(0), ring_rest, L, lock(1), gate, P(D), B:DRIVE_LOST`; then `open, lock(0), gate`, no `P` |
| GLD-104 | DRIVING; t=0 exit hold; t=250 tick; t=400 MIB IDLE; t=500 tick | t=0 `P(D), N:STOPPING`; t=250 `P(D)`; t=500 `menu(0), ring_rest, L, lock(1), gate, P(D), N:NONE`, no banner |
| GLD-104b | as GLD-104 with the burger key at t=0 | same, but `menu(1)` at t=500 |
| GLD-105 | DRIVING; t=0 exit hold; ticks every 250 ms to t=7000, MIB ENABLED | t=0 `P(D), N:STOPPING`. Ticks 250..4750: `P(D)` each (20 DISABLEs in [0, 5000)). t=750: `B:EXIT_REFUSED` then `P(D)`. t=5000: `N:MCB_DID_NOT_STOP`, no `P`. Then `P(D)` at 5750 and 6750 only. Never `L`, `lock`, `gate` |
| GLD-106 | as GLD-105 to t=1750; t=1900 burger key; ticks every 250 ms to t=7000 | as GLD-105 to t=1750; t=1900 `P(D)`; t=2000 nothing; `P(D)` at 2250 and 2500; t=2750 `B:EXIT_REFUSED, P(D)`; `P(D)` each tick 3000..4750; t=5000 `N:MCB_DID_NOT_STOP` (not 6900), no `P`; `P(D)` at 5750 and 6750 only |
| GLD-107 | GLD-105, then t=7000 MIB IDLE; ticks 7000, 7250, 7500 | t=7000 `menu(0), ring_rest, L, lock(1), gate, P(D), N:NONE`, no banner; then nothing |
| GLD-108 | link up, MIB ENABLED, Joystick screen, calibrating; ticks to 3000; t=3100 calibration ends; t=3250 tick; +1000; timer | nothing until 3000; t=3250 `open, lock(0), gate`; timer `Dv` |
| GLD-109 | Boot screen, link up, MIB ENABLED; ticks to 1000; Locked screen; tick | nothing on the Boot screen; then `open, lock(0), gate` |
| GLD-110 | DRIVING; profile HIGH; MIB IDLE (no tick); profile LOW; tick; profile NORMAL | `P(E,HIGH)`; nothing; `menu(0), ring_rest, L, lock(1), gate, P(D,LOW), B:DRIVE_STOPPED`; `P(D,NORMAL)` |
| GLD-111 | link up, IDLE, Locked; exit hold; +1000; tick | `P(D)`; tick: nothing (no banner, no deadline) |
| GLD-112 | link up, IDLE, Locked; unlock hold; exit hold; clock to 2100; tick | `ring_wait, P(E)`; `P(D), ring_rest`; tick: nothing (no NOT_GRANTED, no give-up DISABLE) |
| GLD-113 | DRIVING; t=0 exit hold; t=250 tick; t=300 link down; t=500 tick; t=600 link up, MIB ENABLED; t=750 tick | `P(D), N:STOPPING`; `P(D)`; `menu(0), ring_rest, L, lock(1), gate, P(D), N:NONE` (no banner); `open, lock(0), gate`, no `P` |
| GLD-114 | GLD-105 to t=5100; corrupted input 99 | safe state: `P(D), menu(0), ring_rest, L, lock(1), gate, N:NONE`; the step reports a fault |
| GLD-115 | hand-written scenarios plus the seeded walks (new step kinds: calibrating, Boot screen, stop flows) | every one of the 49 rows taken (replaces GLD-003). No log compared |
| GLD-116 | `DrivePort` over a recording Ui: the link subject says CONNECTED, the live link state says NO_PEER | `sample().link_connected` is false (REQ-UI-18) |

The walks only check row coverage and invariants; their logs are never frozen.

### 5.3 Adapter and stick (host L1)

| ID | Test |
| --- | --- |
| DAD-007 | the stop timer counts from the first stop; a second stop at +1900 ms does not move the fault |
| DAD-008 | re-send boundaries: 124 ms since the last DISABLE not due, 125 due; with the fault 874 not due, 875 due |
| DAD-009 | the port's publish returns false: counted, logged once per second, the next re-send still at its time |
| DAD-010 | the notice is handed to the port once per change, in the §2.7 order (all pairs of stop notice × hold reason) |
| DAD-011 | `calibrating` and the Boot screen in the sample reach the Env |
| STK-060 | held output is bitwise +0.0 on all axes for NaN, −1, +1 positions (REQ-STK-10) |
| STK-061 | 33 ms cycles, centred from t=0: zero until the first cycle at ≥ 300 ms, the stick's value from then |
| STK-062 | a non-neutral cycle at 200 ms restarts the wait; a NaN cycle counts as non-neutral |
| STK-063 | an invalid cycle restarts the wait |
| STK-064 | each of conditions 1-7 failing for one cycle clears the latch; output 0 until centred 300 ms again |
| STK-065 | once latched, full deflection keeps its output while 1-7 hold |
| STK-066 | hold reason order over all combinations (REQ-STK-13) |
| STK-067 | POST gate and stick health values (REQ-STK-14) |
| CAL-401 | `joystick_cal_measured()`: true after a valid load, true after a completed run (saved or not), false on defaults |

### 5.4 Existing tests and expected data that must change (owner approval, each)

| # | What | Why |
| --- | --- | --- |
| E1 | `tests/host/drive_golden/golden_port.txt`, `golden_raw.txt`: GLD-001, 002, 004 **retired**, files kept unchanged as history, not re-recorded | they pin the pre-C1 behaviour; regenerating them from new code is forbidden |
| E2 | GLD-003 replaced by GLD-115 | 49 rows |
| E3 | `TABLE_FINGERPRINT` (DSO-012) | the table changes |
| E4 | DSO-001 sizes: rows 41 → 49, inputs 11 → 13, guards 15 → 21, actions 36 → 39, `kMaxActions` 11 → 12 | |
| E5 | DRV-001..012 (full-product oracle) moved out of CI, runnable on demand (`make full`) | estimated 1.35 M → ~38 M steps, past the 30 s budget (TS-UNIT-09). Measure first; keep it in CI if it fits. `make equivalence` and `make mutants` re-run on the new table as evidence |
| E6 | DRV-013, 015, 017 expectations | relock DISABLE, STOP_FAULT invariant, safe-state list |
| E7 | DRV-020 and DRV-022 removed (successors STK-060..067, DRV-116) | |
| E8 | DAD-006 removed | U3 is rows 42-43 |
| E9 | STK-016 and STK-035 (they pin the multiply) removed; STK-001..009's replay of the extraction expects +0.0 where the golden row is gated or calibrating. `golden_stick.inc` itself is not edited (it pins the legacy code) | REQ-STK-10 |
| E10 | bench `scenario_hazards.py` B5c, B5d, B5e: their records become graded B5'' checks; B5d's "stays locked" branch is gone (M1 re-enters Drive) | |
| E11 | bench `scenario_drive.py` step 2: the HMI now also sends a DISABLE (not graded; verdict unchanged) | |

## 6. Bench checks (B5'')

The sim plays the MCB. No chair, no motors; the stick is injected (`STICK` verb, refreshed every
100 ms, expires 300 ms after the last refresh). Verdicts by script from the sim's event log
(DriveCommands, non-zero XYTwist, per-second XYTwist summaries with `count` and `nonzero`) and
the remote UI. Every step starts from `ok` (IDLE), Locked, `PERMIT POST pass`, stick centred,
and ends Locked (graded clean-up).

New bench tooling (bench builds only, REQ-RUI-05): `STATE` (one JSON line: phase, notice, hold
reason, calibrating, menu open, screen, the calibration's centre/min/max mV), `PERMIT POST
not_run|pending|pass|fail`, `PERMIT STICK not_monitored|ok|fault`, `CAL UNSAVED` (RAM only:
`joystick_cal_measured()` false until reboot). Sim modes used, all existing: `a` (ENABLED on its
own), `i` (IDLE on its own), `e` (ERROR), `z` (INITIALIZING), `s` (refuse DISABLE), `drop N`,
`ign N`, `p`/`r` (stale MibStatus), `ongone keep`, `mark`. "Forward" = injected y at its
calibrated max; "centre" = the calibrated centres.

| Step | Sim | Script | Pass if |
| --- | --- | --- | --- |
| B5''-1 entry, G1 | `a` | Locked, forward held; mark; `a` | DriveScreen within 3.0 s; no DriveCommand after the mark; no non-zero XYTwist for 3.0 s after Drive; `STATE` notice CENTRE_FIRST |
| B5''-1b G1 timing | ENABLED | from 1: centre 0.2 s, forward 2 s | no non-zero XYTwist (0.2 s < 300 ms) |
| B5''-1c | ENABLED | centre 0.5 s, then forward | first y > 0 within 0.3 s of forward; ≥ 90 % of samples non-zero over the next 2 s |
| B5''-2 MCB stops on its own | `i` | driving forward; mark; `i` | Locked within 1.5 s; no non-zero XYTwist from mark + 0.6 s; exactly 1 DISABLE and 0 ENABLE in [mark, mark + 2 s] |
| B5''-3 | `e`, then `z` | as 2 | as 2 |
| B5''-4 stale, DISABLE obeyed | `p` | driving forward; mark; `p` | Locked within 3.0 s; no non-zero XYTwist from mark + 2.6 s; ≥ 1 DISABLE after the mark; after `r` (sim now IDLE): Locked for 5 s |
| B5''-4b stale, DISABLE lost | `drop 1`, `p`, `r` | as 4, then `r` with the sim still ENABLED, forward held | DriveScreen within 3.0 s of `r`; no non-zero XYTwist for 3.0 s; notice CENTRE_FIRST |
| B5''-5 stop obeyed | none | Drive; mark; exit hold (2 s) | first DISABLE within 2.2 s of the mark; Locked within 1.0 s of it; ≤ 3 DISABLEs in [mark, mark + 4 s]; no ENABLE |
| B5''-6 MCB ignores DISABLE | `s` | driving forward; mark; exit hold; watch 7 s from the first DISABLE t1 | DriveScreen the whole time; ≥ 90 % of XYTwist samples non-zero (owner-accepted risk, shown); 18-22 DISABLEs in [t1, t1 + 5.0 s], every gap ≤ 0.4 s; notice STOPPING by t1 + 0.5 s, MCB_DID_NOT_STOP at t1 + 5.0..5.5 s; after t1 + 5.5 s gaps 0.85..1.3 s |
| B5''-6b | `s` off | continue 6 | Locked within 1.5 s; notice NONE; ≤ 1 DISABLE after Locked; none from Locked + 2 s |
| B5''-7 lost DISABLEs | `drop 3` | Drive; exit hold | Locked within 2.0 s of the first DISABLE; ≥ 4 DISABLEs |
| B5''-8 burger key, ignored | `s` | as 6 with the burger key; then `s` off | as 6; then Locked with the menu open (`STATE`) |
| B5''-9 calibration running | `a` | Joystick screen; hold BTN 2 s (calibrating = 1); mark; `a`; watch 5 s; hold BTN 2 s (cancel) | screen stays Joystick and calibrating stays 1 for 5 s; no non-zero XYTwist; after the cancel, DriveScreen within 3.0 s |
| B5''-10 never calibrated | `a` | `CAL UNSAVED`; `a`; centre 0.5 s, forward 2 s; reboot after | DriveScreen within 3.0 s; notice NOT_CALIBRATED; no non-zero XYTwist |
| B5''-11 POST hook | `a` | `PERMIT POST pending`; Drive; centre, forward; then `PERMIT POST pass` with forward held 1 s; centre 0.5 s, forward | zero while pending, notice POST; zero for the 1 s after pass (latch); y > 0 within 0.3 s at the end |
| B5''-12 stick-fault hook | `a` | driving forward; `PERMIT STICK fault`; then `ok` with forward held 1 s; centre 0.5 s, forward | XYTwist zero within 0.2 s of `fault`, notice STICK_FAULT; zero 1 s after `ok`; y > 0 at the end |
| B5''-13 profile tap | ENABLED | Drive; mark; tap LOW | exactly 1 DriveCommand in 1 s: ENABLE, profile LOW |
| B5''-14 entry from another screen | `a` | Seat screen; `a` | DriveScreen within 3.0 s |
| B5''-15 HMI reset, MCB ENABLED | `ongone keep`, `a` | Restart HMI tile; forward held from boot | DriveScreen after boot within 30 s; no non-zero XYTwist until 60 s (POST gate NOT_RUN after reset); no ENABLE from the HMI |
| B5 regression | as today | `scenario_drive.py` steps 1-4 | PASS |

## 7. Implementation notes

### 7.1 Files

| File | Change |
| --- | --- |
| `components/drive_session/include/drive_session_types.hpp` | enums, `Env`, `env_guards`, `kMaxActions` (§2.1) |
| `components/drive_session/include/drive_session_table.hpp` | constants, rows, invariants, effects, preconditions, `TICK_SEQUENCE`, `stop_notice`, static checks; `stick_scale` removed |
| `components/drive_session/include/drive_session_fingerprint.hpp` | new value; hash the stop constants and `stop_notice` |
| `components/drive_session/{include/drive_session.hpp,src/drive_session.cpp}` | the hand-written transition function, safe state |
| `components/drive_session/{TABLE.md,README.md}`, tests | §2.10, §4, §5.1 |
| `components/drive_adapter/include/drive_adapter.hpp`, README, tests | §2.7, U3 removed |
| `components/hmi_ui/include/hmi_ui/drive_port.hpp`, `drive_ui.*`, `ui_app.*` | §2.9, calibrate applies (REQ-UI-16), the Drive notice slot and view |
| `components/hmi_rtps_spec` | the notice texts |
| `components/stick` | `OutputPermit`, literal zeros, hold reason, the hooks' types (motion guard verdict, POST gate, stick health) |
| `main/main.cpp`, `main/joystick_cal.*` | `AdcStickIo::output_permit`, the hook atomics, the ADC clock function, `joystick_cal_measured()`, boot log lines |
| `components/remote_ui` | `STATE`, `PERMIT`, `CAL UNSAVED` |
| `tests/host/drive_golden` | GLD-101..116, the filtered log, retire GLD-001/002/004 |
| `tools/bench/scenario_hazards.py` (or a new `scenario_c1.py`) | B5'' |
| `tests/manifest.d/*.yaml` | entries and requirement IDs |

### 7.2 Commit order (refactor separate from behaviour; CI green at each)

1. **Refactor** (fingerprint and every golden unchanged): `DrivePort::publish` returns bool
   (still ignored); `tick()` loops over `TICK_SEQUENCE[1..]`; the golden harness learns the
   filtered log (no new expectations yet).
2. **Bench tooling** (release ELF unchanged, REQ-RUI-01 symbol check): `STATE`, `PERMIT`,
   `CAL UNSAVED`.
3. **Oracle switch** on the unchanged table (owner-approved, E5): DRV-001..012 out of CI, with
   `make equivalence` evidence.
4. **Behaviour, drive table**: types, table, fingerprint, session, TABLE.md, README, oracle and
   DSO tests, adapter (U3 removed, timers, publish check, notice), port, GLD-101..116, retire
   GLD-001/002/004 (E1-E8). One commit, or two if both stay green.
5. **Behaviour, stick permit**: `OutputPermit`, pipeline literal zeros, `AdcStickIo`, atomics,
   `joystick_cal_measured`, STK and CAL tests (E9).
6. **Behaviour, UI**: the Drive notice slot and texts, calibrate applies.
7. **Bench script**: B5'' and the B5c-e changes (E10, E11).

Then the bench run on board 2, then the merge to `dev_refactor`.

### 7.3 Risks

- The re-send rides the UI task's 250 ms tick. A stalled UI task sends no re-sends (H4; C4's
  heartbeat zeroes the stick, not the re-send).
- The tick is late by a few ms each time. The 125 ms slack is there for it; do not compare
  against exactly 250 ms.
- Oracle run time grows with every guard bit. Measure before E5.
- Removing U3 changes the adapter's public behaviour for a race nobody can trigger on purpose.
  Cover it at L1 only (GLD-111, 112, DRV-028).
- The profile-tap race (§1) is protocol-level and stays.
- `link_connected` read live (§2.9) can differ from the TopBar label for up to 250 ms. The drive
  decision is the one that must be exact.
- Retired REQ IDs must disappear from every TEST_CASE tag and manifest, or `run.py check` fails.

### 7.4 What must not change

- `stick_drives()`, `GATE_TRIGGERS`, the hold table and the 1.5 s holds.
- Rows listed unchanged in §2.4, their order and their action order.
- `kDriveAnswer` (750 ms), `kDriveWait` (2 s), `kUnlockAdvance` (1 s), the 280 ms fade.
- XYTwist keeps flowing at the ADC rate, neutral while held. H9 (an invalid cycle publishes
  nothing) stays until C2.
- No allocation after start-up on the drive and ADC paths; no LVGL call off the UI task; the ADC
  task never waits on `lvgl_mutex`.
- DriveCommand and XYTwist on the wire; the RTPS spec.
- A release build contains no bench verb and no injection.
- Seat path, self-test overlay (H7), POST wiring (C3), staleness on the ADC task (C4) and stick
  fault detection (C2): out of scope. C1 only provides their hooks (§3.2).

Reconciled with C3, C4 and C2 on 2026-10-08: see `hazard-fixes.md`, Reconciliation log, and the
owner's decision sheet `hazard-decisions.md`.

## 8. Open questions for the owner

1. **Q1 "At once."** Keep the 1 s padlock beat and 280 ms fade on an MCB-initiated entry? The
   stick output is 0 during it anyway. Proposed: keep.
2. **Q2 Relock DISABLE.** Every relock sends one DISABLE, including when the MCB stopped on its
   own or the link went stale (fixes H5). Agree?
3. **Q3 Stop and link loss.** A stale link ends the user's stop (relock, one DISABLE). If the
   link returns with the MCB still ENABLED, the HMI re-enters Drive (M1) with no stop warning.
   The v2 alternative kept re-sending through the link loss. §9 is silent. Proposed: end it.
4. **Q4 Hooks before C4/C2.** C1 and C3 merge together, so the POST hook's NOT_RUN block never
   ships alone. The motion-guard hook passes until C4, and the stick hook NOT_MONITORED passes
   until C2 (no detector exists). Agree?
5. **Q5 Calibration.** Does a calibration completed this boot but not saved to flash count as
   calibrated? Proposed: yes. Should the unlock hold refuse to send ENABLE when not calibrated
   or before POST? §9 does not say; proposed: not in C1.
6. **Q6 Calibration only while locked.** Needed so no screen switch can hit a running
   calibration (G5). Agree?
7. **Q7 Fault lifetime.** "MCB did not stop" clears at the relock and stays in the log. Or keep
   it until acknowledged?
8. **Q8 Exit-refused banner.** Keep the 2 s banner at 750 ms next to the persistent "Stopping"
   notice (row 14 unchanged)? Proposed: keep.
9. **Q9 Texts.** Approve the notice texts in §3.3, including "Centre the joystick to drive"
   for G1.
10. **Q10 Profile tap.** DriveCommand has no "profile only" request, so a tap re-sends ENABLE.
    Ask the MCB team (add to D3)?
11. **Q11 Retirements.** Approve E1-E11 (§5.4), in particular retiring GLD-001/002/004 and
    moving the full-product oracle out of CI.
12. **Q12 G1 scope.** The neutral wait applies to every held → allowed change, not only to
    entering Drive. Stricter than §9's text. Agree?
13. **Q13 §9 wording.** §9 says it supersedes C1 v2's rows 1-2. This spec also replaces v2's
    DISABLE_PENDING (kept through link loss, never stopping) with G4's re-send in the exit
    phases. Confirm.
14. **D3 dependency.** The MCB team's answers may change the 5 s value (ramp-down) and Q2/Q3
    (does DISABLE latch across resets?).
