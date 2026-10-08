# Spec: hazard fix C4 (H4 interim + H11)

Status: **spec for owner approval** (2026-10-08). No code yet. Base: `dev_refactor` 4a20866.
Hazards: [refactor.md](refactor.md) §1 H4, H11. Plan: [hazard-fixes.md](hazard-fixes.md) §4 C4,
§9 (MCB authority, G1-G5, D4 values). Safety change (CS-SAF-05): the owner approves this spec,
then a different agent writes the code, then the bench, then the merge to `dev_refactor`.

## 0. Exec summary

**What the user sees.** Normally nothing. The stick output drops to 0 (the chair stops, the MCB
stays ENABLED, the screen stays where it is) when:

- the display task has not finished a cycle for 200 ms;
- no MibStatus for 2 s, or the network link is down;
- the MCB reports anything but ENABLED. The stick goes to 0 on the next ADC cycle (≤ 40 ms),
  before the screen leaves Drive (UI tick, ≤ 250 ms).

Output comes back by itself when the cause clears (O1 proposes 300 ms of neutral first). A
display frozen for 2 s, or a frozen stick task, reboots the HMI. After the reboot C3's rules
apply (reset reason shown, no output before POST and link).

**What changes.**

- The ADC task (`Read ADC`) checks those conditions itself, every cycle. It no longer trusts
  the UI's gate (`stick_drives`) alone. New lock-free atomics; no lock, allocation or logging
  added to its path.
- `Read ADC`: priority 5 → **21** (above `lv_task`, 20), core "first FPU use" → **0**, stack
  4096 → **6144 B**. One row of the G10 baseline changes.
- Task watchdog (TWDT): the ADC and UI tasks subscribe. Timeout 5 s → **2 s**. A trip
  **panics and resets** (today it only prints). Idle-task checks off.
- Not touched: the stick pipeline and its goldens, the drive table, C1's DISABLE logic.

**Risks.**

- A stuck ADC task: the MCB keeps the last XYTwist until its own timeout (D3 Q1, unknown). The
  watchdog reset does not shorten that. This is the largest open item (§4.5).
- False stops: a UI cycle ≥ 200 ms while driving pauses the chair (a flash write on the UI
  task, a self test, a bench screenshot). The bench soak measures this (§8).
- False resets: a UI cycle ≥ 2 s anywhere reboots the HMI. The watchdog runs report-only first
  (phase 1) and panics only after a clean soak (phase 2).
- G4 holds: C4 never reads the user's stop or a DISABLE (§2.5).

## 1. Scope

| In C4 | Not in C4 (owner) |
| --- | --- |
| ADC-side guard: UI heartbeat, MibStatus age, link, MCB state | the drive table, DISABLE re-sends, "MCB did not stop" (C1, lane D) |
| TWDT on the ADC and UI tasks; trip = panic reset | POST gate, boot DISABLE, reset reason shown (C3) |
| ADC task priority, core, stack; the G10 row | stick plausibility, literal-0 gate, NaN (C2) |
| guard observability (self test) | G1 neutral-first on entering Drive (wherever C1/C3 put it) |
| | the islands work (the full H4 fix: `control` owns the gate) |

## 2. Conditions and constants

### 2.1 The gate after C4

The command stays `mounted × scale` (REQ-STK-04, unchanged). The scale is 0 when any of these
holds, else drive speed / 10:

