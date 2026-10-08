# drive_session

The drive session: lock, ask the MIB to drive, unlock, drive, ask to stop. Namespace
`hmi::drive_session`. **Safety-relevant** (CS-SAF-01): it decides when the HMI asks for motion,
when it asks to stop, and when the stick gate may open.

Status: the AS-IS extraction (2026-10-06) with the hazard fix C1 (docs/plans/hazard-c1-spec.md,
approved by the owner 2026-10-08): the MCB is the authority on driving (M1), every relock sends
one DISABLE (H5), the user's stop is re-sent until the MCB stops and "MCB did not stop" is raised
after 5 s (G4), no entry during a calibration or behind the Boot screen (G5, U4). Hazards left:
[TABLE.md](TABLE.md) §7. Any change to it is a behaviour change and needs the owner's approval
and a new table (CS-SAF-05).

| Part | File |
| --- | --- |
| The spec: transition table, invariants, hold gestures, stick gate (`constexpr`, CS-SAF-02) | `include/drive_session_table.hpp`, read as [TABLE.md](TABLE.md) |
| The table's vocabulary: the Phase, Input, Guard and Action enums, Env, the row structs | `include/drive_session_types.hpp` |
| The code: `DriveSession`, the hand-written transition function | `include/drive_session.hpp`, `src/drive_session.cpp` |
| The table's fingerprint: one 64-bit number over every row, pinned by a `static_assert` | `include/drive_session_fingerprint.hpp` |
| The table's own invariants (DSO-001..019) | `test/oracle_selfcheck` |
| The sequences, the safe state, the stick gate and C1's cases (DRV-013..021, DRV-023..028, DRV-116) | `test/oracle` |
| The full-product oracle: the code against the table in every state (DRV-001..012, TS-UNIT-08), on demand (`make full`, owner decision G2) | `test/oracle_full` |
| The oracle by input: the same check over only the guards each input reads, the rest sampled (DRV-101..115, TS-UNIT-09) | `test/oracle_by_input` |

The header `drive_session_table.hpp` is a declaration: it is never edited to make a check pass.
Its data has a fingerprint (`TABLE_FINGERPRINT`, checked wherever the session is built): moving
declarations keeps the value, and a row change needs the owner's approval and the new value in
the same commit.

## Model

State = `Phase` (LOCKED, ASKING, UNLOCKING, DRIVING, EXITING, EXIT_REFUSED) plus six hidden
variables (WARN_ARMED, GIVEUP_ARMED, THEN_MENU, REQUEST_ENABLE, UNLOCK_TIMER_ARMED, STOP_FAULT).
The caller samples an `Env` (link, MIB state, screen, menu, which deadlines have passed, a
calibration running, the stop window and which DISABLE re-send is due) when an input arrives and calls `step(input, env, actions)`. The session returns the table's actions, in
order, and the caller performs each with the LVGL and RTPS calls it stands for. The session keeps
no timestamps and calls nothing: it is deterministic (CS-HAL-03) and allocates nothing (CS-SAF-04).

One 250 ms tick is the six inputs of `TICK_SEQUENCE` (TICK_FOLLOW, TICK_EXIT_DUE,
TICK_STOP_FAULT_DUE, TICK_STOP_RESEND, TICK_WARN_DUE, TICK_GIVEUP_DUE), applied in that order with
**two** Envs: TICK_FOLLOW gets the Env sampled when the tick starts; its actions are performed;
then one more Env is sampled (`now` read once) and the five other sub-steps share it. A deadline
or a re-send that falls due between the two samples is acted on in the same tick; a relock on the
first Env ends the stop before any re-send (DRV-116).

`stop_notice(phase, hidden)` says what the Drive screen shows about the user's stop:
MCB_DID_NOT_STOP, STOPPING or NONE (REQ-DRV-29).

