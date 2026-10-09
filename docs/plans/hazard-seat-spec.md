# Spec: hazard fix, seat path (H8, H10 seat, H6 seat)

Status: **spec for the owner's approval** (2026-10-09). No code, test data, table or golden
has been changed. A different agent implements it after approval (CS-SAF-05; D1: the owner is
the only approver for now). Base: `dev_ai_hazard_table` 670dcc7 (C1 + C3 implementation, not
merged yet). Code lands after C2b (`hazard-decisions.md` §4 step 5, lane D).

Inputs: `hazard-fixes.md` §4 "Seat path" (v2 rule), §9 (M1, G1-G5, D3, D4), §10; the owner's
answers (`hazard-decisions.md`); the C1, C3, C4 and C2 specs; hazards H6, H8, H10
(`refactor.md` §1); `how-the-firmware-works.md` §9; the seat code: `UiApp::seat_step`,
`seat_request`, `seat_apply_state`, `refuse_seat`, `action_seat_up`; `SeatView`;
`SettingsView::step` (Bench → Actuators); `ActionsView` (Skunk Works "Seat up");
`NavView::row_picked`; `hmi_rtps_spec` (`seat_field`, `seat_raw`, `seat_units`);
`rtps_comms_publish_seat`; the drive table rows 10-11; `RAMMP_SEAT_AXIS_TABLE` and
`SeatCommand` in the rammp-rtps submodule (c7db102); `scripts/rtps_mcb_sim.py`.

## 1. Exec summary

What this fix does, in one line each:

- **One seat gate.** Every SeatCommand the HMI sends (Seat screen, Bench → Actuators, Skunk
  Works "Seat up") passes one check at the press: link CONNECTED (MibStatus < 2 s), MCB IDLE,
  POST passed, stick not in FAULT, the axis position known. Otherwise nothing is sent and the
  user sees why.
- **Seat values validated (H8).** A NaN, an infinity or a value outside the axis range (plus
  one step of tolerance) from the MCB reads "unknown" ("--"). No undefined behaviour.
- **Unknown means unknown.** All seat values go "--" when the link goes stale. A step from an
  unknown value is refused (today it steps from the axis minimum: a "+" could drop the seat
  to the bottom).
- **A step moves at most one step** from the last reported position, and never against the
  press. A preset is an absolute target, as today.
- **Delivery checked (H6).** The publish result is checked and counted, a failure is logged
  and shown. The HMI never re-sends a SeatCommand: a lost press means no motion.
- **One press, one request.** A held key on the Actuators page no longer repeats seat steps.
- **No drive-table change.** The seat path has no phases or timers; it is one pure decision
  function with an exhaustive test (§2.11).

What the user sees:

| Situation | Seat screen | Actuators page, Skunk Works "Seat up" | SeatCommand |
| --- | --- | --- | --- |
| All good | number moves when the MCB reports it (≤ 0.5 s) | same | one per press |
| Link down or MibStatus ≥ 2 s | numbers "--"; back to Locked with "SEAT REFUSED: RTPS LINK" (rows 10-11, as today) | numbers "--", ErrorBanner6; a press is refused | none |
| MCB not IDLE | ENABLED: Drive (M1). ERROR, INITIALIZING: home, banner (rows 10-11) | press refused, "SEAT REFUSED: MCB STATE" | none |
| POST not passed | press refused, "Seat not ready" + the check (C3) | same | none |
| Stick FAULT | press refused, "Seat not ready" + "Joystick fault: recalibrate to clear" | same | none |
| That axis "--" (not reported, NaN, out of range) | press refused, "SEAT REFUSED: POSITION UNKNOWN" | buttons greyed (as today), feedback only | none |
| Step at or past the limit | feedback only (double click), no banner | same (as today) | none |
| Publish failed | "SEAT REFUSED: NOT SENT", press again | same | none reached the wire |
| Key held 5 s on the Actuators page | — | one step | one |

**Residual hazards, stated plainly.**

1. **The HMI cannot stop a seat move.** SeatCommand is an absolute target. The protocol has no
   stop. Once sent, the MCB moves the seat to the target. A step is small (2.5° tilt, 5 mm
   elevation, 2.5 mm translation). A preset can cross an axis' whole range (FB tilt 90° → 0°).
2. **Link loss, stale MibStatus, an HMI reset or a hung UI task during a move: the MCB finishes
   the move on its own**, unless it has its own timeout (unknown, §8 S1). The HMI shows "--"
   and sends nothing.
3. **A lost SeatCommand is not detected.** BEST_EFFORT: "published" is not "delivered". The
   user sees the number not move and presses again.
4. **The MCB state the gate reads is up to 0.5 s old** (one MibStatus period, 2 s at worst). A
   press just after the MCB leaves IDLE (for example to ENABLED) is sent. The MCB must refuse
   seat moves while driving (§8 S3).
5. **Phantom touches** (a touch panel fault) can press seat buttons. One press = one step or
   one preset. Not detected.
6. **Wrong but plausible seat values** (in range) are shown and stepped from. A step then goes
   one step from the wrong value.
7. **Stranding.** After a POST FAIL (latched until a clean reset) or a stick FAULT (until a
   recalibration or reboot), the HMI cannot move the seat at all. A user left tilted or raised
   stays there until then, unless the MCB has another seat control (§8 S8).
8. **The MCB is the authority.** If it moves the seat on its own, or for another participant,
   the HMI only shows it.