- a calibration run is in progress (today);
- `stick_drives` is false (the UI's gate; C1 defines when it is true);
- **the C4 verdict is not OK** (new, this spec);
- later terms from C3 (POST) and C2 (stick fault) join the same composition.

C4 joins through `AdcStickIo::stick_drives()`, which returns `gate_open(stick_drives, verdict)`.
The pipeline's `Io` contract is unchanged, so the STK goldens and call-order tests stay as they
are.

### 2.2 The C4 conditions

Each row forces the output to 0. The reason is a code the ADC task publishes.

| # | Forces 0 when | Source (writer) | Reason |
| --- | --- | --- | --- |
| C4-e | the ADC or the UI task failed to subscribe to the TWDT | the two tasks, at their first cycle | `WDT_MISSING` |
| C4-a | UI heartbeat age ≥ 200 ms, or never written | `lv_task`, after each cycle | `UI_STALE` |
| C4-b | net failed, no link, or no IP | `rtps_comms` (existing atomics) | `LINK_DOWN` |
| C4-c | newest MibStatus age ≥ 2000 ms, or none since boot | `rtps_worker`, in `on_mib_status` | `MIB_STALE` |
| C4-d | newest MibStatus `systemState` ≠ ENABLED (INITIALIZING, IDLE, ERROR, any other byte) | `rtps_worker`, in `on_mib_status` | `MCB_NOT_ENABLED` |
| C4-f | only if O1 = yes: after any of the above, until the stick has read neutral for 300 ms | the ADC task | `REARM_PENDING` |

- C4-b plus C4-c is exactly "link not CONNECTED" as `rtps_comms_link_state()` defines it, built
  from lock-free values instead of its 64-bit atomic (§3.3).
- Several can hold at once. The reported reason is the first in the order e, a, b, c, d, f. The
  order only picks the reported code; every one of them forces 0.

### 2.3 Constants

Each is a named constant (D4). New ones live in `components/control/include/control/motion_guard.hpp`.

| Name | Value | Status |
| --- | --- | --- |
| `UI_HEARTBEAT_MAX_AGE_MS` | 200 | approved (D4) |
| `MIB_STATUS_MAX_AGE_MS` | 2000; `static_assert` equal to `rammp::kMibStatusTimeout` | approved (D4) |
| `CONFIG_ESP_TASK_WDT_TIMEOUT_S` | 2 (today 5) | **proposed** (O5) |
| `CONFIG_ESP_TASK_WDT_PANIC` | y (today n), in phase 2 | proposed |
| `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0/1` | n (today y) | proposed (O3) |
| `ADC_TASK_PRIORITY` | 21 (today 0 → 5) | proposed (O2) |
| `ADC_TASK_CORE` | 0 (today -1, first FPU use) | proposed |
| `ADC_TASK_STACK_BYTES` | 6144 (today 4096) | proposed |
| `NEUTRAL_REARM_MS` | 300, G1's value (one constant if G1's exists) | only if O1 = yes |

"Fresh" means age < limit; "stale" means age ≥ limit. Ages are whole ms. Truncation can only
make a source look older, never younger past a limit.

### 2.4 Timing bounds

ADC cycle: 33 ms wait after the work, measured 35.0 ms average, 39.0 ms worst (85 runs, board 2).
Bounds below use 40 ms for one cycle.

| Event at time t | Output is 0 by | Then |
| --- | --- | --- |
| UI stops finishing cycles | t + 200 + 40 ms | reset at t + 2 s + 40 ms (TWDT) |
| last MibStatus received | t + 2000 + 40 ms | the UI also leaves Drive (C1, same 2 s) |
| a non-ENABLED MibStatus received | t + 40 ms | the UI leaves Drive on its tick (≤ 250 ms) |
| carrier lost (link down, IP lost) | t + 40 ms | MibStatus stops too |
| ADC task stops | no XYTwist from t | reset at t + 2 s + 40 ms; the MCB timeout decides (§4.5) |

### 2.5 What C4 does not check (G4, M1, C1)

C4 reads none of these: `drive_request`, `DISABLE_PENDING`, the exit hold, the burger key, the
drive phase, the "MCB did not stop" fault.

- **G4 holds.** After the user's stop, while the MCB reports ENABLED (fresh, link up) and the UI
  runs, C4 lets the stick drive. It adds no "user stop → 0" rule.
- **M1 holds.** MCB leaves ENABLED → C4 forces 0 within one cycle; the UI follows on its tick.
- **G2 is implemented here** on the ADC side. The UI side of G2 (leave Drive when stale) is C1's.
- **C1 interaction.** C1 owns `stick_drives` and the DISABLE re-sends. C4 only ANDs into the
  gate. Neither reads the other's state. When both land, the gate is open only if C1 says Drive
  and C4 says OK.

## 3. Data flow

### 3.1 Shared values

All are `std::atomic` with `static_assert(std::atomic<T>::is_always_lock_free)` (CS-OWN-03).
One writer each.

