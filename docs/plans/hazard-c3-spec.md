# Spec: hazard fix C3 (no stick output and no drive entry effect before POST pass)

Status: **spec for the owner's approval** (2026-10-08). No code, test data, table or golden
has been changed. A different agent implements it after approval (CS-SAF-05; D1: the owner is
the only approver for now). Base: the C1 spec, 1d41fb3 (`dev_ai_hazard_c1_spec`).

C1 and C3 are implemented and merged **together**. C1's POST hook (`PostGate` = NOT_RUN)
blocks the stick in every normal build until C3 wires POST in, so C1 must not merge alone.

Inputs: `docs/plans/hazard-fixes.md` §4 C3 (v2 rules) and §9 (M1, G1-G5, D4);
`docs/plans/hazard-c1-spec.md` (rows 1-49, `TICK_SEQUENCE`, the stick output permit, the
POST hook); `docs/plans/post-limits-proposal.md` (approved, `WINDOW_MIN_SAMPLES` 30); the C4
spec (`origin/dev_ai_hazard_c4_spec`); `components/post` (the evaluator, unchanged in shape);
hazards H2 and H12 (`docs/plans/refactor.md` §1); CS-SAF-03, TS-POST-01..05.

Row numbers continue C1's: C1 ends at row 49, C3 appends rows 50-54. Every C1 row keeps its
number.

## 1. Exec summary

What C3 does, in one line each:

- POST runs at every boot: hardware checks latch (a FAIL holds until the next reset); the
  "stick at rest" checks only delay (judged again on every 1.05 s window).
- Until POST passes, the stick output is 0 and the HMI never sends ENABLE. The unlock hold, the
  Drive entry push, the menu's DRIVE row and seat presses are refused with a banner.
- Under M1 the MCB still decides: if it reports ENABLED before POST passes, the HMI enters Drive,
  sends 0, and the Drive screen names the check (G3).
- The boot sends one DISABLE on the first fresh link (rows 50-51). No re-send, no fault timer.
- A persistent indicator in the TopBar (every screen but Boot) shows a waiting or failed POST.
  It is separate from the 3 s refusal banners and from C1's Drive notice.
- The reset reason is always logged and shown on the About screen. An unclean reset (panic,
  watchdog, brownout, unknown) fails POST until a clean reset (H12).

What the user sees at boot:

| Case | Screen | Stick | Unlock hold, seat |
| --- | --- | --- | --- |
| All good | Boot logo, Locked. TopBar "Start-up check" for about 1 s, then nothing | 0 until POST pass (about 1.1 s after the stick task starts, long before the link) | works once POST passed and the MCB is ready |
| Stick bumped or button held at power-on | Locked. TopBar amber "Centre the joystick" or "Release the joystick button" | 0 | refused, banner "Not ready to drive: start-up check". Released: POST passes within 2.5 s, the indicator goes |
| Hardware fault (I2C device missing, bad ADC read, memory, stack, image, no saved calibration) | Locked. TopBar red "Start-up check failed: <check>" and what to do | 0 until the next reset | refused, banner |
| MCB already ENABLED at boot (HMI reset mid-drive) | First fresh link: one DISABLE. MCB obeys: stays Locked. MCB ignores it: Drive one tick later (M1) | 0 until POST pass **and** the stick centred 300 ms (C1 G1) | as above |
| Unclean reset (panic, watchdog, brownout) | Locked. TopBar red "Restarted after a fault: TASK WDT. Turn the HMI off and on" | 0 until a clean reset | refused |
| Any reset | About screen "Last reset: <reason>"; serial log `POST sys.clean_reset ...` | | |

What the HMI does when the MCB reports ENABLED and POST has not passed (G3):

| POST gate | Drive table | Stick output (ADC task) | Drive notice | TopBar |
| --- | --- | --- | --- | --- |
| NOT_RUN (runner never ran) | enters Drive (rows 1-2, M1) | 0, hold reason POST_NOT_PASSED | "Start-up check not run" | red, same text, from the budget on |
| PENDING, hardware still gathering | enters Drive | 0 | "Start-up check running" | grey |
| PENDING, stick not at rest | enters Drive | 0 | "Centre the joystick" or "Release the joystick button" | amber, same text |
| FAIL | enters Drive | 0 until reset | "Start-up check failed: <check>" | red |
| PASS | enters Drive | the stick, once centred 300 ms (C1) | C1's notices | none |

In every row: XYTwist keeps flowing, neutral. No ENABLE is sent by the HMI. The user's stop
(exit hold, burger key) works as C1 says.

**Plan change, flagged (Q1).** The v2 rule "new phases POST_PENDING and POST_FAILED; no row
leaves them except POST pass and link" cannot hold under M1: the MCB's ENABLED must take the
HMI to Drive. So POST is **not** a drive phase. It is one atomic, the POST gate, that both the
ADC task (stick output) and the drive table (an Env guard, `POST_OK`) read. "POST_PENDING" and
"POST_FAILED" below are gate values, not phases.

## 2. Rules

### 2.1 The checks: latched hardware vs live guards

The evaluator's table (`components/post/include/post/checks.hpp`) is used as it is. C3 only
changes `WINDOW_MIN_SAMPLES` (25 → 30, approved) and the `D4:` comments (§3).

| Kind | Checks | Judged on | A FAIL |
| --- | --- | --- | --- |
| LATCHED | `adc.valid`, `joy.cal_saved`, `joy.cal_span`, `i2c.missing`, `sys.clean_reset`, `img.ok`, `mem.int_min`, `mem.int_block`, `mem.dma_min`, `mem.psram_free`, `stk.adc`, `stk.ui` | the first window that brings their facts | holds until the next reset |
| LIVE | `joy.x/y/twist_cal_off` (rest within the band of the calibrated centre), `joy.x/y/twist_noise` (still), `joy.button_idle` | every window | only delays: the next window judges again |

Once the overall state is PASS or FAIL it never changes until reset (`OVERALL_TRANSITIONS`). So
after PASS, the live checks stop mattering; C1's neutral latch (300 ms centred) takes over for
every later held → allowed change.

### 2.2 Where the facts come from