## 2. Rules

### 2.1 Who decides seat motion (S1, the seat side of M1)

- **The MCB decides.** It executes SeatCommands, clamps targets to its ranges, refuses seat
  motion while driving, and owns the position. Same authority as M1 gives it for driving.
- **The HMI only asks, and only through the gate (§2.2).** It never asks on its own: no seat
  request at boot, on link return, on a screen change, or as a re-send.
- **The HMI shows only what the MCB reports.** A press never changes a number on screen
  (unchanged).
- **The HMI has no seat stop** (residual 1). This fix does not add one (Q6).
- Unlike driving, there is no seat "ENABLED" for the HMI to follow, so M1's "follow the MCB"
  has nothing to act on here. The gate is a request-side filter only.

### 2.2 The seat gate

One pure function decides each press, from inputs read live at the press on the UI task. It
returns ALLOW (with the target) or the first failing reason, in this order:

| # | Reason | Allows only when | Read from |
| --- | --- | --- | --- |
| 1 | `LINK_DOWN` | link CONNECTED (network up, MibStatus < 2 s) | `LinkPort::state()`, live, as C1's REQ-UI-18 (not the 250 ms `rtps_link` subject) |
| 2 | `MCB_NOT_IDLE` | MCB state IDLE exactly (a value outside the enum refuses) | `mib_state` subject (last MibStatus) |
| 3 | `POST_NOT_PASSED` | POST gate PASS (NOT_RUN, PENDING, FAIL, outside the enum refuse) | `permit_hooks_.post_gate`, acquire |
| 4 | `STICK_FAULT` | stick health not FAULT (NOT_MONITORED, OK, CHECK allow; outside the enum refuses) | `permit_hooks_.stick_health`, acquire (Q3) |
| 5 | `POSITION_UNKNOWN` | that axis' value known (§2.3) | `seat_axis_value_[axis]` |
| 6 | `AT_LIMIT` | a step's target moves the axis in the press' direction (§2.4) | the target |

After ALLOW the HMI publishes. A false publish result gives `NOT_SENT` (§2.5).

- Order: the MCB first (link, state), as the drive refusals do (C3 rows 52-54 come after the
  `!RDY` rows 19, 38-41); then POST before stick, as C1's hold-reason order and C2b row 57.
- 1 and 2 are today's `seat_ready()`, but with the link read live.
- 3 is C3's REQ-UI-20 (C3 §2.10), kept as it lands.
- 4 is FAULT only: CHECK covers INIT (~128 ms at boot), SUSPECT and RECOVERING (~0.1 s), and
  CALIBRATING (Joystick screen only). The stick keys are already silent in SUSPECT and FAULT
  (C2).
- C4's motion guard is not a seat condition. It guards the stick output on the ADC task. A seat
  press runs on the UI task, so a stalled UI sends nothing, and the gate's conditions 1-2 cover
  C4's MibStatus age, link and MCB state at the press.

### 2.3 Seat values from the MCB (H8)

- Every `MibStatus.currentSeatState` field goes through `seat_reading(spec, value)` before it
  reaches a subject. Known only when:
  - the value is finite (not NaN, not ±inf);
  - |value × scale| < 2^23, so the conversion to raw is exact and defined;
  - the rounded raw value is within [min − step, max + step] of that axis (tolerance one step,
    Q4).
- Otherwise the axis reads `VALUE_UNKNOWN`: "--" on screen, both Actuators buttons greyed.
- Raw values inside the tolerance band are shown as reported (for example "-46.0°").
- `seat_raw` stays as it is, with a stated precondition. Only `seat_reading` calls it in the
  firmware (a guard test checks the call sites).
- **Stale → unknown.** On every UI poll tick (250 ms) that finds the link not CONNECTED, every
  axis is set to `VALUE_UNKNOWN`. It stays unknown until a MibStatus arrives on a CONNECTED
  link. So "--" shows within 2.25 s of the last MibStatus.
- Today a value, once known, is kept forever, through link loss.

### 2.4 Targets

| Press | Target (raw, then whole units on the wire) | Refused when |
| --- | --- | --- |
| "-" / "+" (Seat screen, Actuators page), Skunk Works "Seat up" (elevation "+") | clamp(value + dir × step, min, max) | value unknown; or the target is not strictly beyond the value in the press' direction (`AT_LIMIT`) |
| Preset (Seat screen, "0°", "15°", "25°" read off the label) | clamp(preset, min, max) | value unknown (Q5) |

- A step's target is never more than one step from the last reported value. Rapid presses do
  not add up: each starts from the reported value, which updates every 0.5 s.
- `AT_LIMIT` fixes a direction flip: a value reported in the tolerance band below min, with "-"
  pressed, would clamp to min and move the seat **up**. At exactly the limit, today's Seat
  screen sends a no-op target; after this it is refused (feedback only, as the Actuators page
  does today).
- Every target sent is finite and within [min, max] in whole units.
- A step during a preset's move starts from the reported value, so it may reverse the move.
  The newest request wins (absolute targets). Unchanged, stated here.

### 2.5 Delivery (H6, seat)

- `LinkPort::publish_seat`'s result is checked (today `(void)`).
- A false result: counted (`seat_publish_failures`), logged at most once a second
  ("SeatCommand <axis> not published (<n> failed since start)"), and shown: banner "SEAT
  REFUSED: NOT SENT" with refusal feedback. Same pattern as C1's REQ-DAD-09 for DriveCommand.