| Value | Type | Writer (task) | When | Store | Reader | Load |
| --- | --- | --- | --- | --- | --- | --- |
| `ui_heartbeat_ms` | `uint32_t` | `lv_task` (UiIsland loop) | after each `cycle()` returns | release | Read ADC | acquire |
| `ui_wdt_ok` | `bool` | `lv_task` | once, after subscribing | release | Read ADC | acquire |
| `mcb_state` | `uint8_t` | `rtps_worker` (`on_mib_status`) | each MibStatus, **before** the stamp | relaxed | Read ADC | relaxed, **after** the stamp |
| `mcb_rx_ms` | `uint32_t` | `rtps_worker` (`on_mib_status`) | each MibStatus, before the handler takes the LVGL lock | release | Read ADC | acquire |
| `net_failed`, `link_up`, `got_ip` | `bool` | existing (`sys_evt`, `rtps_start`) | events | existing | Read ADC | acquire |
| `stick_drives` | `bool` | existing (`lv_task`) | gate triggers | existing | Read ADC | existing |
| `adc_wdt_ok` | `bool` | Read ADC | once | own | Read ADC | own |
| `guard_reason` | `uint8_t` | Read ADC | every cycle | relaxed | UI, self test | relaxed |
| `guard_trips[reason]` | `uint32_t` | Read ADC | each stale onset, only goes up | relaxed | self test | relaxed |
| `ui_age_max_ms` | `uint32_t` | Read ADC | every cycle | relaxed | self test | relaxed |
| `ui_stalls_drive` | `uint32_t` | Read ADC | each `UI_STALE` onset while `stick_drives` is true, only goes up | relaxed | UI, self test | relaxed |

Stamps are `esp_timer_get_time() / 1000` truncated to `uint32_t`, from one clock function main
supplies to both writers and the reader (an injected clock, CS-CON-05). The guard takes `now` as
a parameter, so tests use fake time.

### 3.2 Rules on the ADC path

1. **Once per cycle.** The guard is evaluated once, after the three reads and before the
   pipeline, on every cycle: valid, invalid (H9) and calibrating. That cycle's gate uses that
   verdict.
2. **Read order.** Load the stamps first (acquire), then `mcb_state`, then read the clock. A
   stamp is then never ahead of `now`.
3. **Tearing is harmless.** The writer stores the state before the stamp. A reader that sees
   stamp N sees a state from message N or newer. A newer state is real data; a stale stamp only
   makes the guard more conservative.
4. **Wrap and alias.** Ages are `now − stamp` modulo 2^32. A source that the guard has seen
   stale stays stale until its stamp changes. So a stamp 2^32 ms (49.7 days) old cannot look
   fresh again. A stamp ahead of `now` counts as stale.
5. **No allocation, no logging, no lock, no indirect call** in the guard or the gate. The guard
   is plain inline code on plain values: no `std::function`, no virtual call, no recursion, no
   VLA. Its frames stay visible to `-fstack-usage` (§8 SU).
6. **Start-up only.** Subscribing to the TWDT and a failure log happen at the task's first
   cycle. That counts as start-up (CS-SAF-04); the steady-state cycle logs nothing new.

### 3.3 Why 32-bit stamps

- `std::atomic<int64_t>` is not lock-free on the P4 (RV32): IDF implements it with a critical
  section. It would fail CS-OWN-03's `static_assert` and add a lock. That is why the guard does
  not call `rtps_comms_link_state()` (it reads `last_status_us`, 64-bit).
- A sequence counter instead of a stamp would avoid the wrap, but the ADC can only time it from
  when it sees the change: up to one cycle late, so "200 ms" would become up to 240 + 40 ms.
  Stamps keep the approved 200 ms exact; the stale latch (rule 4) removes the alias.

### 3.4 Locks already on the ADC path (not changed by C4)

| Where | Kind | Note |
| --- | --- | --- |
| `joystick_cal_take_new` | `std::mutex`, blocking, short copy | for the islands work |
| `AdcStickIo::show` (the bars) | `try_lock` on `lvgl_mutex`, never waits | unchanged |
| `rtps_comms_publish_adc` | espp RTPS and lwIP internals | unchanged |

## 4. Watchdog design

### 4.1 Who subscribes