| Facts group | Gathered by | When | Source |
| --- | --- | --- | --- |
| `window` (StickWindow) | ADC task, `RestWindow` accumulator | every 30 cycles, from the first cycle with all three reads valid | the reads passed to `StickPipeline::cycle` (after bench injection), button as `button_pressed()` reads it |
| `i2c` | `app_main` | the existing boot scan, before the UI task starts | `found_addresses` → `I2cSet` |
| `reset` | POST runner (UI task) | at the first window | `esp_reset_reason()` |
| `image` | POST runner | at the first window | `esp_ota_get_state_partition(running)`; `verified` = the bootloader validated it (Q10) |
| `cal` | POST runner | at the first window | `joystick_cal_saved()`, `joystick_cal_current()` |
| `memory` | POST runner | at the first window | `heap_caps_get_minimum_free_size(INTERNAL + 8BIT)`, `heap_caps_get_largest_free_block(INTERNAL)`, `heap_caps_get_minimum_free_size(DMA)`, `heap_caps_get_free_size(SPIRAM)`; saturated to `int32` |
| `stacks` | POST runner | at the first window | `uxTaskGetStackHighWaterMark` of `Read ADC` and of itself (`lv_task`) |

The window starts at the first all-valid cycle so that the continuous ADC's start-up (its DMA
frames not yet filled) cannot latch `adc.valid` FAIL on every boot. A dead ADC never starts a
window and fails by the budget (§2.6). After the first valid cycle every cycle counts.

### 2.3 The POST runner and the gate

- A pure class `hmi::post::PostRunner` (components/post) holds the facts, the latched `Report`
  and the start time. A port that `main` fills gathers the IDF facts (§2.2). No IDF in the class.
- It runs on the **UI task**, in the 250 ms UI poll, **before** the drive adapter's tick, so a
  pass seen on a tick is used by that tick. Interim choice (Q7): the target is the selftest
  island writing `POST_RESULT` to `control` (topology). Why not the ADC task: the evaluator's
  frames (two `Report`s and one `Facts`, an estimated 600 B) do not fit the ADC task's 1188 B worst free
  stack before C4 raises it.
- Each tick: read the window mailbox; on CHANGED, set `facts.window`; on the first window,
  gather the other groups once; `latched = merge(latched, evaluate(facts))`; apply the budget
  (§2.6); store the gate if it changed.
- Gate values: `PostGate{NOT_RUN, PENDING, PASS, FAIL}` (C1 §3.2). NOT_RUN at boot (static
  init). The runner stores PENDING at its first tick, then PASS or FAIL. It never moves back
  (enforced with `next_overall`; a static_assert maps `Overall` onto `PostGate`).
- On PASS or FAIL it prints the TS-POST-05 lines once (§2.7).

### 2.4 How the gate reaches the stick and the table (atomics, memory order)