- Sent commands are counted too (`seat_sent`), for the bench.
- **No re-send, ever (Q7).** A re-sent absolute target could move the seat after the user has
  moved on. A lost press is the safe failure: no motion. The user's next press is the retry.

### 2.6 Held input (stuck button)

- Seat screen and Skunk Works: a step or preset fires on LVGL's CLICKED, that is on release.
  A held touch, or a held stick button (`select_key` is one-shot), gives at most one request.
  Unchanged; now a requirement.
- Bench → Actuators: today a held stick key repeats every 250 ms (after 500 ms), and each
  repeat steps from the reported value. A stuck key walks the seat to its limit at about one
  step per MibStatus. After this fix a seat row steps once per key press; repeats are ignored
  until the key is released (Q8). Settings rows that are not seat axes keep their repeat.

### 2.7 Where the gate applies

| Entry point | Today | After |
| --- | --- | --- |
| Seat screen "-"/"+", presets (`SeatView` → `seat_step` / `seat_request`) | no check (H10) | the gate |
| Bench → Actuators rows (`SettingsView::step` → `seat_step`) | greyed only at a limit or unknown; ErrorBanner6 shows `!seat_ready` but the press is still sent | the gate |
| Skunk Works "Seat up" (`action_seat_up` → `seat_step`) | greyed while `!mcb_ready` (IDLE **or ENABLED**) | greying unchanged; the press goes through the gate |
| Menu "Seat Functions" row (`NavView::row_picked`) | refused while `!mcb_ready` | refused by the gate's conditions 1-4 (not 5-6), same banners (Q10) |
| Drive table rows 10-11 (on Seat, MCB not ready → home) | as is | unchanged |

All three publish paths already meet in `UiApp::seat_request`. The gate sits there, in one
function, and it is the only caller of `publish_seat` (a guard test checks it).

### 2.8 Refusal banners

| Reason | Banner (`refused` value) | Title, body | Clears early when |
| --- | --- | --- | --- |
| `LINK_DOWN`, `MCB_NOT_IDLE` | `REFUSED_SEAT` (as today) | live cause: "SEAT REFUSED: RTPS LINK" / "SEAT REFUSED: MCB STATE" | the MCB is ready (as today) |
| `POST_NOT_PASSED` | `REFUSED_POST` (C3) | "Seat not ready", the check's text | POST passes (C3) |
| `STICK_FAULT` | C2b's stick refusal value | "Seat not ready", "Joystick fault: recalibrate to clear" | the stick leaves FAULT |
| `POSITION_UNKNOWN` | new `REFUSED_SEAT_UNKNOWN` | "SEAT REFUSED: POSITION UNKNOWN", "Wait for the MCB's seat position" | the axis is known |
| `NOT_SENT` | new `REFUSED_SEAT_NOT_SENT` | "SEAT REFUSED: NOT SENT", "Press again" | never (its 3 s) |
| `AT_LIMIT` | none | feedback only | — |

- Every refusal plays the refusal feedback (`cues->refused()`), as `refuse_seat` does today.
- New `Refused` values are appended after the last one C2b adds; numbers are set in the commit.
- Texts are new (Q11); all in `hmi_rtps_spec`, sized for `kErrorTextLen` / `kErrorFooterLen`.

### 2.9 Reconciliation with C1, C3, C4, C2

| Spec | What it says | Seat path |
| --- | --- | --- |
| M1 / C1 | The MCB decides driving; the HMI follows ENABLED | The MCB decides the seat too (§2.1). The gate needs IDLE, so the HMI never asks for seat motion while it would follow a drive. MCB ENABLED on the Seat screen: row 1 takes the HMI to Drive (unchanged) |
| C1 G4 | The HMI's stop is a request; the MCB wins | No seat stop exists at all (residual 1, Q6) |
| C1 REQ-UI-18 | Link read live at the sample | The gate reads the link live too |
| C1 REQ-DAD-09 | DriveCommand publish checked and counted | SeatCommand the same (§2.5), but no re-send: drive re-sends a stop, a seat re-send would be a move |
| C3 REQ-UI-20 | Seat publish only with POST PASS, REFUSED_POST | Kept as condition 3; REQ-UI-20 retired into REQ-UI-28 (one gate) |
| C3 boot DISABLE | One DISABLE at the first fresh link | No seat equivalent: nothing to send. No seat request at boot |
| C4 | Stick output 0 when UI stale, MibStatus stale, link down, MCB not ENABLED | Not a seat condition (§2.2). Its watchdog reset is "HMI reset mid-actuation" (§2.10) |
| C2 | Stick FAULT latched, keys silent | Condition 4. FAULT only (Q3). Keys already silent, so a faulty stick cannot press seat rows |
| C2b D9 | Refuse unlock, entry push, DRIVE row with a FAULT | Seat refusal uses the same stick text |

### 2.10 What happens when