D4, the state diagram: [TABLE.md §5](TABLE.md#5-d4-state-diagram-drawn-by-hand-from-2).

## Requirements

Row numbers are the rows of [TABLE.md §2](TABLE.md#2-transition-table-49-rows) (`TRANSITIONS`
index + 1). Every row is checked by the oracle case for its input: for every phase, all 64 hidden
masks and every Env (7,680), the session must give exactly that row's next phase, hidden
variables and action list, or change nothing where no row matches (DRV-001..012, on demand). The
oracle by input (DRV-101..115) checks the same in CI, over the states described in
[Two oracles](#two-oracles).

| ID | Requirement | Rows | Tests |
| --- | --- | --- | --- |
| REQ-DRV-01 | A new session is LOCKED, with DISABLE as the request and nothing armed | – | DRV-021 |
| REQ-DRV-02 | RETIRED 2026-10-08, superseded by REQ-DRV-22. On a tick, a locked session whose MIB is driving (link up, ENABLED) unlocks: clears every wait and exit latch, opens the padlock, starts the 1 s advance timer, unlocks and updates the gate; whether or not it asked (H1) | 1–2 | – |
| REQ-DRV-03 | RETIRED 2026-10-08, superseded by REQ-DRV-23. On a tick, an unlocked session whose MIB is not driving relocks: Locked screen, gate updated, no DISABLE sent (H5). Before the Locked screen loads, the menu-on-arrival flag is set to whether the burger key asked for the exit (open the menu: row 7) and cleared on every other relock. An exit the user asked for raises no banner; otherwise the banner says DRIVE_STOPPED with the link up and DRIVE_LOST with it down | 3–9 | – |
| REQ-DRV-04 | On a tick, locked on the Seat screen without the MCB: home, with the seat refusal. Otherwise, asking with the answer window gone: the ring rests (LOCKED) | 10–13 | DRV-001, DRV-101, DRV-014 |
| REQ-DRV-05 | RETIRED 2026-10-08, superseded by REQ-DRV-24. On a tick, an exit not granted by its deadline is refused: EXIT_REFUSED, banner, menu flag cleared, DISABLE not re-sent (H6) | 14 | – |
| REQ-DRV-06 | On a tick, an ask not granted within its answer window raises NOT_GRANTED once; the ring keeps going until the next tick | 15 | DRV-003, DRV-103, DRV-014 |
| REQ-DRV-07 | On a tick, an ask past its give-up deadline sends DISABLE once, in LOCKED or ASKING | 16–17 | DRV-004, DRV-104, DRV-014 |
| REQ-DRV-08 | A completed unlock hold asks for ENABLE and arms the warn and give-up deadlines when the MCB is ready, refuses on the spot when it is not, and does nothing when already asking | 18–20 | DRV-005, DRV-105, DRV-013 |
| REQ-DRV-09 | RETIRED 2026-10-08, superseded by REQ-DRV-25. A completed exit hold, in any unlocked phase, sends DISABLE, latches the exit, clears the menu flag and arms a fresh exit deadline | 21–24 | – |
| REQ-DRV-10 | RETIRED 2026-10-08, superseded by REQ-DRV-27. The menu key on the Drive screen, unlocked, asks to stop and then open the menu; while an exit deadline is armed it does nothing | 25–28 | – |
| REQ-DRV-11 | RETIRED 2026-10-08, superseded by REQ-DRV-30. A profile click re-publishes the drive request as it is, in every phase (H5) | 29–34 | – |
| REQ-DRV-12 | The unlock advance timer, while armed, loads the Drive screen; from UNLOCKING it moves to DRIVING | 35–37 | DRV-009, DRV-109, DRV-013 |
| REQ-DRV-13 | A stick push on the Locked screen with no menu and no MCB raises the drive refusal, LOCKED or ASKING | 38–39 | DRV-010, DRV-110 |
| REQ-DRV-14 | The DRIVE menu row with no MCB raises the menu refusal, LOCKED or ASKING | 40–41 | DRV-011, DRV-111 |
| REQ-DRV-15 | Any (phase, hidden, input, env) that no row lists changes nothing and returns no action | all | DRV-001..011, DRV-101..111, DRV-114, DRV-115 |
| REQ-DRV-16 | Every row of the table is reachable from a state inside the table's contract, and the oracle drives every input | all | DRV-012, DRV-112, GLD-115 |
| REQ-DRV-17 | Every state reachable from the start keeps the phase invariants (`PHASE_INVARIANTS`) | all | DRV-015 |
| REQ-DRV-18 | RETIRED 2026-10-08, superseded by REQ-DRV-31. A corrupted phase or input sends the session to the safe state (LOCKED; DISABLE sent; waits, exit latch, menu flag, menu-on-arrival flag and advance timer cleared; ring at rest; Locked screen; gate updated) and `step` returns false so the caller reports it | – | – |
| REQ-DRV-19 | The stick drives only when unlocked, on the Drive screen, with no menu open (`stick_drives`) | §4 | DRV-019 |
| REQ-DRV-20 | The stick scale is 0 while calibrating or gated, else the speed; a NaN speed passes an open gate (pinned as-is) | §4 | DRV-020 |
| REQ-DRV-21 | RETIRED 2026-10-08, superseded by REQ-DRV-32. A tick decides TICK_FOLLOW on the Env sampled at its start and the three deadline checks on one Env sampled after follow-state's actions: a deadline that passes in between is acted on in the same tick, and an ENABLED that arrives in between is seen by follow-state only on the next tick | `TICK_SEQUENCE` | – |
| REQ-DRV-22 | On a tick, LOCKED or ASKING with the MCB ENABLED on a CONNECTED link unlocks (F1), asked or not (M1), unless a calibration is running or the Boot screen is up | 1–2 | DRV-001, DRV-101, DRV-023, DSO-017, GLD-101, GLD-108, GLD-109 |
| REQ-DRV-23 | On a tick, an unlocked session whose MCB is not ENABLED on a CONNECTED link relocks: the menu-on-arrival flag is set when the burger key asked for the exit and cleared otherwise, Locked screen, gate updated, one DISABLE sent (the request is DISABLE after), and the banner DRIVE_STOPPED (link up) or DRIVE_LOST (link down) unless the user asked for the exit. From the exit phases it also clears the stop fault | 3–9 | DRV-001, DRV-101, DRV-013, DRV-026, GLD-102, GLD-103, GLD-107 |
| REQ-DRV-24 | On a tick, an exit not seen granted by its 750 ms deadline moves to EXIT_REFUSED with the exit-refused banner | 14 | DRV-002, DRV-102, GLD-105 |
| REQ-DRV-25 | A completed exit hold sends DISABLE in every phase. From UNLOCKING or DRIVING: latch the exit, clear the menu flag, arm the exit deadline and the stop timer. From EXITING or EXIT_REFUSED: the same but the stop timer is kept. From LOCKED: clear the give-up. From ASKING: withdraw the ask (warn, give-up cleared), ring at rest, LOCKED | 21–24, 42, 43 | DRV-006, DRV-106, DRV-028, GLD-104, GLD-111, GLD-112 |
| REQ-DRV-26 | In EXITING and EXIT_REFUSED, each tick re-sends DISABLE when at least `kStopResend − kStopResendSlack` has passed since the last DISABLE; after the stop fault, when at least `kStopResendSlow − kStopResendSlack` has. It stops only when the session leaves those phases | 46–49 | DRV-115, DRV-024, DSO-019, GLD-105, GLD-106 |
| REQ-DRV-27 | The burger key on Drive, unlocked, asks to stop and then open the menu, and from UNLOCKING or DRIVING arms the stop timer; while an exit deadline is armed it does nothing | 25–28 | DRV-007, DRV-107, DRV-013, GLD-104, GLD-106 |
| REQ-DRV-28 | The stop fault is raised once, on the first tick at least `kStopFaultAfter` after the first stop of the exit, and cleared by the relock | 7–9, 44–45 | DRV-114, DRV-024, DSO-014, GLD-105, GLD-107 |
| REQ-DRV-29 | `stop_notice` is MCB_DID_NOT_STOP in an exit phase with the stop fault, STOPPING in an exit phase without it, NONE otherwise | §2.6 of the C1 spec | DRV-025, GLD-104, GLD-107 |
| REQ-DRV-30 | A profile tap re-publishes the request as it stands in LOCKED, ASKING, EXITING and EXIT_REFUSED. In UNLOCKING and DRIVING it sends ENABLE with the profile only when the MCB is ENABLED on a CONNECTED link, else nothing | 29–34 | DRV-008, DRV-108, GLD-110 |
| REQ-DRV-31 | A corrupted phase or input gives the safe state: LOCKED, DISABLE sent, waits, exit latch, menu flag, menu-on-arrival, advance timer and stop fault cleared, ring at rest, Locked screen, gate updated; `step` returns false | – | DRV-016, DRV-017, DRV-018, GLD-114 |
| REQ-DRV-32 | A tick decides TICK_FOLLOW on the Env at its start and the five other sub-steps, in `TICK_SEQUENCE` order, on one Env sampled after TICK_FOLLOW's actions | `TICK_SEQUENCE` | DRV-116, DSO-018 |
| REQ-DRV-33 | No row sends ENABLE except the unlock hold (row 18) and a profile tap with the MCB ENABLED (rows 31-32) | 18, 31, 32 | DRV-027, DSO-016, GLD-110 |
| REQ-DRV-34 | Every transition from an unlocked phase to LOCKED sends DISABLE | 3–9 | DRV-026, DSO-015 |

The table excludes some (phase, input) pairs from its contract (TABLE.md §2.2). The session
follows the row guards literally, so for those it changes nothing. An exit hold completing while
locked (TABLE.md U3) is no longer one of them: C1 made it rows 42-43.

## Two oracles

`test/oracle_full` (DRV-001..012) drives the full product: 6 phases x 11 inputs x 32 hidden
masks x 640 Envs = 1,351,680 steps on the AS-IS table (1.1 s); with C1, 6 x 13 x 64 x 7,680 =
38,338,560 steps (114 s, measured 2026-10-08). Every new hidden bit doubles it (the hazard fixes add
DISABLE_PENDING, POST_PENDING, STICK_OK, ...), past the 30 s budget of an L1 app (TS-UNIT-09).
`test/oracle_by_input` (DRV-101..113, docs/plans/hazard-fixes.md B3) gives the same verdict over
fewer states. The state is cut into dimensions: (link, MIB state), screen, menu, each `*_ELAPSED`,
and one per hidden bit (`oracle_space.hpp`; a new hidden bit gets its own, a new env guard does
not compile until it has one). Per input:

| Dimensions | Visited |
| --- | --- |
| Holding a bit that a row of the input (or its precondition) reads | every value, in every phase: the table's row depends only on these |
| The others, if they have at most 256 points (or no more than the sample) | every point |
| The others, otherwise | every point within distance 3 of the all-clear and the all-set corner, plus 16 seeded random points per class |

The table cannot tell the sampled states apart, so they only check that the code does not read
a bit the table does not. A dependence on up to three such bits set (any number clear), or up to
three clear (any number set), is always found; a wider one only through the random points.

Evidence, on today's table (both run on demand in `test/oracle_by_input`):

- `make equivalence`: every (phase, input, read bits) class of the full product is visited, with
  the same row, the same rows reached, and the session agreeing at every step of both.
- `make mutants`: one-line mutations of the table (the fingerprint check off in the copy) and of
  the code; both oracles must reject each.

The full product is out of CI by the owner's decision G2 (2026-10-08, hazard-c1-spec.md E5):
CI runs the by-input oracle; the full product runs on demand with `make full` in
`test/oracle_full`. Re-run `make full`, `make equivalence` and `make mutants` with every table
change.

## Coverage

`make coverage` in `test/oracle_full`: 100% lines. Branches: 100% without sanitizers
(`make coverage SAN_FLAGS= BUILD_DIR=$HOME/hmi-build/drive_session_nosan`); 99.32% with
ASan/UBSan, where the one branch never taken is UBSan's own check of the `bool` load in `step()`.

## Tasks

None. Every call runs on its caller's task: the UI (LVGL) task. Not thread-safe; one owner.

## Dependencies

None: the C++ standard library only. No LVGL, ESP-IDF or FreeRTOS.

## Using it

```cpp
#include "drive_session.hpp"
namespace ds = hmi::drive_session;

ds::DriveSession session;
ds::Actions actions{};
// The 250 ms tick: follow-state on the Env at the start ...
if (!session.step(ds::TICK_SEQUENCE[0], sample_env(), actions)) { /* report: corrupted */ }
for (ds::Action a : actions) { perform(a); }       // LVGL and RTPS calls, in order
// ... then the other sub-steps, in order, on one Env sampled after those actions.
const ds::Env env = sample_env();
for (std::size_t i = 1; i < ds::TICK_SEQUENCE.size(); ++i) {
  if (!session.step(ds::TICK_SEQUENCE[i], env, actions)) { /* report: corrupted state */ }
  for (ds::Action a : actions) { perform(a); }
}
```