| Value | Type | Writer (task) | Store | Readers | Load |
| --- | --- | --- | --- | --- | --- |
| `post_gate` | `fw::AtomicValue<PostGate>` (`uint8_t`, `is_always_lock_free` asserted) | POST runner (`lv_task`) only | release, only on change | `Read ADC` every cycle (C1's `OutputPermit`, condition 4); `lv_task` (drive Env, seat gate, indicator) | acquire |
| rest window | `fw::Mailbox<RestWindowMsg>` (one slot, overwrite; message ≤ 128 B, trivially copyable) | `Read ADC` | once per 30 cycles, until the gate is PASS or FAIL | POST runner (`lv_task`) | `read()` |
| bench override | `PERMIT POST ...` (C1) | arrives on the UI task (remote UI request) and is applied **by the runner** | — | — | — |

- One writer per value. The bench override goes through the runner, so the gate keeps one
  writer (CS-OWN).
- The ADC task reads the gate with one acquire load per cycle (C1 already has it). Any byte
  that is not PASS withholds output, including a value outside the enum.
- The ADC task stops accumulating once it reads PASS or FAIL. It never waits on the mailbox.
  In bench builds only, when it sees the gate leave PASS or FAIL (`POST RERUN`), it drops any
  partial window and waits again for a first all-valid cycle.
- The drive table reads the gate at the sample: `Env.post_ok = (post_gate == PASS)`, live, in
  `DrivePort::sample()` (the same way C1 reads the link live).
- Why acquire/release and not relaxed: nothing else is published with the gate, so relaxed
  would do. Release/acquire costs nothing on RV32 and keeps the rule "every safety atomic is
  release/acquire" simple to review.

### 2.5 Boot DISABLE (reconciles v2's `DISABLE_PENDING`, flagged Q2)

The v2 rule was "`DISABLE_PENDING` is set at start, so the first fresh link sends and re-sends
DISABLE". C1 replaced `DISABLE_PENDING` with G4's re-send, which runs only after the user's own
stop. C3 does **not** bring `DISABLE_PENDING` back. It adds:

- One DISABLE on the **first tick with link CONNECTED** after boot (fresh MibStatus < 2 s), from
  LOCKED (row 50). If the user's unlock hold already sent ENABLE (ASKING, a race of ≤ 250 ms),
  the user's request wins: no DISABLE, the bit is just marked (row 51).
- Rows 1-2 (M1 entry) need the boot DISABLE done. The boot step runs right after TICK_FOLLOW, so
  the first CONNECTED tick sends DISABLE and does not enter Drive. The next tick (250 ms later)
  enters Drive if the MCB still reports ENABLED.
- No re-send, no 5 s fault. If the DISABLE is lost or ignored, the MCB wins (M1, G4) and the
  stick is still held by POST and the neutral latch.
- Why: after an HMI reset mid-drive, the MCB keeps its last state. One DISABLE asks it to start
  from a deliberate request (D3 Q2). It costs one message.

If the MCB reports its new state later than 250 ms after the DISABLE, the HMI may enter Drive
for one tick and relock with C1's "stopped" banner (and one more DISABLE). Output is 0 throughout.

### 2.6 POST time budget

- `POST_BUDGET_MS` = 3000 ms from the runner's first tick (proposed, Q5). Expected: one window,
  30 × 35 ms = 1.05 s after the ADC task starts, plus ≤ 250 ms to the next UI tick: about 1.3 s.
  On board 2 the runner starts at about 8.4 s after reset (the UI build is slow), so POST passes
  at about 9.7 s, before the network link (15 s or more).
- At the budget, any **LATCHED** check still PENDING makes the gate FAIL ("timed out"). The
  blocking check is that check; its POST line is FAIL (TS-POST-03: no measurement is a FAIL).
- A **LIVE** check has no budget: a stick held off centre keeps POST PENDING (safe, no motion),
  with the amber indicator naming it. It never becomes FAIL.
- The profile's "POST time budget: none" becomes "3 s from POST start; expected 1.3 s".

### 2.7 POST lines (TS-POST-05)

At PASS or FAIL, once, on the serial log:

- one line per check, in table order: `POST <name> <PASS|FAIL|SKIP> <value> <unit> [<lo>,<hi>]`;
  a LIVE check still PENDING at a FAIL prints SKIP; a LATCHED one prints FAIL;
- `POST RESULT <PASS|FAIL> <passed>/19 <ms since POST start>`;
- `POST reset reason: <name> (<esp_reset_reason value>)`, at every boot, at the first window.

While PENDING on a LIVE check: `POST waiting: <name> <value> <unit>`, at most once per change
of blocking check.

### 2.8 The persistent fault indicator

- A new element in the TopBar (code, not SquareLine), on every screen but Boot. It is not a
  banner: it does not time out, cannot be dismissed, and banners do not hide it.
- State (pure function `post_indicator(gate, report, since_start)`, components/post):

| Gate | Blocking check | Indicator | Colour |
| --- | --- | --- | --- |
| PASS | none | NONE | — |
| PENDING | a LATCHED check, PENDING | CHECKING "Start-up check" | grey |
| PENDING | a LIVE check | WAITING: the check's text | amber |
| FAIL | a LATCHED check | FAILED: "Start-up check failed: <text>" + what to do | red |
| NOT_RUN | — | NOT_RUN "Start-up check not run", from the budget on (before it: CHECKING) | red from the budget |

- Texts per check (Q9; one table, `hmi_ui/post_texts.hpp`): REST and STILL checks "Centre the
  joystick"; `joy.button_idle` "Release the joystick button"; `joy.cal_saved` "The joystick
  must be calibrated first" (G5's words); `joy.cal_span` "Joystick calibration too small:
  calibrate again"; `sys.clean_reset` "Restarted after a fault: <reason>. Turn the HMI off and
  on"; `i2c.missing` "Internal device missing"; `adc.valid` "Joystick read failed"; `img.ok`
  "Firmware image not valid"; memory and stack "Low memory". A timed-out check prefixes
  "Start-up check timed out:". Each FAIL text ends "Turn the HMI off and on" unless stated.
- The Drive notice (C1 §2.7) uses the same text for POST_NOT_PASSED. C1's "Start-up check not
  run" stays for NOT_RUN.

### 2.9 Reset reason (H12)

- Logged at every boot (§2.7).
- About screen: a new row "Last reset: <name>" (names from the self test's
  `reset_reason_name`, moved to a shared place).
- Unclean (PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT, EFUSE, PWR_GLITCH, CPU_LOCKUP, UNKNOWN,
  unnamed): POST FAIL, latched (the evaluator's REQ-POST-05, owner to confirm, Q3). Way out: a
  clean reset (power off and on; the Restart HMI action is a software reset and counts as clean).
- With C4's task watchdog, a trip resets the HMI and this rule then holds it until a clean reset.

### 2.10 Seat presses

`UiApp::seat_request` (the only SeatCommand publish, Seat screen and Actuators page) publishes
only when the gate is PASS. Otherwise: no SeatCommand, the refusal banner REFUSED_POST and the
refusal feedback. The seat-path fix adds STICK_OK and `seat_ready` later.

### 2.11 Stick button bit before POST (Q11)

C1 keeps the stick button bit flowing in XYTwist while the output is held. Proposed for C3:
while the gate is not PASS, the button bit is sent released. G3 says "no stick output"; the bit
is part of it.

### 2.12 Calibration and POST

`joy.cal_saved` and `joy.cal_span` latch at the first window. A device never calibrated fails
POST. A calibration completed and saved in this boot does not change the latched FAIL: the
Joystick screen then says "Calibration saved. Restart the HMI to drive", and the Drive notice
and indicator say the same (Q4). The evaluator is not changed.

## 3. Constants

| Constant | Value | Where | Status |
| --- | --- | --- | --- |
| `WINDOW_MIN_SAMPLES` | **30** cycles (was 25; 1.05 s at the measured 35 ms) | `post/checks.hpp` | approved D4; fix the "33 ms" comment |
| `ADC_VALID_MIN_PERMILLE` | 990 | `post/checks.hpp` | approved |
| `EXPECTED_I2C` | 0x10 0x28 0x32 0x36 0x40 0x41 0x43 0x44 0x55 0x5a 0x68 | `post/checks.hpp` | approved (board 2; board 1 has no 0x5a, Q6) |
| `MEM_INT_MIN_B` | 12 KiB | `post/checks.hpp` | approved |
| `MEM_INT_BLOCK_B` | 12 KiB | `post/checks.hpp` | approved |
| `MEM_DMA_MIN_B` | 1536 B | `post/checks.hpp` | approved |
| `MEM_PSRAM_FREE_B` | 8 MiB | `post/checks.hpp` | approved |
| `STK_ADC_MIN_B` | 1024 B | `post/checks.hpp` | approved (thin margin) |
| `STK_UI_MIN_B` | 2048 B | `post/checks.hpp` | approved |
| `REST_XY_MAX_MV` | 40 mV | `post/checks.hpp` | approved |
| `REST_TWIST_MAX_MV` | 50 mV | `post/checks.hpp` | approved |
| `STILL_XY_MAX_MV` | 30 mV | `post/checks.hpp` | approved |
| `STILL_TWIST_MAX_MV` | 120 mV | `post/checks.hpp` | approved |
| `CAL_MIN_HALF_SPAN_MV` | 1000 mV | `post/checks.hpp` | fixed, not D4 (unchanged) |
| `POST_BUDGET_MS` | 3000 ms from the runner's first tick | `post/runner.hpp` | **proposed** (Q5) |
| POST tick | = `kTickPeriod` (250 ms) | UI poll | `static_assert` |
| REFUSED_POST banner | 3000 ms | refusal view | as the other refusals |
| `kNeutralHold` | 300 ms | `components/stick` | C1, approved |
| fresh link | MibStatus age < 2000 ms (`kMibStatusTimeout`) | existing | approved |

The `D4:` markers in `checks.hpp` become "D4 approved 2026-10-08" with the values above.

## 4. Drive table changes on top of C1

### 4.1 New vocabulary (appended to each enum)

| Kind | New | Meaning |
| --- | --- | --- |
| Input | `TICK_BOOT_STOP` | tick sub-step: is the boot DISABLE due |
| Guard, env | `POST_OK` | `Env.post_ok`: the POST gate is PASS at the sample |
| Guard, hidden | `BOOT_STOP_DONE` | the boot DISABLE was sent (or the user's ENABLE made it moot) |
| Action | `MARK_BOOT_STOP` | sets BOOT_STOP_DONE |
| Action | `SHOW_REFUSED_POST` | banner REFUSED_POST, 3000 ms; the view fills the check's text |

Counts after C1 + C3: inputs 13 → 14, guards 21 → 23 (env 15 → 16, hidden 6 → 7), actions
39 → 41, rows 49 → 54, `kMaxActions` 12 (unchanged).

### 4.2 Changed rows

| # | from, input | C1 guard → C3 guard | Actions | Why |
| --- | --- | --- | --- | --- |
| 1 | LOCKED, TICK_FOLLOW | `+DOK !CALIBRATING !ON_BOOT_SCREEN` → `+DOK !CALIBRATING !ON_BOOT_SCREEN +BOOT_STOP_DONE` | F1 (unchanged) | §2.5: entry waits one tick after the boot DISABLE. POST_OK is **not** read: M1 enters Drive before POST pass (G3) |
| 2 | ASKING, TICK_FOLLOW | same as row 1 | F1 (unchanged) | same |
| 18 | LOCKED, UNLOCK_HOLD_DONE | `+RDY` → `+RDY +POST_OK` | unchanged (RING_WAIT, SEND_ENABLE, ARM_WARN, ARM_GIVEUP) | H2: no ENABLE before POST pass |
| 31 | UNLOCKING, PROFILE_CLICK | `+DOK` → `+DOK +POST_OK` | SEND_ENABLE (unchanged) | H2: no ENABLE before POST pass (Q8) |
| 32 | DRIVING, PROFILE_CLICK | `+DOK` → `+DOK +POST_OK` | SEND_ENABLE (unchanged) | same |

### 4.3 Added rows

| # | from | input | guard | to | actions | Why |
| --- | --- | --- | --- | --- | --- | --- |
| 50 | LOCKED | TICK_BOOT_STOP | `+LINK !BOOT_STOP_DONE` | LOCKED | SEND_DISABLE, MARK_BOOT_STOP | §2.5 boot DISABLE |
| 51 | ASKING | TICK_BOOT_STOP | `+LINK !BOOT_STOP_DONE` | ASKING | MARK_BOOT_STOP | the user's ENABLE wins; no DISABLE |
| 52 | LOCKED | UNLOCK_HOLD_DONE | `+RDY !POST_OK` | LOCKED | RING_REST, SHOW_REFUSED_POST, REFUSAL_FEEDBACK | refusal, mirrors row 19; reachable only through FILL_DONE (no `applies()` re-check) |
| 53 | LOCKED | ENTRY_PUSH | `+ON_LOCKED !MENU_OPEN +RDY !POST_OK` | LOCKED | SHOW_REFUSED_POST, REFUSAL_FEEDBACK | the user pushes to unlock before POST pass; mirrors row 38 |
| 54 | LOCKED | MENU_ROW_DRIVE | `+RDY !POST_OK` | LOCKED | REFUSAL_FEEDBACK, SHOW_REFUSED_POST | the menu's DRIVE row before POST pass; mirrors row 40 |

No ASKING versions of 52-54: ASKING is reached only through row 18, which needs POST_OK, and the
gate never leaves PASS (outside bench builds). The oracle still enumerates them: no row, no
change. Rows 19, 38-41 (`!RDY`) keep priority when the MCB is not ready: their guards and 52-54's
are exclusive on RDY.

Removed rows: none. Every other C1 row is unchanged, in order and in action order.

### 4.4 Other table data

| Item | Change |
| --- | --- |
| `TICK_SEQUENCE` | TICK_FOLLOW, **TICK_BOOT_STOP**, TICK_EXIT_DUE, TICK_STOP_FAULT_DUE, TICK_STOP_RESEND, TICK_WARN_DUE, TICK_GIVEUP_DUE. FOLLOW on Env 1; the six others on Env 2 (C1's rule). BOOT_STOP right after FOLLOW: the first CONNECTED tick sends DISABLE and does not enter |
| `PHASE_INVARIANTS` | BOOT_STOP_DONE must be true in UNLOCKING, DRIVING, EXITING, EXIT_REFUSED (only rows 1-2 leave the locked phases, and both need it) |
| `ACTION_EFFECTS` | MARK_BOOT_STOP sets BOOT_STOP_DONE |
| `INPUT_PRECONDITIONS` | TICK_BOOT_STOP: all phases, always |
| `SAFE_STATE_ACTIONS` | unchanged: the safe state sends DISABLE but does not mark the boot stop, so a safe state before the first link still gets the boot DISABLE at the first link |
| `UNLOCK_APPLIES` | `when({ON_LOCKED_SCREEN, MCB_READY}, {MENU_OPEN})` → `when({ON_LOCKED_SCREEN, MCB_READY, POST_OK}, {MENU_OPEN})`: the unlock hold does not fill before POST pass; ENTRY_PUSH (row 53) says why |
| `EXIT_APPLIES`, `HOLD_TRANSITIONS`, `HOLD_POLL_SEQUENCE`, `GATE_TRIGGERS`, `stick_drives()` | unchanged |
| `KNOWN_GAPS` | stays empty |
| Static checks | add: every row with SEND_ENABLE has `+POST_OK`; rows 1-2 read `+BOOT_STOP_DONE` and do not read POST_OK; MARK_BOOT_STOP only in rows 50-51; SHOW_REFUSED_POST only with `!POST_OK` |
| Fingerprint | changes (C1's value → a new one, computed by the implementer in the same commit). The approval of §4 is the approval of that value |

TABLE.md: rows 50-54, the new guards, §5 diagram (boot DISABLE, refusals), §7 H2 closed.

### 4.5 Adapter and port

- `DrivePort::sample()` adds `post_ok`, read live from `post_gate` (acquire).
- `DriveAdapter::tick()` runs the seven `TICK_SEQUENCE` steps (C1's loop, one more step).
- The refusal view gains REFUSED_POST: title "Not ready to drive" (Seat: "Seat not ready"),
  body the indicator's text for the blocking check.
- UI poll order per 250 ms tick: POST runner, then the drive adapter's tick, then the
  indicator update.

## 5. Requirements

Format: the README tables (`tests/reqmatrix.py`). A retired row stays, starts with `RETIRED
2026-10-xx, superseded by REQ-...`, lists no tests; no test may cite it. C1's IDs are written
by C1's commits and retired here by C3's, so history matches the code at each commit.

### 5.1 Retired (all introduced by C1)

| ID | Successor | Why |
| --- | --- | --- |
| REQ-DRV-22 | REQ-DRV-35 | entry also needs the boot DISABLE done |
| REQ-DRV-30 | REQ-DRV-38 | the profile tap's ENABLE also needs POST_OK |
| REQ-DRV-32 | REQ-DRV-36 | seven tick sub-steps |
| REQ-DRV-33 | REQ-DRV-37 | every ENABLE row also needs POST_OK |

### 5.2 New

| ID | Component | Requirement |
| --- | --- | --- |
| REQ-DRV-35 | drive_session | On a tick, LOCKED or ASKING with the MCB ENABLED on a CONNECTED link unlocks (F1), asked or not (M1), unless a calibration is running, the Boot screen is up, or the boot DISABLE is not done yet. The POST gate is not read (rows 1-2) |
| REQ-DRV-36 | drive_session | A tick decides TICK_FOLLOW on the Env at its start, then TICK_BOOT_STOP, TICK_EXIT_DUE, TICK_STOP_FAULT_DUE, TICK_STOP_RESEND, TICK_WARN_DUE, TICK_GIVEUP_DUE, in that order, on one Env sampled after TICK_FOLLOW's actions |
| REQ-DRV-37 | drive_session | No row sends ENABLE except the unlock hold (row 18) and a profile tap (rows 31-32); each needs POST_OK; rows 31-32 also need DRIVING_OK |
| REQ-DRV-38 | drive_session | A profile tap re-publishes the request as it stands in LOCKED, ASKING, EXITING and EXIT_REFUSED. In UNLOCKING and DRIVING it sends ENABLE with the profile only when the MCB is ENABLED on a CONNECTED link and POST passed, else nothing |
| REQ-DRV-39 | drive_session | On the first tick with a CONNECTED link, LOCKED sends one DISABLE and marks the boot stop done; ASKING only marks it. It is never sent again until reset. The mark is set in every unlocked phase (rows 50-51) |
| REQ-DRV-40 | drive_session | The unlock hold applies only with POST passed. A completed unlock hold with the MCB ready and POST not passed stays LOCKED: ring at rest, REFUSED_POST banner, refusal feedback (row 52) |
| REQ-DRV-41 | drive_session | From LOCKED with the MCB ready and POST not passed, an entry push on the Locked screen with no menu, and the menu's DRIVE row, show the REFUSED_POST banner with refusal feedback (rows 53-54) |
| REQ-POST-14 | post | The rest-window accumulator starts at the first cycle with all three reads valid, counts every cycle after it, and emits one `StickWindow` per `WINDOW_MIN_SAMPLES` cycles, then starts a new one. Axis mean, min and max are over the valid cycles, rounded to whole mV; `button_idle` is true only if the button read released on every cycle of the window. A NaN read counts as a failed read |
| REQ-POST-15 | post | The runner stores PENDING at its first tick. It gathers the boot facts once, at the first window; merges every new window into the latched report; and stores the gate PASS or FAIL as the latched overall state. The gate never moves back. It is the gate's only writer |
| REQ-POST-16 | post | A LATCHED check still PENDING `POST_BUDGET_MS` after the first tick makes the gate FAIL ("timed out", that check blocking). A LIVE check has no budget |
| REQ-POST-17 | post | At PASS or FAIL the runner prints the TS-POST-05 lines once, in table order (§2.7). A LATCHED check without a measurement prints FAIL, a LIVE one SKIP. The reset reason line is printed at every boot |
| REQ-POST-18 | post | `post_indicator` gives NONE, CHECKING, WAITING, FAILED or NOT_RUN per §2.8 |
| REQ-POST-19 | post | The IDF adapter converts `esp_reset_reason()` and the OTA state with `static_assert`ed enum values; `verified` is true only when the build has no bootloader skip-validate option; memory and stack values saturate to `int32` |
| REQ-POST-20 | post | The ADC task feeds the accumulator on every cycle, valid or not, with the reads passed to the stick pipeline; it writes the window mailbox once per window and never waits; it stops once the gate is PASS or FAIL; it allocates nothing |
| REQ-STK-15 | stick | While the POST gate is not PASS, XYTwist carries the stick button released (only if Q11 = yes) |
| REQ-UI-19 | hmi_ui | The TopBar shows the POST indicator on every screen but Boot, within 250 ms of a change; no banner hides it and it cannot be dismissed |
| REQ-UI-20 | hmi_ui | A seat request publishes a SeatCommand only when the POST gate is PASS; otherwise it shows REFUSED_POST with refusal feedback and publishes nothing |
| REQ-UI-21 | hmi_ui | The About screen shows the last reset's reason |
| REQ-UI-22 | hmi_ui | `DrivePort::sample` reads the POST gate live (acquire); `post_ok` is true only for PASS |
| REQ-UI-23 | hmi_ui | Each UI poll tick runs the POST runner, then the drive adapter's tick, then the indicator |
| REQ-RUI-06 | remote_ui | Bench builds only: `STATE` adds `post`, `post_check`, `indicator`, `reset_reason`; `POST RERUN` restarts the runner from NOT_RUN (facts re-gathered, gate back to PENDING); `CRASH` aborts (a PANIC reset); `PERMIT POST` is applied by the runner; `CAL UNSAVED` also makes the POST fact `saved` false. A release ELF has none of their symbols |

Unchanged: C1's other new IDs; REQ-POST-01..13 (the evaluator, with the new window value).

**ID clash to fix in C4 (flag).** The C4 spec uses REQ-UI-16 and REQ-UI-17, which C1 already
uses. C4 lands after C1 and C3, so C4 renumbers to REQ-UI-24 onward.

## 6. Tests that prove it

Written from this spec; no expected value is generated from the new code.

### 6.1 Drive table (oracle, statics, session)

| ID | Test | Expected |
| --- | --- | --- |
| DRV-101..115 | by-input oracle over the new table; new dimensions POST_OK (2), BOOT_STOP_DONE (2) | every combination: the table's row or no change |
| DRV-117 | TICK_BOOT_STOP by input | the table's row (50, 51) or no change |
| DRV-116 (C1, updated) | a tick runs seven sub-steps in order on two Envs | the first CONNECTED tick with MIB ENABLED: DISABLE, no F1; a re-send due between the samples is still sent the same tick |
| DRV-029 | session: link down for 3 ticks, then up with MIB IDLE; 4 more ticks | one DISABLE on the first CONNECTED tick, nothing after |
| DRV-030 | session: unlock hold with POST PASS seen CONNECTED before any tick (ASKING), then a tick | no DISABLE from the tick; BOOT_STOP_DONE set; the ask stands |
| DRV-031 | over all states with POST_OK false | no row sends ENABLE |
| DRV-032 | rows 52, 53, 54 and their `!RDY` twins (19, 38, 40) | each refusal fires exactly where its guard says; never both |
| DRV-033 | MIB ENABLED, POST_OK false, BOOT_STOP_DONE true, Locked screen | row 1 fires (M1 before POST) |
| DRV-034 | safe state before the first link, then the first CONNECTED tick | the boot DISABLE is still sent |
| DSO-001, 004, 005, 006, 012 (updated) | sizes (rows 54, inputs 14, guards 23 = 16 env + 7 hidden, actions 41), invariants, fingerprint | the new values |
| DSO-018 (updated) | `TICK_SEQUENCE` | exactly §4.4's order |
| DSO-020 | BOOT_STOP_DONE invariant | true in all four unlocked phases; MARK_BOOT_STOP only in rows 50-51 |
| DSO-021 | every SEND_ENABLE row has `+POST_OK` | rows 18, 31, 32 |
| DSO-022 | rows 1-2 read `+BOOT_STOP_DONE` and not POST_OK | as stated |
| DSO-023 | `UNLOCK_APPLIES` needs POST_OK; SHOW_REFUSED_POST only with `!POST_OK` | as stated |

### 6.2 New goldens (hand-written, at the port, C1's filtered log)

New log token: `B:REFUSED_POST`. Fake POST gate: PASS unless stated.

| ID | Steps | Expected |
| --- | --- | --- |
| GLD-117 | boot; link down; 3 ticks; link up, MIB IDLE, Locked; 3 ticks | first CONNECTED tick `P(D)`; nothing else |
| GLD-118 | boot; link up, MIB ENABLED, Locked; tick; tick; +1000; timer | 1st tick `P(D)` only; 2nd `open, lock(0), gate`; timer `Dv`. No `P(E)` |
| GLD-119 | as GLD-118, MIB IDLE before the 2nd tick | 1st tick `P(D)`; then nothing |
| GLD-120 | booted, POST PENDING, MIB IDLE, Locked; unlock hold completes | `ring_rest, B:REFUSED_POST`; no `P` |
| GLD-121 | booted, POST PENDING; MIB ENABLED; tick; +1000; timer; profile LOW; POST PASS; profile LOW | `open, lock(0), gate`; `Dv`; profile with PENDING: nothing; with PASS: `P(E,LOW)` |
| GLD-122 | booted, POST FAIL, MIB IDLE, Locked; entry push; menu DRIVE row | `B:REFUSED_POST` twice; no `P` |
| GLD-123 | Boot screen, link up, MIB ENABLED; ticks to 1000; Locked screen; tick | first tick `P(D)` (the boot stop ignores the screen); no entry on Boot; Locked tick `open, lock(0), gate` |
| GLD-124 | link up, MIB IDLE; unlock hold before the first tick; tick | `ring_wait, P(E)`; the tick: nothing (no `P(D)`) |
| GLD-115 (C1, updated) | scenarios and seeded walks, new step kinds (POST gate, first link) | all 54 rows taken |

"Booted" = the preamble below.

### 6.3 POST (host L1, `components/post/test`)

| ID | Test | Expected |
| --- | --- | --- |
| POST-034 | accumulator: 5 invalid cycles, then valid ones | no window until 30 cycles after the first valid one |
| POST-035 | 30 valid cycles with known reads | one window: cycles 30, valid 30, exact mean/min/max per axis, samples 30; cycle 31 starts a new window |
| POST-036 | one invalid cycle inside a window (after the first valid) | cycles 30, valid 29; axis stats over the 29 |
| POST-037 | button pressed on one cycle | `button_idle` false; next window true |
| POST-038 | NaN read; reads at 0 and 4095 mV; rounding 0.5 | NaN = invalid cycle; no overflow; rounding as stated |
| POST-039 | runner, fake port: before the first tick; first tick; first window | gate NOT_RUN; PENDING; each fact group gathered exactly once |
| POST-040 | window with y +400 mV (about 25 %), then a centred window | PENDING, blocking `joy.y_cal_off`, indicator WAITING; then PASS |
| POST-041 | `i2c` without 0x5a, then good windows | FAIL, blocking `i2c.missing`; stays FAIL |
| POST-042 | no window at all | PENDING at 2999 ms; FAIL at 3000 ms, blocking `adc.valid`, timed out |
| POST-043 | latched all PASS, stick held off centre for 60 s | PENDING all along; never FAIL |
| POST-044 | every gate sequence the runner can produce | never PASS → other, never FAIL → other |
| POST-045 | the lines at PASS, at FAIL, at a timeout | the exact TS-POST-05 text; LATCHED unmeasured FAIL, LIVE SKIP; `<passed>/19` |
| POST-046 | `post_indicator` over every gate × blocking kind × verdict, and NOT_RUN before/after the budget | §2.8's table |
| POST-047 | reset TASK_WDT; UNKNOWN; SW | FAIL `sys.clean_reset`; FAIL; PASS |
| POST-048 | 1000 accumulator cycles and 100 runner ticks under the allocation guard | 0 allocations in the accumulator |
| POST-049 | `Overall` → `PostGate` mapping | PENDING, PASS, FAIL map one to one (static and run time) |
| POST-050 | the IDF adapter's enum static_asserts (L0 build) | builds against IDF v6.0; a must-not-compile twin with a wrong value fails |

### 6.4 Stick, UI

| ID | Test | Expected |
| --- | --- | --- |
| STK-067 (C1, unchanged) | POST gate values | NOT_RUN, PENDING, FAIL withhold; PASS allows |
| STK-068 | (Q11 = yes) button pressed, gate PENDING then PASS | bit released while PENDING; pressed after PASS |
| GLD-125 | `DrivePort` over a recording Ui: gate PENDING, then PASS | `sample().post_ok` false, then true (REQ-UI-22) |

The indicator view, the seat gate and the About row are checked on the bench (§7).

### 6.5 Existing tests and expected data that change (owner approval, each)

| # | What | Why |
| --- | --- | --- |
| F1 | C1's GLD-101..116 get a "booted" preamble: POST gate PASS, link up, MIB IDLE, one tick (the boot `P(D)`), log cleared. Their expected lines do not change | rows 1-2 now need the boot DISABLE done; without the preamble each would start with a `P(D)` |
| F2 | DSO-001, 012, 018 and DRV-116, 115 (C1's new values) | §4 counts, fingerprint, sequence |
| F3 | `post/checks.hpp`: `WINDOW_MIN_SAMPLES` 25 → 30 and the `D4:` comments | approved D4. POST-001..033 use the constant, so no expected value changes; the README table is updated |
| F4 | bench B2 boot check: new markers `POST RESULT PASS` and `POST reset reason:` (declared) | the POST lines |
| F5 | C1's B5''-15 (HMI reset, MCB ENABLED): "no non-zero XYTwist until 60 s (NOT_RUN)" becomes B5''-18b below | POST now passes; the boot DISABLE changes what the sim does |
| F6 | every C1 B5'' step's start condition `PERMIT POST pass` becomes "STATE post = PASS" | POST passes by itself |

Unchanged: the STK goldens (`golden_stick.inc`), the self test and its count marker, the G10
task baseline, the profile's other budgets.

## 7. Bench checks (B5'', C3 rows)

Same rig as C1 §6: the sim plays the MCB, the stick is injected (`STICK`, refreshed every
100 ms), verdicts by script from the sim's event log, `STATE` and the serial log. Times are the
PC's clock. Every step ends Locked with the stick centred.

The remote UI starts after the POST's first window on a real boot, so the bench cannot inject a
deflection before a real boot's POST. Steps 17-19 use `POST RERUN`, which restarts the same
runner code from NOT_RUN without a reset (Q12). Steps 16, 18b and 21 are real reboots.

| Step | Sim | Script | Pass if |
| --- | --- | --- | --- |
| B5''-16 POST at boot | IDLE | Restart HMI, stick centred (no injection); 5 boots | each boot: serial has 19 `POST <name> PASS` lines and `POST RESULT PASS 19/19 <ms>` with ms ≤ 3000; `STATE` post PASS, indicator NONE, reset_reason SW; About "Last reset: software"; sim: the HMI's first DriveCommand after the reboot is a DISABLE, and it is the only DriveCommand in the 10 s after it (no ENABLE) |
| B5''-17 stick bumped (the C3 row) | IDLE | inject y = centre + 25 % of (max − centre); `POST RERUN`; wait 3 s; unlock hold 2 s; centre; wait 3 s; unlock hold 2 s | after 3 s: post PENDING, post_check one of `joy.y_cal_off`/`joy.y_noise`, indicator WAITING "Centre the joystick", never FAIL; 1st hold: banner REFUSED_POST, no DriveCommand; after centring: post PASS within 2.5 s, indicator NONE; 2nd hold: 1 ENABLE |
| B5''-17b button held | IDLE | button pressed (remote press); `POST RERUN`; 3 s; release | post_check `joy.button_idle`, "Release the joystick button"; PASS within 2.5 s of release |
| B5''-18 ENABLED before POST pass | ENABLED, `s` (refuse DISABLE) | Drive; inject full forward; `POST RERUN`; hold 5 s; centre 3 s; forward 2 s | DriveScreen stays (M1); every XYTwist from RERUN + 0.2 s (the verb's own latency) until `STATE` first shows PASS is zero, at ≥ 25 Hz; notice names the check; after PASS and 0.5 s centred: first y > 0 within 0.3 s of forward |
| B5''-18b HMI reset, MCB ENABLED (replaces C1's B5''-15) | `ongone keep`, `a`, `s` | Restart HMI; inject forward from the remote UI's reconnect | the HMI's first DriveCommand after the reboot is a DISABLE, exactly 1 in its first 2 s of link; no ENABLE; DriveScreen within 3 s of the HMI's link; no non-zero XYTwist until the script centres 0.5 s; serial `POST RESULT PASS` present |
| B5''-18c same, MCB obeys | `ongone keep`, `a` | Restart HMI | 1 DISABLE; the sim goes IDLE; the HMI stays Locked for 10 s |
| B5''-19 seat before POST | IDLE | inject y +25 %; `POST RERUN`; Seat screen; press a seat row; centre; wait for PASS; press again | 1st press: no SeatCommand for 2 s, banner REFUSED_POST; 2nd press: exactly 1 SeatCommand |
| B5''-20 latched hardware FAIL | IDLE | `CAL UNSAVED`; `POST RERUN`; 3 s; centred; unlock hold | post FAIL, post_check `joy.cal_saved`, indicator FAILED "The joystick must be calibrated first"; stays FAIL for 10 s with good windows; hold refused; Restart HMI clears it (RAM only) |
| B5''-21 reset reason, unclean | IDLE | `CRASH`; wait for the reboot | serial `POST sys.clean_reset FAIL 0` and `POST RESULT FAIL`; `STATE` reset_reason PANIC, indicator FAILED "Restarted after a fault: PANIC"; About "Last reset: PANIC"; unlock hold refused; then Restart HMI → PASS, reset_reason SW |
| B5''-22 budget | IDLE | inject centre with fail_mask 7 (all reads fail); `POST RERUN` | post FAIL between 3.0 and 3.7 s after the RERUN (budget + up to two ticks + one poll), post_check `adc.valid`, "timed out" |
| B5''-22b one bad read | IDLE | centre; `POST RERUN`; within the first window, fail_mask 1 for one refresh | post FAIL, `adc.valid` below 990, latched |
| Regression | as C1 | C1's B5''-1..14 with F6 | PASS |

`STATE` reads are polled every 100 ms; a time "within X" is between the triggering command's
send time and the first poll that shows the result.

## 8. Implementation notes

### 8.1 Files

| File | Change |
| --- | --- |
| `components/post/include/post/checks.hpp` | `WINDOW_MIN_SAMPLES` 30; D4 comments; the 35 ms cycle |
| `components/post/include/post/{rest_window,runner,indicator}.hpp`, `src/` | accumulator, runner (with its port), `post_indicator`, `PostGate` mapping, the POST lines |
| `components/post/README.md`, `test/`, `tests/manifest.d/post.yaml` | REQ-POST-14..20, POST-034..050 |
| `components/drive_session/...` (types, table, fingerprint, session, TABLE.md, README, tests) | §4 |
| `components/drive_adapter/...` | seven tick steps |
| `components/hmi_ui` (`drive_port`, `topbar_view`, `refusal_view`, `ui_app` seat gate, `about_view`, UI poll order, `post_texts.hpp`) | §2.8-2.10, §4.5 |
| `components/hmi_rtps_spec` | REFUSED_POST banner words |
| `components/stick` | button bit (Q11) |
| `components/control/include/control/stick_island.hpp` | hand the reads passed to the pipeline to the Io once per cycle (refactor) |
| `main/main.cpp` | the `post_gate` atomic as the C1 hook's writer target; the window mailbox; the accumulator (owned by `Read ADC`); the IDF facts port; `I2cSet` from the boot scan; the runner object |
| `main/selftest.cpp` | none. `reset_reason_name` is copied to a shared header only if the owner agrees; otherwise duplicated with a parity test |
| `components/remote_ui` | REQ-RUI-06 |
| `tools/bench/` | B5''-16..22, B2 markers, C1 F5/F6 |
| docs | `project-profile.md` (POST budget, `components/post` now wired), `refactor.md` H2/H12 status, `hazard-fixes.md` C3 status, `how-the-firmware-works.md` boot section |

### 8.2 Commit order (refactor separate from behaviour; CI green at each)

C3's commits follow C1's seven. Nothing merges to `dev_refactor` until both are done and benched.

1. **Values** (no firmware behaviour: the evaluator is not wired): `WINDOW_MIN_SAMPLES` 30 and the
   D4 comments.
2. **Refactor**: accumulator, runner, indicator and their L1 tests (unused by the firmware); the
   island hands the pipeline's reads to the Io (release ELF behaviour unchanged).
3. **Bench tooling**: `STATE` fields, `POST RERUN`, `CRASH`, `PERMIT POST` via the runner
   (release ELF unchanged, symbol check).
4. **Behaviour, POST wiring**: accumulator on `Read ADC`, mailbox, runner on the UI poll, gate
   writes, POST lines, B2 markers. From here the stick drives again in normal builds.
5. **Behaviour, drive table**: §4, fingerprint, TABLE.md, oracle and DSO tests, GLD-117..125,
   the C1 golden preamble (F1, F2).
6. **Behaviour, UI**: indicator, REFUSED_POST, seat gate, About row, notice texts, button bit
   (Q11).
7. **Bench script**: B5''-16..22, C1's B5''-15 replaced (F5, F6).
8. **Docs**.

Then the bench run on board 2 (C1's B5'' and C3's), then the merge to `dev_refactor`.

### 8.3 Risks

- **ADC stack.** The accumulator and the mailbox write add to `Read ADC`'s frame before C4
  raises the stack to 6144 B. Worst free today 1188 B against `STK_ADC_MIN_B` 1024 B. The
  accumulator call is `noinline` with a frame ≤ 64 B, checked by `-fstack-usage` (C4's SU step).
  If it does not fit, take C4's stack change into C3 (Q13).
- **ADC warm-up.** If the continuous ADC still fails a read after its first valid cycle, every
  boot latches `adc.valid` FAIL. B5''-16's five boots and TS-POST-06's nightly boots measure it.
- **Board 1.** It has no 0x5a: it fails POST until Q6 is decided.
- **UI-task runner.** A stalled UI task leaves the gate PENDING (safe), but POST is safety logic
  on a non-safety task (CS-SAF-04 deviation, interim, Q7).
- **Stranding.** An unclean reset (C4's watchdog included) or a first calibration needs a clean
  reset before driving. Safe, but the user must know the way out: the texts say it.
- **Boot DISABLE vs a slow MCB.** One tick of Drive then relock if the MCB reports late (§2.5).

### 8.4 What must not change

- The evaluator's logic, `CHECKS` rows, kinds and order; `OVERALL_TRANSITIONS`.
- C1's rows other than 1, 2, 18, 31, 32; their order and action order.
- C1's permit rule, neutral latch and hold-reason order; `stick_drives()`; `GATE_TRIGGERS`.
- The self test (`main/selftest.cpp`); the G10 task table; XYTwist and DriveCommand on the wire.
- No allocation on the ADC path; no LVGL call off the UI task; the ADC task never waits on the
  mailbox or `lvgl_mutex`.
- A release build has no bench verb and no injection.

## 9. Open questions for the owner

1. **Q1 No POST phases.** POST is a gate plus an Env guard, not drive phases, because M1 must
   still enter Drive on ENABLED (G3). Replaces v2's "no row leaves POST_PENDING". Agree?
2. **Q2 Boot DISABLE.** One DISABLE on the first fresh link, entry one tick later, no re-send,
   no fault (§2.5). This reconciles v2's `DISABLE_PENDING` (gone in C1) with G4. Alternative:
   none, relying on M1 + the permit. Also: no extra DISABLE when POST fails with the MCB ENABLED.
   Depends on D3 Q2 and Q4. Proposed: send it.
3. **Q3 Unclean reset.** FAIL until a clean reset (power cycle or Restart HMI), as the evaluator
   does today; or show only. With C4's watchdog this strands the user after a trip. Proposed:
   latch (CS-SAF-03).
4. **Q4 First calibration.** Restart needed after the first saved calibration (evaluator
   unchanged). Alternative: re-judge the two calibration checks on a save (evaluator change).
   Proposed: restart.
5. **Q5 Budget.** `POST_BUDGET_MS` 3000 from POST start (not in D4). LIVE checks have no budget.
6. **Q6 Board 1 I2C.** Board 1 has no 0x5a and fails POST. Per-board list (Kconfig), or drop
   0x5a from the required list? Measure boards 1 and 3 first (the proposal's to-measure).
7. **Q7 Where the runner runs.** UI task now (stack, simplicity); the selftest island writing
   `POST_RESULT` to `control` in the islands work. A CS-SAF-04 deviation until then. Agree?
8. **Q8 Profile tap before POST.** Rows 31-32 send nothing before POST pass, even with the MCB
   ENABLED. Agree?
9. **Q9 Texts.** Indicator, banner, notice and About texts (§2.8, §4.5).
10. **Q10 Image check.** `verified` from the bootloader's own validation (a `static_assert`
    that no skip-validate option is set; 0 ms) rather than `esp_image_verify` at boot (a 4 MB
    flash read). Proposed: the bootloader.
11. **Q11 Button bit.** Send the stick button released while POST has not passed?
    Proposed: yes.
12. **Q12 Bench evidence.** Accept `POST RERUN` (bench builds only) as the evidence for "stick
    bumped at boot", since a real boot's POST finishes before the remote UI is up?
13. **Q13 ADC stack.** Take C4's 6144 B stack (and its G10 row) into C3 if the accumulator grows
    the frame by more than 64 B?
14. **Q14 C1 notice order.** C1 §2.7 lists "POST not passed" before "not calibrated"; C1 §3.3's
    hold-reason order is the reverse. C3 follows §3.3 (not calibrated first, so a fresh device
    says "calibrate first"). Fix C1 §2.7 to match?
15. **Q15 For C4.** C4 renumbers its REQ-UI-16/17 (clash with C1) and joins C1's permit as one
    more condition instead of `stick_drives`' multiply; its O1 re-arm then comes from C1's
    neutral latch.
16. **Out of scope, noted.** H12's battery "78%" and range "19 mi" placeholders stay.