| Task | Subscribes | Why |
| --- | --- | --- |
| `Read ADC` | **yes** | safety task (CS-SAF-06) |
| `lv_task` | **yes** | it decides `stick_drives` and sends DISABLE; a stall is caught by C4-a in 200 ms and reset at 2 s |
| `rtps_pub` | no (O4) | availability only (the bench counter, rebinds). A reset mid-drive is worse than that fault |
| `rtps_worker_*`, `rtps_reactor`, `rtps_protocol` | no | espp's, event-driven: no MibStatus would trip it. Their stall shows as C4-c (fail-safe) |
| `ContinuousAdc T` | no | espp's; see O9 |
| `IDLE0/1` | no (O3) | today's 8 s boot trip (IDLE0 starved by `main`) would become a boot loop with panic on |

IDF's TWDT has one timeout and one panic setting for all. That is why the non-safety tasks stay
out.

### 4.2 Settings (`sdkconfig.defaults`, every variant inherits)

| Setting | Today | Phase 1 | Phase 2 |
| --- | --- | --- | --- |
| `CONFIG_ESP_TASK_WDT_EN`, `_INIT` | y | y | y |
| `CONFIG_ESP_TASK_WDT_TIMEOUT_S` | 5 | 2 | 2 |
| `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0`, `_CPU1` | y | n | n |
| `CONFIG_ESP_TASK_WDT_PANIC` | n | n (report only) | **y** |

Phase 2 is its own commit, made after the soak (§8) passes.

### 4.3 Subscribe and reset

- Each task subscribes itself at its first cycle (`esp_task_wdt_add(NULL)`), checks the result
  (CS-ERR-02) and stores `adc_wdt_ok` / `ui_wdt_ok`.
- A failed subscription logs one error at start-up and leaves the flag false. C4-e then holds
  the output at 0 until reboot.
- Reset (`esp_task_wdt_reset`) at the end of every cycle: ADC after `note_cycle`, before the
  wait, valid or not; UI after `cycle()` and the FPS report, before the wait.
- Both calls go through a small port that main fills, so `control` and `hmi_ui` keep no IDF
  include on these paths and the host tests use a fake.

### 4.4 What a trip does (phase 2)

1. A subscribed task has not reset for 2 s. The TWDT interrupt fires within one cycle of that.
2. Panic: both cores stop. No task runs, so no XYTwist leaves the HMI from here.
3. The panic handler prints the backtrace and resets (`PANIC_PRINT_REBOOT`, delay 0; no core
   dump configured).
4. Boot. Reset reason `ESP_RST_TASK_WDT` (to confirm on the bench, B5i). C3 shows it and
   holds output until POST and link.

Output just before the reset:

| Trip on | Last XYTwist the MCB got | Stops the chair |
| --- | --- | --- |
| `lv_task` | neutral, for the last ≥ 1.8 s (C4-a since t + 200 ms) | C4, before the trip |
| `Read ADC` | whatever the ADC sent last, possibly deflected | **only the MCB's XYTwist timeout** |

### 4.5 Interaction with the MCB's XYTwist timeout (D3 Q1, assumed to exist)

| Failure | Who stops the chair | How fast |
| --- | --- | --- |
| UI stall | C4-a (the HMI sends neutral) | ≤ 240 ms; XYTwist keeps flowing |
| RTPS receive stall | C4-c | ≤ 2.04 s |
| link loss | the MCB timeout (nothing reaches it) | T_mcb |
| ADC task stall | the MCB timeout | T_mcb; the HMI resets at 2 s and sends neutral again only after boot and link (seconds) |
| any panic or reset | the MCB timeout | T_mcb |

So for an ADC stall, a panic or a link loss, the MCB timeout is the only stop. The TWDT adds
recovery, not a faster stop. The bench sim models T_mcb as 1 s (`ongone`). O8 asks the MCB team
for the real value.

## 5. Task table changes

G10 baseline `tools/guards/baselines/tasks.json` (a declared table; this spec is its approval).

| Task | Field | Old | New |
| --- | --- | --- | --- |
| `Read ADC` | prio | 5 (configured 0, pthread default) | 21 |
| `Read ADC` | core | `"fpu"` (boot-dependent, 0 seen) | 0 (pinned; `coproc_pinned` true) |
| `Read ADC` | stack_bytes | 4096 | 6144 |
| `Read ADC` | source | `main/main.cpp:1619 …BaseConfig defaults…` | the new `StickIsland` config literal in `main/main.cpp` |