| Event | HMI | MCB (assumed, §8) | Residual |
| --- | --- | --- | --- |
| Link loss during a move | "--" within 2.25 s; Seat screen → home with the link banner (rows 10-11); every press refused; nothing sent at link return | finishes the move, or stops if it has a link timeout (S1) | 1, 2 |
| Stale MibStatus (link up, MCB silent) | same as link loss (CONNECTED needs MibStatus < 2 s) | unknown | 2 |
| MCB ignores a seat "stop" | there is no seat stop; the HMI cannot send one | — | 1 (Q6, S2) |
| MCB ignores a SeatCommand (or it is lost) | the number does not move; no re-send; the user may press again | — | 3 |
| Stuck touch or stick button | at most one request, at release | — | 5 |
| Stuck stick key, Actuators page | one step (§2.6). C2 FAULT/SUSPECT silence the keys anyway | — | — |
| HMI reset mid-move (crash, watchdog, Restart HMI, power) | after boot: values "--" until the first MibStatus; no seat request until the user presses and the gate passes (POST, link, IDLE, value known). An unclean reset fails POST until a clean one (C3): no seat moves until then | finishes the move, or stops if it notices the HMI gone (S1) | 2, 7 |
| UI task hung (C4 watchdog resets at 2 s) | no presses processed; then as an HMI reset | as above | 2 |
| MCB goes ENABLED during a move | HMI → Drive (M1); seat presses refused (not IDLE) | does ENABLE stop the seat? (S3) | 4 |
| MCB moves the seat on its own | the number follows; the HMI sends nothing | — | 8 |
| MCB reports NaN / out of range | that axis "--"; its presses refused; no UB | — | 6 |

### 2.11 Does the seat need a table?

No. The drive table exists because driving has phases, timers, re-sends and screen changes.
The seat path has none: each press is decided alone, from six inputs, and nothing is
remembered between presses. A pure decision function, `constexpr` with a reason-order array
checked by `static_assert`, and an exhaustive L1 test over every input combination give the
same assurance as a table and its oracle (§5). A table becomes worth it only if the owner picks
a seat stop or hold-to-run (Q6), which adds state.

## 3. Constants

| Constant | Value | Where | Status |
| --- | --- | --- | --- |
| `kSeatRangeTolerance` | 1 step of the axis (raw: `spec.step`) | `components/seat` | **proposed** (Q4) |
| conversion bound | \|value × scale\| < 2^23 | `components/seat` | fixed (float exactness, RTPS-006) |
| fresh link | MibStatus age < 2000 ms (`kMibStatusTimeout`) | existing | approved D4 |
| stale → unknown | the first UI poll tick not CONNECTED (`kTickPeriod`, 250 ms) | existing tick | — |
| `kSeatPublishFailLogMs` | 1000 ms | `hmi_ui` | as C1's `kPublishFailLogUs` |
| seat refusal banner | 3000 ms (`DRIVE_REFUSED_SHOW_MS`) | existing | — |
| seat re-send | none | rule | **proposed** (Q7) |
| axis ranges, steps | `RAMMP_SEAT_AXIS_TABLE` (shared spec) | rammp-rtps | unchanged |

## 4. Requirements

Format: the README tables (`tests/reqmatrix.py`). A retired row stays, starts with `RETIRED
2026-10-xx, superseded by REQ-...`, lists no tests; no test may cite it. IDs checked against the
C1, C3, C4 and C2 specs and every `origin/dev_ai_hazard_*` and `dev_refactor` branch
(2026-10-09): REQ-SEAT is a new series; REQ-UI ends at 27 (C2); REQ-RUI ends at 08 (C2).

### 4.1 Retired

| ID | Successor | Why |
| --- | --- | --- |
| REQ-UI-20 (C3) | REQ-UI-28 | the seat gate gains the link, MCB state, stick and position conditions |

### 4.2 New

| ID | Component | Requirement |
| --- | --- | --- |
| REQ-SEAT-01 | seat | `seat_reading(spec, v)` is known only when v is finite, \|v × scale\| < 2^23 and the rounded raw value is within [min − step, max + step]; then it returns that raw value. Otherwise `VALUE_UNKNOWN`. No input bit pattern reaches undefined behaviour |
| REQ-SEAT-02 | seat | The gate returns ALLOW only when the link is CONNECTED, the MCB state is IDLE, the POST gate is PASS, stick health is not FAULT and is inside its enum, and the axis value is known; otherwise the first failing reason in the order LINK_DOWN, MCB_NOT_IDLE, POST_NOT_PASSED, STICK_FAULT, POSITION_UNKNOWN |
| REQ-SEAT-03 | seat | A step's target is clamp(value + dir × step, min, max). It is refused with AT_LIMIT unless the target is strictly beyond the value in the press' direction. An allowed step's target is at most one step from the value |
| REQ-SEAT-04 | seat | A preset's target is clamp(preset, min, max); it needs the axis value known |
| REQ-SEAT-05 | seat | Every allowed target, in whole units, is finite and within [min, max] / scale |
| REQ-SEAT-06 | seat | The component is pure: no allocation, no LVGL, no IDF, no lock; the gate and the reading are `constexpr` |
| REQ-UI-28 | hmi_ui | Every SeatCommand (Seat screen, Bench → Actuators, Skunk Works "Seat up") goes through one function that applies the seat gate to inputs read at the press: the link live (`LinkPort::state`), the MCB state, the POST gate and stick health (acquire), the axis value. A refused press publishes nothing, plays the refusal feedback and shows the reason's banner (§2.8). That function is the only caller of `publish_seat` |
| REQ-UI-29 | hmi_ui | The SeatCommand publish result is checked. A failure is counted, logged at most once a second, and shown (REFUSED_SEAT_NOT_SENT, refusal feedback). The HMI never re-sends a SeatCommand and sends none except on a user press that passed the gate |
| REQ-UI-30 | hmi_ui | Each MibStatus seat field reaches its subject only through `seat_reading`. On every UI poll tick with the link not CONNECTED every axis is set unknown; it stays unknown until a MibStatus arrives on a CONNECTED link |
| REQ-UI-31 | hmi_ui | A held key or touch makes at most one seat request per press: Seat screen and Skunk Works act on release; a Bench → Actuators seat row ignores key repeats until the key is released |
| REQ-UI-32 | hmi_ui | The menu's Seat Functions row is refused when the seat gate's conditions LINK_DOWN to STICK_FAULT fail, with that reason's banner (Q10) |
| REQ-UI-33 | hmi_ui | A seat refusal banner stays its 3 s and clears early only when its own reason clears (§2.8) |
| REQ-RUI-09 | remote_ui | Bench builds only: `STATE` adds `seat` (per axis: raw value or null), `seat_sent`, `seat_refused`, `seat_reason` (last), `seat_publish_failures`; `SEATFAIL <n>` makes the next n seat publishes return false. A release ELF has none of their symbols |

