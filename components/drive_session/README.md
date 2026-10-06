# drive_session

The drive session: lock, ask the MIB to drive, unlock, drive, ask to stop. Namespace
`hmi::drive_session`. **Safety-relevant** (CS-SAF-01): it decides when the HMI asks for motion,
when it asks to stop, and when the stick gate may open.

Status: **AS-IS extraction, draft, unreviewed** (2026-10-06). The behaviour is today's firmware
behaviour, hazards included (H1, H5, H6, H7 in [TABLE.md](TABLE.md) §7). Any change to it is a
behaviour change and needs two human approvals and a new table (CS-SAF-05).

| Part | File |
| --- | --- |
| The spec: transition table, invariants, hold gestures, stick gate (`constexpr`, CS-SAF-02) | `include/drive_session_table.hpp`, read as [TABLE.md](TABLE.md) |
| The code: `DriveSession`, the hand-written transition function | `include/drive_session.hpp`, `src/drive_session.cpp` |
| The table's fingerprint: one 64-bit number over every row, pinned by a `static_assert` | `include/drive_session_fingerprint.hpp` |
| The table's own invariants (DSO-001..012) | `test/oracle_selfcheck` |
| The oracle: the code against the table (DRV-001..021, TS-UNIT-08) | `test/oracle` |

The header `drive_session_table.hpp` is a declaration: it is never edited to make a check pass.
Its data has a fingerprint (`TABLE_FINGERPRINT`, checked wherever the session is built): moving
declarations keeps the value, and a row change needs the owner's approval and the new value in
the same commit.

## Model

State = `Phase` (LOCKED, ASKING, UNLOCKING, DRIVING, EXITING, EXIT_REFUSED) plus five hidden
variables (WARN_ARMED, GIVEUP_ARMED, THEN_MENU, REQUEST_ENABLE, UNLOCK_TIMER_ARMED). The
caller samples an `Env` (link, MIB state, screen, menu, and which deadlines have passed) when an
input arrives and calls `step(input, env, actions)`. The session returns the table's actions, in
order, and the caller performs each with the LVGL and RTPS calls it stands for. The session keeps
no timestamps and calls nothing: it is deterministic (CS-HAL-03) and allocates nothing (CS-SAF-04).

One 250 ms tick is the four inputs of `TICK_SEQUENCE` (TICK_FOLLOW, TICK_EXIT_DUE, TICK_WARN_DUE,
TICK_GIVEUP_DUE), applied in that order with one Env.

D4, the state diagram: [TABLE.md §5](TABLE.md#5-d4-state-diagram-drawn-by-hand-from-2).

## Requirements

Row numbers are the rows of [TABLE.md §2](TABLE.md#2-transition-table-41-rows) (`TRANSITIONS`
index + 1). Every row is checked by the oracle case for its input: for every phase, all 32 hidden
masks and every Env (640), the session must give exactly that row's next phase, hidden variables
and action list, or change nothing where no row matches.

| ID | Requirement | Rows | Tests |
| --- | --- | --- | --- |
| REQ-DRV-01 | A new session is LOCKED, with DISABLE as the request and nothing armed | – | DRV-021 |
| REQ-DRV-02 | On a tick, a locked session whose MIB is driving (link up, ENABLED) unlocks: clears every wait and exit latch, opens the padlock, starts the 1 s advance timer, unlocks and updates the gate; whether or not it asked (H1) | 1–2 | DRV-001 |
| REQ-DRV-03 | On a tick, an unlocked session whose MIB is not driving relocks: Locked screen, gate updated, no DISABLE sent (H5). An exit the user asked for raises no banner, and opens the menu if the burger key asked for it; otherwise the banner says DRIVE_STOPPED with the link up and DRIVE_LOST with it down | 3–9 | DRV-001 |
| REQ-DRV-04 | On a tick, locked on the Seat screen without the MCB: home, with the seat refusal. Otherwise, asking with the answer window gone: the ring rests (LOCKED) | 10–13 | DRV-001, DRV-014 |
| REQ-DRV-05 | On a tick, an exit not granted by its deadline is refused: EXIT_REFUSED, banner, menu flag cleared, DISABLE not re-sent (H6) | 14 | DRV-002 |
| REQ-DRV-06 | On a tick, an ask not granted within its answer window raises NOT_GRANTED once; the ring keeps going until the next tick | 15 | DRV-003, DRV-014 |
| REQ-DRV-07 | On a tick, an ask past its give-up deadline sends DISABLE once, in LOCKED or ASKING | 16–17 | DRV-004, DRV-014 |
| REQ-DRV-08 | A completed unlock hold asks for ENABLE and arms the warn and give-up deadlines when the MCB is ready, refuses on the spot when it is not, and does nothing when already asking | 18–20 | DRV-005, DRV-013 |
| REQ-DRV-09 | A completed exit hold, in any unlocked phase, sends DISABLE, latches the exit, clears the menu flag and arms a fresh exit deadline | 21–24 | DRV-006 |
| REQ-DRV-10 | The menu key on the Drive screen, unlocked, asks to stop and then open the menu; while an exit deadline is armed it does nothing | 25–28 | DRV-007, DRV-013 |
| REQ-DRV-11 | A profile click re-publishes the drive request as it is, in every phase (H5) | 29–34 | DRV-008 |
| REQ-DRV-12 | The unlock advance timer, while armed, loads the Drive screen; from UNLOCKING it moves to DRIVING | 35–37 | DRV-009, DRV-013 |
| REQ-DRV-13 | A stick push on the Locked screen with no menu and no MCB raises the drive refusal, LOCKED or ASKING | 38–39 | DRV-010 |
| REQ-DRV-14 | The DRIVE menu row with no MCB raises the menu refusal, LOCKED or ASKING | 40–41 | DRV-011 |
| REQ-DRV-15 | Any (phase, hidden, input, env) that no row lists changes nothing and returns no action | all | DRV-001..011 |
| REQ-DRV-16 | Every row of the table is reachable from a state inside the table's contract, and the oracle drives every input | all | DRV-012 |
| REQ-DRV-17 | Every state reachable from the start keeps the phase invariants (`PHASE_INVARIANTS`) | all | DRV-015 |
| REQ-DRV-18 | A corrupted phase or input sends the session to the safe state (LOCKED; DISABLE sent; waits, exit latch, menu flag and advance timer cleared; ring at rest; Locked screen; gate updated) and `step` returns false so the caller reports it | – | DRV-016, DRV-017, DRV-018 |
| REQ-DRV-19 | The stick drives only when unlocked, on the Drive screen, with no menu open (`stick_drives`) | §4 | DRV-019 |
| REQ-DRV-20 | The stick scale is 0 while calibrating or gated, else the speed; a NaN speed passes an open gate (pinned as-is) | §4 | DRV-020 |

The table excludes some (phase, input) pairs from its contract (TABLE.md §2.2). The session
follows the row guards literally, so for those it changes nothing. One of them differs from the
firmware today: an exit hold completing while locked (TABLE.md U3), which the firmware turns into
a DISABLE and a latched exit. That is parked for the owner.

## Coverage

`make coverage` in `test/oracle`: 100% lines. Branches: 100% without sanitizers
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
const ds::Env env = sample_env();                  // the 250 ms tick: one Env for all four
for (ds::Input in : ds::TICK_SEQUENCE) {
  if (!session.step(in, env, actions)) { /* report: corrupted state */ }
  for (ds::Action a : actions) { perform(a); }     // LVGL and RTPS calls, in order
}
```