No other row changes. `lv_task` stays 20 / core 1 / 16384. `ContinuousAdc Task` stays 5 / `"fpu"`.
The name stays `Read ADC` (the census, logs and bench match it; the topology's `control` name
is for the islands work).

**Priority 21, core 0.** Core 0 today: `ipc0` 24, unpinned `esp_hosted` tasks 23, `esp_timer`
22, `sys_evt` 20, `tcpip` 18, everything of ours at ≤ 5 (plus `main` 1, `selftest` 3).
21 is above every application task and `sys_evt`/`tcpip`, and below IDF's timing services, so
none of them ties with it. Core 0 keeps it off `lv_task` and `tab5_audio` (both 20, core 1).
The topology draft says 22 (O2).

**Stack 6144 B.** CS-MEM-04: at least 1.5 × the stress high-water mark. Worst clean run:
1188 B free of 4096 (stress-tasks, b677b01), so 2908 B used; × 1.5 = 4362 B. 6144 B leaves
room for C4's frame growth, the TWDT reset and C2. It matches the topology draft. Cost: 2 KB
of internal RAM (worst `mem.dma_min` seen: 80 KB).

## 6. Requirements

In `components/control/README.md` (its first requirements) and `components/hmi_ui/README.md`.
Test IDs `CTL-0nn` are cases of the new host app `components/control/test`.

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-CTL-01 | Every ADC cycle evaluates the guard once, after the three reads and before the pipeline, on valid, invalid and calibrating cycles alike; that cycle's gate uses that verdict. | CTL-013, CTL-014 |
| REQ-CTL-02 | A cycle whose verdict is not OK sends a command of 0, whatever `stick_drives` says. XYTwist is still published on every valid cycle (neutral, not silence); the button bit is unchanged. | CTL-012, CTL-015 |
| REQ-CTL-03 | `UI_STALE`: the UI heartbeat's age is ≥ `UI_HEARTBEAT_MAX_AGE_MS` (200), or it was never written. | CTL-002, CTL-004 |
| REQ-CTL-04 | `MIB_STALE`: the newest MibStatus's age is ≥ `MIB_STATUS_MAX_AGE_MS` (2000), or none arrived since boot. `rtps_comms` stamps it on the receive task, state first, before any handler or lock. | CTL-003, CTL-004 |
| REQ-CTL-05 | `LINK_DOWN`: net failed, no link, or no IP. With REQ-CTL-04 this is "not CONNECTED". | CTL-005 |
| REQ-CTL-06 | `MCB_NOT_ENABLED`: the newest MibStatus's `systemState` is not ENABLED, out-of-range values included. | CTL-006 |
| REQ-CTL-07 | Ages are `uint32` ms modulo 2^32. A source seen stale stays stale until its stamp changes; a stamp ahead of the clock is stale. | CTL-008, CTL-009, CTL-010 |
| REQ-CTL-08 | `WDT_MISSING`: if the ADC or UI task failed to subscribe to the TWDT, the verdict is never OK. | CTL-011 |
| REQ-CTL-09 | The guard reads none of: `drive_request`, `DISABLE_PENDING`, the exit hold, the burger key, the drive phase (G4). | CTL-016, review |
| REQ-CTL-10 | The reported reason follows the order e, a, b, c, d (f); onset counters per reason and `ui_stalls_drive` only go up; `ui_age_max_ms` is the largest heartbeat age seen since the first heartbeat. All are written by the ADC task only. | CTL-007, CTL-017 |
| REQ-CTL-11 | The guard and the gate allocate nothing, log nothing, take no lock and make no indirect call; every shared value is an atomic with `is_always_lock_free` asserted. | CTL-018, static_assert, SU, review |
| REQ-CTL-12 | The ADC task subscribes to the TWDT at its first cycle and resets it at the end of every cycle, valid or not. | CTL-019, B5j |
| REQ-CTL-13 | `Read ADC` runs at priority 21, pinned to core 0, with a 6144 B stack. | G10, B3 |
| REQ-CTL-14 | Only if O1 = yes: after any non-OK verdict, the verdict is `REARM_PENDING` until the mounted position has been exactly 0 on all three axes for ≥ `NEUTRAL_REARM_MS`. | CTL-020, CTL-021 |
| REQ-UI-16 | The UI task stores the heartbeat after every completed cycle, subscribes to the TWDT at its first cycle and resets it after every cycle. | B3 (`ctl.ui_age_max`), B5i |
| REQ-UI-17 | Only if O7 = yes: when `ui_stalls_drive` goes up, the Drive screen shows "Display stalled: stick paused" for 3 s. | B5i |