Unchanged: C1's REQ-UI-16..18, C3's REQ-UI-19, 21-23, C4's 24-25, C2's 26-27; REQ-RUI-05..08.

## 5. Tests that prove it

Written from this spec. No expected value is generated from the new code.

### 5.1 The seat component (host L1, new app `tests/host/seat`, L1-SEAT, UBSan on)

| ID | Test | Expected |
| --- | --- | --- |
| SEAT-001 | `seat_reading` of quiet NaN, −NaN, signalling NaN, +inf, −inf, 1e30, −1e30, 3e8, FLT_MAX, −FLT_MAX, every axis | UNKNOWN; no UBSan report |
| SEAT-002 | the conversion bound: \|value × scale\| = 2^23 − 1, 2^23, 2^24, 2^31, 2^32, both signs | UNKNOWN (all out of range); the bound is checked before any float → int conversion: no UBSan report |
| SEAT-003 | range edges per axis: raw min − step, min − step − 1, max + step, max + step + 1, as whole-unit floats; and −47.54°/−47.56° on FB tilt | known, unknown, known, unknown; −47.54 → −475 known, −47.56 → −476 unknown |
| SEAT-004 | every raw in [min − step, max + step], every axis: `seat_reading(seat_units(raw))` | == raw |
| SEAT-005 | gate, exhaustive: link {CONNECTED, NO_PEER, DOWN} × MCB state {INITIALIZING, IDLE, ENABLED, ERROR, 4, 255} × POST {NOT_RUN, PENDING, PASS, FAIL, 7} × health {NOT_MONITORED, OK, CHECK, FAULT, 9} × value {known, UNKNOWN} × axis | ALLOW exactly when CONNECTED, IDLE, PASS, health in {NOT_MONITORED, OK, CHECK}, known; else the first failing reason in REQ-SEAT-02's order |
| SEAT-006 | steps: value at min − step, min, min + 1, mid, max − 1, max, max + step; dir ±1; every axis | target = clamp(v + dir × step); AT_LIMIT at min with −1, max with +1, min − step with −1, max + step with +1; otherwise ALLOW with \|target − v\| ≤ step and sign(target − v) = dir |
| SEAT-007 | presets 0, 150, 250, −10, 99999 raw on every axis, value known / unknown | clamp(preset); POSITION_UNKNOWN when unknown |
| SEAT-008 | property: 100 000 random float values and random inputs | every ALLOW target finite, within [min, max] / scale |
| SEAT-009 | `static_assert`s: reason order array matches REQ-SEAT-02; a few gate and reading cases evaluated at compile time | builds |
| SEAT-010 | 10 000 gate calls and readings under the allocation guard | 0 allocations |
| SEAT-011 | guard: in the firmware sources, `seat_raw` is called only in `components/seat`, `publish_seat` only in the gate function of `ui_app.cpp` | as stated (grep over `components/` and `main/`, tests excluded) |

### 5.2 Existing tests

| ID | Change |
| --- | --- |
| RTPS-009..013 | unchanged: they pin `seat_raw`'s undefined domain, which stays (now a precondition). Titles keep "H8 hazard"; the README says the seat path reaches `seat_raw` only through `seat_reading` |
| C3's B5''-19 | unchanged, must still pass (POST refusal on the Seat screen, then exactly 1 SeatCommand) |
| bench B4b (`ui_models_check.py`) | unchanged (presses no seat button); needs a live IDLE MCB, POST PASS and the stick OK, as on every bench run |

No drive-table test, golden, fingerprint or oracle changes: the table is not touched.

### 5.3 MCB sim and bench graders (L0)

| ID | Test | Expected |
| --- | --- | --- |
| SIM-015 | `seatval <axis> nan\|inf\|-inf\|<v>` then `seatval <axis> off` | the packed MibStatus carries the override for that axis only, then the model's value |
| SIM-016 | `seatdrop 2`, three SeatCommands | the first two logged `dropped`, model unchanged; the third applied |
| SIM-017 | `seatslow on`, target 10 steps away | the model moves one axis step per MibStatus period, logs `seat_move`, stops at the target |
| BENCH-055..071 | one pass and one fail fixture per B5s grader (§6, 17 steps), as `hazard_selftest.py` does for C3 | each grader passes its pass fixture and fails its fail fixture |

