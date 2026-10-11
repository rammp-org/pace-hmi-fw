# Decision note: XYTwist stops during a UI stall (C4 B5i): the RTPS worker priority

Status: **PROPOSAL for the owner** (2026-10-11). Branch `dev_ai_hazard_c4_rtpsprio` (off
`dev_ai_hazard_c4` 1e8facb). Task configs are ask-first and this change is outside the approved
C4 spec ([hazard-c4-spec.md](hazard-c4-spec.md)), so it is not on the C4 or C2 branches.

## 1. The finding

hazard-c4-spec.md §6 expects a UI stall to give "C4-a (the HMI sends neutral) ≤ 240 ms; XYTwist
keeps flowing". On the bench (run `c4d-8dbd387-20261011-065658`, B5i) XYTwist **stopped** for the
whole stall instead.

Why: the stick task does not send XYTwist itself. `publish()` adds the sample to the RTPS writer's
history and hands the send to espp RTPS's worker pool (`EsppTransport`, managed component
`espp__rtps`): two tasks, `rtps_worker_0/1`, priority 5 (`config_esp32.hpp`
`THREAD_POOL_READER_PRIO`), each pinned to the core of its first FPU use (boot-dependent; on the
board: worker 0 on core 1, worker 1 on core 0). `lv_task` runs at 20 on core 1. While it is stuck
busy, nothing at 5 on core 1 runs. The pool wakes one worker for the writer's drain and folds later
sends of that writer into it, so when the woken worker is the one on core 1, the worker on core 0 is
never woken: every send waits for the UI task.

The same starvation is behind the slow remote-UI replies at the Drive screen's load seen earlier
(`remote_ui`, also 5 on core 1 on that boot), and it applies to `ContinuousAdc Task` (§4).

## 2. Evidence

| Run | What | Result |
| --- | --- | --- |
| `c4d-8dbd387…/hazard/b5i` (C4 image) | STALL UI 300, STALL UI 3000 | the sim gets no XYTwist during either stall: last message 78 ms after the 300 ms mark, then none until the stall ends; at 3000 `hmi_gone` (1.48 s silent) and `hmi_back` after 3.08 s |
| `l4-probe-uistall` (C4 image) | Locked, stick centred, 6 × STALL UI 1800 | 6 of 6 silences of 1.80-1.84 s |
| `c4prio-9e92595-20261011-074735` (this branch) | B0-B5e, B5f-B5j | below (§5) |

## 3. Options

| # | Change | Consequences |
| --- | --- | --- |
| 1 (**proposed**, this branch) | at RTPS start (each participant), find `rtps_worker_0/1` by name and `vTaskPrioritySet` them to 20 (`main/rtps_comms.cpp` `raise_rtps_workers`, logged); G10 rows 5 → 20 | on core 1 a worker shares time with a stuck `lv_task` (equal priority, 1 ms slices) instead of starving: XYTwist flows through a UI stall. Stays below `Read ADC` (21) on core 0, so the stick task is never delayed by it. The workers also run the receive callbacks: `on_mib_status` takes the LVGL lock at 20 instead of 5 (priority inheritance either way). Network work on core 1 now competes with UI rendering at equal priority (bench B4/B4b/B5 unchanged, §5). No edit to the managed component |
| 2 | the same at 21 or higher | the worker on core 0 would tie with or preempt `Read ADC` (21): the stick cycle could wait behind a send. Not proposed |
| 3 | pin the pool to core 0 | not possible from our code: espp creates the workers, and IDF FreeRTOS cannot re-pin a created task (they pin themselves at first FPU use). Needs a change to `espp__rtps` (managed) |

Not chosen either: lowering `lv_task`, or making the UI stall "polite" (a real hung LVGL loop is
busy, which is what the bench STALL models).

## 4. `ContinuousAdc Task`: the parallel decision

`ContinuousAdc Task` (espp's X/Y producer, 5, FPU-pinned, boot-dependent core; owner decision E5:
"detect only, decide pinning from M8 and C2-16") starves the same way when it lands on core 1.
Measured on the C2 image (`dev_ai_hazard_c2` 2b2b219, its monitor reports X/Y window ages), stick
injection stopped (the real stick at rest), Locked, ContinuousAdc on core 1 on that boot
(`l4-probe-cadc-1`):

| STALL UI | `xy_age_max_ms` after | Monitor |
| --- | --- | --- |
| 150 ms | 177 (unchanged: the window period) | OK |
| 250 ms | 253 | OK |
| 500 ms | 538 | **FAULT, STALE horizontal** (5 STALE samples), latched |

So with C2, a UI stall of about 300 ms or more latches a stick FAULT (cleared only by a
recalibration or a reboot) whenever the producer sits on core 1. This option-1 change does not
touch it; the same remedy (raise `ContinuousAdc Task` to 20 at start, or pin it to core 0 through
its config, which our code makes) is for the owner's E5 decision, with C2-16 and M8.

## 5. Bench results of this branch

Image `C:\b\dev_ai_hazard_c4_rtpsprio_bench`, v3.0.0-alpha-638-g9e92595 (bench + stick inject +
wifi.local); boot log: `rtps_worker_0: priority 5 -> 20`, `rtps_worker_1: priority 5 -> 20`.

| Step | Verdict |
| --- | --- |
| B0, B1, B2, B3, B4, B4b, B5, B5a-B5d | PASS |
| B5e | RECORD |
| B5f, B5g, B5h | PASS |
| B5j | PASS (phase 1: report names Read ADC, no reset, 3.05 s silence, then XYTwist again) |
| B5i | XYTwist now flows through both stalls (300: 3 zero samples in the stall; 3000: 79 zero samples, none non-zero) and phase 1 passes (report names `lv_task`, no reset). The "first zero by stall start + 240 ms" check passed 2 of 4 runs |

First zero after the stall's mark, 4 runs: STALL UI 300: 0.217, 0.219, 0.227, 0.221 s; STALL UI
3000: 0.249, 0.207, 0.260, 0.223 s (allowance 240 ms + the command's one-way, 1-5 ms). On the
board the guard trips at most one ADC cycle after the heartbeat is 200 ms old, so the zero leaves
by 235 ms after the last heartbeat before the request; what is left of the 240 ms (≈ 5 ms) has to
carry it over the hotspot Wi-Fi. The two misses (9 and 20 ms over) are that margin, not the stall
path. Owner question: keep 240 ms as an end-to-end bound (then the HMI side needs a shorter
`UI_HEARTBEAT_MAX_AGE_MS` or cycle), or grade it on the board's own publish time.

## 6. What the owner decides

1. Option 1 (workers at 20) yes / no.
2. `ContinuousAdc Task` (E5): raise or pin now, given §4.
3. B5i's 240 ms: end-to-end, or on the board's publish.