Conditional rows are last, so dropping them leaves no gap in the series.

## 7. Tests (host L1, written from this spec)

New app `components/control/test` (`make test`, `make coverage`), manifest
`tests/manifest.d/control.yaml`: id `L1-CTL`, `safety: true`, requirements REQ-CTL-01..14.
Fake time only (TS-DET-02). 100 % branch coverage of `motion_guard.hpp` (it is safety logic).

H4 is not bench-testable on a release build. CTL-012..016 are its evidence: a deterministic
simulation of both tasks on fake time, with the UI stall injected.

| ID | Case | Expected |
| --- | --- | --- |
| CTL-001 | all fresh, carrier up, ENABLED, both WDT flags true | OK |
| CTL-002 | heartbeat age 0, 199, 200, 5000 ms | OK, OK, `UI_STALE`, `UI_STALE` |
| CTL-003 | MibStatus age 0, 1999, 2000 ms | OK, OK, `MIB_STALE` |
| CTL-004 | nothing written since boot; then heartbeat only | `UI_STALE`; then `MIB_STALE` |
| CTL-005 | fresh stamp with each of: net failed, no link, no IP | `LINK_DOWN` each |
| CTL-006 | state INITIALIZING, IDLE, ERROR, 4, 255; ENABLED | `MCB_NOT_ENABLED` each; OK |
| CTL-007 | all 32 subsets of conditions e, a, b, c, d (table-driven) | reason = first failing in order; OK only for the empty set |
| CTL-008 | stamp 2^32 − 50, now wrapped to 100 | age 150, OK |
| CTL-009 | seen stale at age 2000, then now + 2^32 with the same stamp; then a new stamp | still `MIB_STALE`; then OK |
| CTL-010 | stamp 1 ms ahead of now | stale |
| CTL-011 | `adc_wdt_ok` false; `ui_wdt_ok` false (all else fresh) | `WDT_MISSING` each |
| CTL-012 | **UI stall.** Simulated UI writes every 8 ms, ADC cycles every 35 ms through `StickPipeline::cycle` with full forward and `stick_drives` stuck true. UI stops at t0 | before t0 the command equals full forward × speed; every cycle starting ≥ t0 + 200 ms publishes x = y = twist = 0; the first 0 by t0 + 240 ms; publishes continue at the ADC rate |
| CTL-013 | as CTL-012, UI resumes at t1 | output returns on the first cycle after t1 (or per CTL-020 with O1) |
| CTL-014 | UI stall during invalid-read cycles and during a calibration run | the guard still advances: onset counted, latch set |
| CTL-015 | MCB → IDLE mid-drive, `stick_drives` still true (UI has not ticked) | the first cycle after the IDLE stamp publishes 0 |
| CTL-016 | G4: MCB ENABLED and fresh, UI alive, `stick_drives` true, the world has sent DISABLE (exit) | the output follows the stick (C4 does not stop it) |
| CTL-017 | counters: two stalls, one with `stick_drives` false | `guard_trips[UI_STALE]` = 2, `ui_stalls_drive` = 1, `ui_age_max_ms` = longest age |
| CTL-018 | 1000 cycles of guard + pipeline + gate under the host allocation guard, armed | 0 allocations (TS-DET-08) |
| CTL-019 | per-cycle sequence with a fake watchdog port, incl. invalid-read cycles; a failing subscribe | subscribe once, reset once per cycle after publish; failing subscribe → `WDT_MISSING` forever |
| CTL-020 | O1 only: trip, then the stick neutral 299 ms vs 300 ms | `REARM_PENDING`; OK |
| CTL-021 | O1 only: neutral broken at 200 ms, then 300 ms neutral | the 300 ms restarts |

Unchanged and must still pass byte for byte: `L1-STK` (goldens not regenerated), the drive
session oracle and goldens.

## 8. Bench checks (scripted verdicts)

B5f..B5h need the stick-injection build (`ci/sdkconfig.stick_inject`). No step grades XYTwist
while a `SHOT` runs: a screenshot holds the LVGL lock about 150-250 ms, so C4-a zeros the stick
then (bench only).