BENCH-0xx numbers follow `dev_refactor`'s last (054); renumber at the commit if taken.

## 6. Bench checks (B5s, sim only, never real motion)

Rig: the sim plays the MCB (no chair, no actuators); the rig's preflight asserts the sim is the
only other RTPS participant. Verdicts by script from the sim's event log (`seat_command`,
`seat_move`, `mib_publish` with the seat values), `STATE` (polled every 100 ms) and the serial
log. Every step starts with the sim IDLE, POST PASS, the stick centred and OK, every axis
known, and ends on the Locked screen.

New sim modes (`rtps_mcb_sim.py`, `mcb_sim_logic.py`):

| Mode | Does |
| --- | --- |
| `seatval <axis> <value>` / `off` | publishes that value for the axis instead of the model's (nan, inf, -inf, 1e30, any number) |
| `seatdrop <n>` | the next n SeatCommands are dropped (logged, not applied) |
| `seatslow on\|off` | the model moves one axis step per MibStatus period toward the target, instead of at once |

Existing modes used: `p` / `r` (pause, resume MibStatus), `e`, `z`, `ok`, `mark`. New HMI bench
verb: `SEATFAIL <n>` and the `STATE` seat fields (REQ-RUI-09). Stick FAULT: C2's injection
recipe (C2-3: horizontal 3300 mV for 0.5 s).

| Step | Sim | Script | Pass if |
| --- | --- | --- | --- |
| B5s-1 baseline | IDLE | Seat screen, Elevation, "+" once | exactly 1 SeatCommand, axis 2, target = reported + 5.0 mm, within 0.5 s of the press; `STATE` seat[2] shows it within 1.0 s |
| B5s-2 NaN | `seatval 2 nan` | wait 1 s; "+"; preset "15°"; `seatval 2 off`; wait 1 s; "+" | seat[2] null within 1.0 s; 0 SeatCommands for 2 s after each press, `seat_reason` POSITION_UNKNOWN, banner REFUSED_SEAT_UNKNOWN; no reboot (no boot line in serial); after `off`: known within 1.0 s, then exactly 1 SeatCommand |
| B5s-3 inf, huge | `seatval 2` inf, -inf, 1e30, -1e30 in turn | as B5s-2 for each | as B5s-2 |
| B5s-4 tolerance band | `seatval 2 255.0`, then `seatval 2 255.1` | at 255.0: "+" then "-"; at 255.1: wait 1 s | 255.0: known; "+" → 0 SeatCommands, AT_LIMIT, no banner; "-" → 1 SeatCommand, target 250.0. 255.1 (raw 2551): seat[2] null |
| B5s-5 link loss | IDLE | Actuators page; `p`; at +3 s press Elevation "+" (touch and KEY RIGHT); at +5 s `r` | every seat value null no earlier than 1.5 s and no later than 2.9 s after `p`; 0 SeatCommands from `p` to `r` + 2 s; ErrorBanner6 up; values known within 1.0 s of `r`; no SeatCommand caused by `r` |
| B5s-6 link loss mid-move | `seatslow on` | Seat screen, Elevation, preset "0°"; at +2 s `p`; at +7 s `r` | exactly 1 SeatCommand in [press, r + 5 s]; Seat screen → Locked with the link banner within 2.9 s of `p`; the sim's `seat_move` log continues through the pause (residual 2, recorded) |
| B5s-7 HMI reset mid-move | `seatslow on` | preset "0°"; at +1 s Restart HMI tile; wait for the HMI's link; wait 10 s; "+" once | 0 SeatCommands from the restart tap to the "+" press; seat values null in `STATE` until the first MibStatus after the link; serial `POST RESULT PASS`; the "+" gives exactly 1 SeatCommand, target = reported + step |
| B5s-8 MCB not IDLE | `e`, then `z`, then `ok` | Actuators page: KEY RIGHT on Elevation in each state; Skunk Works "Seat up" in each state | `e`, `z`: 0 SeatCommands, `seat_reason` MCB_NOT_IDLE (Actuators) or the tile greyed with feedback only (Skunk Works); `ok`: exactly 1 each, target = elevation + 5.0 |
| B5s-9 POST | IDLE | as C3's B5''-19, then the same on the Actuators page and Skunk Works | B5''-19's verdicts; Actuators and Skunk Works: 0 SeatCommands before PASS, `seat_reason` POST_NOT_PASSED; 1 each after |
| B5s-10 stick FAULT | IDLE | inject horizontal 3300 mV 0.5 s; centre; Seat screen by touch; TAP "+"; Restart HMI; TAP "+" | `STATE` stick FAULT; 0 SeatCommands, banner "Seat not ready" with the C2 text; after the reboot exactly 1 |
| B5s-11 stuck key | IDLE | Actuators page, Elevation row; `KEY RIGHT` 5 s; `KEY NONE` | exactly 1 SeatCommand in [key, key + 6 s] (today about 10) |
| B5s-12 stuck touch | IDLE | Seat screen, Elevation page; `PRESS` on "+" 5 s; `RELEASE` | 0 SeatCommands during the hold; at most 1 within 0.5 s of the release |
| B5s-13 publish failure | IDLE | `SEATFAIL 1`; "+"; watch 5 s; "+" | first press: 0 SeatCommands at the sim, `seat_publish_failures` +1, banner REFUSED_SEAT_NOT_SENT, serial "SeatCommand ELEVATION not published (1 failed since start)"; nothing for 5 s (no re-send); second press: exactly 1 |
| B5s-14 lost command | `seatdrop 1` | "+"; watch 5 s | the sim logs 1 dropped SeatCommand and no other in 5 s; `seat_publish_failures` unchanged; the value unchanged |
| B5s-15 rapid presses | IDLE | 10 TAPs on Elevation "+" within 2 s | every SeatCommand's target ≤ the elevation in the latest `mib_publish` before it + 5.0 mm |
| B5s-16 menu row | `e`; then IDLE with a stick FAULT | menu, Seat Functions | refused, the reason's banner, the menu stays; 0 SeatCommands (Q10) |
| B5s-17 MCB moves on its own | IDLE | Seat screen; `seatval 2 100`; wait 3 s | seat[2] = 100.0 within 1.0 s; 0 SeatCommands |
| Regression | as C1, C3, C4, C2 left them | their B5'' and C2 steps | PASS |