| Step | What | PASS |
| --- | --- | --- |
| SU (build, no board) | `-fstack-usage` of main's unit | the Read ADC invoker frame ≤ 256 B (208 B today); `read_twist_mv` 240 B; each new function on the path ≤ 96 B and `static`; no new `dynamic` entry |
| B2 | boot | markers as baseline; the self test's count marker moves 54 → 57 checks (declared change); **no `task_wdt` line at all** (today's 8 s IDLE0 line goes away with idle checks off) |
| B3 | self test | `mem.stk_adc` ≥ 2048 B (1.5 × rule on 6144 B); `time.adc_avg` 34.5..35.5 ms; `time.adc_max` ≤ 40 ms; new checks: `ctl.wdt` = 1, `ctl.ui_age_max` ≤ 1000 ms, `ctl.ui_stalls_drive` = 0; all other bands unchanged |
| B3 after stress | the stress-tasks run, then B3 | `mem.stk_adc` ≥ 2048 B (CS-MEM-04 is the stress value) |
| G10 | `task_dump.py fetch`, then `check` against the new baseline | exit 0; `Read ADC` prio 21, core 0, stack 6144 (−16 B allowed), `coproc_pinned` true |
| B5, B5a..e | existing | verdicts unchanged |
| B5f | MCB state, ADC side. Inject neutral, sim `a`, Drive, inject full forward, XYTwist y > 0.5 for 1 s, sim `i` | every XYTwist received > 120 ms after the first IDLE MibStatus publish is 0, in **10 of 10** repeats (the UI tick alone would meet 120 ms in about 0.07 % of 10-run sets) |
| B5g | stale MibStatus. As B5f, then sim `p` | every XYTwist after last publish + 2120 ms is 0; XYTwist still arrives at ≥ 25 Hz in [+2.12 s, +4 s]; 3 of 3 repeats |
| B5h | G4 holds. Sim `ign 50`, Drive, forward, exit hold | the sim stays ENABLED and XYTwist y > 0.5 for 5 s after the hold |
| Soak (phase 1 → 2 gate) | panic off. B4 walk, B4b, B3, a calibration run, 20 side-button presses on Drive (brightness saves), Settings changes, an OTA install from the local URL, a Wi-Fi scan and join, 10 min on Drive with neutral injected | zero `task_wdt` lines in the serial log; then a second B3: `ctl.ui_age_max` ≤ 1000 ms (half the timeout); `ctl.ui_stalls_drive` = 0 |
| B5i (O6) | `STALL UI 300`, then `STALL UI 3000` (bench verb: the remote UI holds the LVGL lock) | 300: XYTwist 0 by stall start + 240 ms, back after; 3000: reset, panic names `lv_task`, reset reason TASK_WDT, the last 1.5 s of XYTwist before the silence all 0 |
| B5j (O6) | `STALL ADC 3000` (bench-only sleep in the ADC cycle, compiled out otherwise) | reset; panic names `Read ADC`; the sim logs XYTwist silence from the stall and its `ongone` at 1 s |

B5f needs the sim's event log to carry each MibStatus publish (time, state); add it if missing.
`ctl.ui_stalls_drive` > 0 in the soak goes to the owner: move the slow work off the UI task, or
accept it.

## 9. Implementation notes

### 9.1 Files

| File | Change |
| --- | --- |
| `components/control/include/control/motion_guard.hpp` (new) | constants, `Verdict`, `MotionGuard` (stale latches, counters), `gate_open()`; pure C++, no IDF |
| `components/control/include/control/cycle.hpp` (new) | the per-cycle sequence (guard, pipeline, `note_cycle`, watchdog reset) as a template with no espp/IDF types, so the host test drives it |
| `components/control/include/control/stick_island.hpp` | calls `cycle.hpp`; owns the guard like the twist lowpass |
| `components/control/test/`, `tests/manifest.d/control.yaml` (new) | L1-CTL |
| `components/control/README.md` | REQ-CTL rows, the Tasks row (6144 / 21 / 0) |
| `components/hmi_ui` (`app_state`, `ui_island`, README) | `ui_heartbeat_ms`, `ui_wdt_ok`, the watchdog port; REQ-UI-16 (17) |
| `main/rtps_comms.cpp/.hpp` | `mcb_state`, `mcb_rx_ms`; one lock-free accessor in the order of §3.2 |
| `main/main.cpp` | clock and watchdog ports; `AdcStickIo::stick_drives()` → `gate_open(...)`; the `Read ADC` config literal |
| `main/selftest*.cpp`, `selftest_spec.hpp`, PC copy in `scripts/` | `ctl.wdt`, `ctl.ui_age_max`, `ctl.ui_stalls_drive` |
| `sdkconfig.defaults` | the TWDT lines of §4.2 |
| `tools/guards/baselines/tasks.json` | the §5 row and its `about` text |
| `tools/bench/` | B5f..h (i, j), the sim's MibStatus log, the B2 marker, the B3 baseline IDs |
| docs | `how-the-firmware-works.md` §4.1 and §6, `refactor.md` H4/H11 status, `hazard-fixes.md` C4 status |