Times "within X" are from the triggering command's send time to the first `STATE` poll or sim
event that shows the result.

## 7. Implementation notes

### 7.1 Files

| File | Change |
| --- | --- |
| `components/seat/` (new) | `include/seat/seat_gate.hpp` (`SeatReason`, `SeatInputs`, `decide`, `step_target`, `preset_target`, the reason-order array), `include/seat/seat_reading.hpp` (`seat_reading`), `CMakeLists.txt` (pure; depends on `hmi_rtps_spec`, `hmi_format` for `VALUE_UNKNOWN`, `stick` for `PostGate`/`StickHealth`), `README.md` (REQ-SEAT-01..06) |
| `tests/host/seat/`, `tests/manifest.d/seat.yaml` | L1-SEAT, SEAT-001..011, UBSan, the allocation guard |
| `components/hmi_ui/src/ui_app.cpp`, `include/hmi_ui/ui_app.hpp` | `seat_request` becomes the gate function (live inputs, publish check, counters, banners); `seat_step` and presets pass through it; `seat_apply_state` uses `seat_reading`; a hook from the UI poll sets every axis unknown when not CONNECTED |
| `components/hmi_ui/src/settings_view.cpp` | seat rows step once per key press (REQ-UI-31) |
| `components/hmi_ui/src/nav_view.cpp` | the Seat Functions row asks the gate (Q10) |
| `components/hmi_ui/src/refusal_view.cpp`, `include/hmi_ui/refusal_texts.hpp` | the new seat banners and their early-clear rules |
| `components/drive_ui/include/drive_ui/refused.hpp`, `drive_port.hpp` | `REFUSED_SEAT_UNKNOWN`, `REFUSED_SEAT_NOT_SENT` (appended), names, dwell |
| `components/hmi_rtps_spec/include/hmi_rtps_spec.hpp`, `README.md` | the new seat texts; `seat_raw`'s precondition in its comment |
| `components/remote_ui`, `main/main.cpp` | `STATE` seat fields, `SEATFAIL` (bench builds only) |
| `scripts/rtps_mcb_sim.py`, `scripts/mcb_sim_logic.py`, `tests/host/mcb_sim` | `seatval`, `seatdrop`, `seatslow`; seat values in `mib_publish`; SIM-015..017 |
| `tools/bench/scenario_seat.py` (new), `hazard_grade.py`, `hazard_selftest.py` | B5s-1..17 and their graders, BENCH-055..071 |
| docs | `how-the-firmware-works.md` §9 and the H table, `refactor.md` H6/H8/H10 seat status, `hazard-fixes.md` seat status, this spec's status |

### 7.2 Commit order (refactor separate from behaviour; CI green at each)

The seat code lands after C2b (lane D) and after C3's REQ-UI-20 has landed from lane L1.

1. **Refactor**: `components/seat` and L1-SEAT (unused by the firmware).
2. **Bench tooling**: sim modes and SIM-015..017; `STATE` seat fields and `SEATFAIL` (release
   ELF unchanged, REQ-RUI-01 symbol check).
3. **Behaviour, seat values**: `seat_reading` in `seat_apply_state`; unknown on stale
   (REQ-UI-30).
4. **Behaviour, the gate**: `seat_request` as the gate function, publish check and counters,
   REQ-UI-20 retired for REQ-UI-28, the banners and texts (REQ-UI-28, 29, 33).
5. **Behaviour, input**: Actuators seat rows without repeat; the menu row (REQ-UI-31, 32).
6. **Bench script**: B5s-1..17, graders, BENCH-055..071.
7. **Docs**.

Then the bench run on board 2 (B5s and the regression), then the merge to `dev_refactor`.

### 7.3 Risks

- **Stranding** (residual 7). A latched POST FAIL or stick FAULT blocks every seat move. Safe
  for motion, but the user may be left tilted. Needs the MCB answer S8.
- **Tolerance too tight.** If the real MCB reports a little outside its range (overshoot,
  before homing), that axis reads "--" and cannot be stepped. B5s-4 checks the band; S5 asks.
- **Banner churn.** On the Actuators page with the link down, every press now raises a banner
  where it only fed back before. Acceptable on a bench page.
- **Key-repeat change** makes long moves on the Actuators page slower (one press per step).
  Bench page only.

### 7.4 What must not change