### 9.2 Commit order

Refactor commits (R) change no behaviour; behaviour commits (B) each cite this spec and its REQs
(CS-GIT-02). C4 lands after C1 and C3 (§9 order); it composes with whatever gate they leave.

| # | Kind | Commit | Bench before merge |
| --- | --- | --- | --- |
| 1 | R | `motion_guard.hpp`, `cycle.hpp`, L1-CTL (unused by the firmware) | L1 only |
| 2 | R | the writers: UI heartbeat, `mcb_state`/`mcb_rx_ms` (written, not yet read) | B2, B3 unchanged |
| 3 | B | the ADC gate uses the guard; observability; the 3 self-test checks (declared) | B2, B3, B5..B5h |
| 4 | B | `Read ADC` 21 / core 0 / 6144 B and the G10 row (declared) | SU, G10, B3, B3 after stress |
| 5 | B | TWDT phase 1: subscriptions, resets, timeout 2 s, idle checks off, panic off | soak |
| 6 | B | TWDT phase 2: `CONFIG_ESP_TASK_WDT_PANIC=y`, after a clean soak | B2, B3, B5i/j if O6 |
| 7 | test | O6 only: bench `STALL` injection (`depends on HMI_REMOTE_UI`, compiled out otherwise); may come before 5 | B5i, B5j |
| 8 | B | O1, O7 if approved | CTL-020/021, B5i |
| 9 | docs | the docs rows | — |

The implementer never edits the STK goldens, the drive table, the topology draft or an expected
value to make a check pass. The declared changes this spec approves are: the G10 row, the
TWDT lines, the three self-test checks (with the PC copy, the B2 count marker and the B3
baseline IDs), and the README requirement rows.

## 10. Open questions for the owner

| # | Question | Recommendation |
| --- | --- | --- |
| O1 | After a C4 trip, wait for 300 ms of neutral stick before output resumes (G1's rule)? Without it, a stick held deflected jumps back when a stall ends. | yes (REQ-CTL-14) |
| O2 | ADC priority 21 (this spec) or 22 (topology draft; ties with `esp_timer` on core 0)? | 21; align the draft |
| O3 | Turn the idle-task checks off, or keep them and first fix the 8 s boot hog on `main`? | off |
| O4 | Subscribe `rtps_pub` too? It is not safety, and the panic would reset mid-drive for it. | no; revisit with the `net` island |
| O5 | TWDT timeout 2 s (not in D4). | 2 s, confirmed by the soak |
| O6 | Add the bench-only `STALL` verbs, so the trip and the reset reason are seen on the board? | yes |
| O7 | Show "Display stalled: stick paused" after a stall while driving (CS-SAF-03 says shown)? Or leave it to C3's fault indicator? | yes, REQ-UI-17 |
| O8 | MCB team (D3 Q1): the XYTwist timeout T_mcb. It is the only stop for an ADC stall, a panic or a link loss. Propose asking for ≤ 500 ms. | ask now; it bounds this fix |
| O9 | `ContinuousAdc T` (X/Y producer, espp, priority 5, unpinned) can starve and serve old X/Y without a failed read. Handle in C2 (rate check)? | yes, in C2 |
| O10 | With the UI alive and the ADC task dead, should the UI send DISABLE (an ADC heartbeat check in C1's table)? It would give the HMI a stop path that does not need the ADC task. | lane D to cost it |