- The drive table, its fingerprint, oracle and goldens; rows 10-11.
- `RAMMP_SEAT_AXIS_TABLE`, `SeatCommand`, `MibStatus` on the wire; `seat_raw`'s arithmetic.
- C1's output permit, C3's POST gate, C4's guard, C2's monitor: read only.
- No allocation, no lock, no LVGL in `components/seat`; no LVGL call off the UI task.
- A release build has no bench verb.

## 8. Questions for the MCB team (seat, add to D3)

1. **S1** When the HMI goes silent (no XYTwist, link loss, HMI reset), does the MCB stop a seat
   move in progress, or finish it? After how many ms?
2. **S2** Can the protocol get a seat stop: a STOP request, or hold-to-run (the seat moves only
   while SeatCommands keep arriving every ≤ N ms)?
3. **S3** Does the MCB refuse SeatCommand while ENABLED, or in any state but IDLE? Does ENABLE
   stop a seat move in progress? A SeatCommand arriving just after ENABLE?
4. **S4** How fast does each axis move? How long does a full-range move take?
5. **S5** What does `currentSeatState` hold when a position sensor fails, or before homing: NaN,
   0, a sentinel? Can it go outside the table's ranges (overshoot)?
6. **S6** Does the MCB clamp to the same ranges as `RAMMP_SEAT_AXIS_TABLE`?
7. **S7** Does the MCB block driving while the seat is raised or tilted? Is that reported
   (state, error text)?
8. **S8** Is there any other way to move the seat (MCB buttons, attendant control)? Does the MCB
   ever move the seat on its own, or for another participant?
9. **S9** After an MCB reset, does it resume a seat target or stay where it is?

Message to forward: "For the HMI seat safety review: (1) if the HMI goes silent mid-move, does
the MCB stop the seat or finish the move, and after how many ms? (2) could SeatCommand get a
stop, or hold-to-run? (3) does the MCB refuse seat moves unless IDLE, and does ENABLE stop a
seat move? (4) axis speeds? (5) what does currentSeatState report for a failed sensor or before
homing, and can it go outside the table's ranges? (6) same ranges as RAMMP_SEAT_AXIS_TABLE?
(7) is driving blocked with the seat raised or tilted? (8) any other seat control, or seat
motion the MCB starts itself? (9) after an MCB reset, does it resume a seat target?"

## 9. Open questions for the owner

Answer by ID; "rec" takes the recommendation.

1. **Q1 Seat authority (S1, §2.1).** The MCB decides seat motion; the HMI only asks, through the
   gate; it never stops, re-sends or asks on its own. **Rec: yes.**
2. **Q2 No seat table (§2.11).** A pure decision function with an exhaustive test, not a
   drive-style table. **Rec: yes**; a table only if Q6 picks (b) or hold-to-run.
3. **Q3 Stick condition.** (a) refuse only on stick FAULT; (b) FAULT and CHECK; (c) no stick
   condition. **Rec: a.** (b) gives rare refusals in the ~0.1 s CHECK windows for no gain (keys
   are already silent); (c) drops the v2 rule.
4. **Q4 Range tolerance.** (a) one step beyond each end; (b) none; (c) clamp out-of-range to the
   limit and call it known. **Rec: a**, revisit with S5. (c) hides a sensor fault.
5. **Q5 Presets need a known value.** (a) yes; (b) no, a preset is absolute. **Rec: a.** An
   unknown value means the MCB cannot say where the seat is; moving it then is a guess.
6. **Q6 Seat stop.** (a) none on the HMI now; ask the MCB team (S2); (b) a "Stop" that sends the
   last reported position as the target (reverses up to 0.5 s of travel); (c) nothing, no
   question. **Rec: a.**
7. **Q7 No re-send.** A failed or lost SeatCommand is never re-sent. **Rec: yes.** A re-send
   could move the seat after the user has moved on.
8. **Q8 No key repeat on the Actuators seat rows.** **Rec: yes.** It turns a stuck key from
   "walk to the limit" into one step.
9. **Q9 AT_LIMIT.** Refuse a step that would not move in the press' direction (feedback only).
   **Rec: yes.** Stops the direction flip in the tolerance band.
10. **Q10 Menu row.** The Seat Functions row uses the gate's first four conditions instead of
    `mcb_ready`. **Rec: yes**: the user is told why before opening a screen where every press
    would be refused.
11. **Q11 Texts.** "SEAT REFUSED: POSITION UNKNOWN" / "Wait for the MCB's seat position";
    "SEAT REFUSED: NOT SENT" / "Press again"; "Seat not ready" + C2's fault text for a stick
    FAULT. **Rec: approve or reword.**
12. **Q12 Stranding (residual 7).** Accept that a latched POST FAIL or stick FAULT blocks every
    seat move until a clean reset or a recalibration. Alternative: let presets toward a
    defined safe position through (needs S8 and an MCB-side definition). **Rec: accept now,
    revisit with S8.**
13. **Q13 Send §8 to the MCB team** with the D3 message (A1). **Rec: yes.** Seat sign-off waits
    for S1-S3 as C3's waits for D3.
14. **Q14 IDs.** Retire C3's REQ-UI-20 into REQ-UI-28; new series REQ-SEAT in a new component
    `components/seat`. **Rec: yes.**
15. **Q15 "Seat did not move" feedback** (a banner when the reported value does not change after
    a press). Out of scope here. **Rec: later**, with S4's speeds.
