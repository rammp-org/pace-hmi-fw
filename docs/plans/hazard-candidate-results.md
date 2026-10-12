# Hazard fixes: Tuesday candidate, bench results

Status: for the owner (2026-10-11). Branch `dev_ai_hazard_candidate`: `dev_ai_hazard_c2b` (d13adcc:
C1, C3, C4, C2, C2b) with the two priority proposals merged in (not yet approved, see §3) and the
bench tools' fixes below.

| What | Value |
| --- | --- |
| Firmware | 473b8d5 (tools after it: fe12d19, dfee8f7; no firmware change) |
| Image | `C:\b\dev_ai_hazard_candidate_bench`, v3.0.0-alpha-663-g473b8d5, bench + stick inject + wifi.local |
| Gates | default, bench, bench_inject: 0 warnings, init_order and exports PASS; ratchet, include_boundary, gen_diagrams, ui_contract, `tests/run.py check` PASS; host tests 20 PASS, 1 SKIP (on demand); CI green |
| Bench | board 2 on the hotspot, `run_bench.py --flash --no-save --hazard c1,c3,c4,c2`: `C:\b\bench\results\candidate-473b8d5-20261011-155348`; retries `candidate-473b8d5-retry1`, `-retry2`, `-c28` |
| Board | left on the candidate image |

## 1. Verdicts

After the retries (lapsed steps run up to twice more) and the two grader fixes found on the way
(§2): **59 PASS, 1 FAIL, 3 NOT_RUN, 1 RECORD, 2 SKIP** of 66.

| Step | Verdict | Step | Verdict | Step | Verdict |
| --- | --- | --- | --- | --- | --- |
| B0 | PASS | B5''-8 | PASS | B5j | PASS |
| B1 | PASS | B5''-9 | PASS | C2-1 | PASS (retry 1) |
| B2 | PASS | B5''-10 | PASS | C2-2 | PASS |
| B3 | PASS | B5''-11 | PASS | C2-3 | PASS |
| B4 | PASS | B5''-12 | SKIP (retired by C2) | C2-3b | PASS |
| B4b | PASS | B5''-13 | PASS | C2-4a | PASS |
| B5 | PASS | B5''-14 | PASS | C2-4b | PASS |
| B5a-B5d | PASS | B5''-15 | SKIP (retired by C3) | C2-4c | PASS |
| B5e | RECORD | B5''-16 | PASS | C2-5 | PASS |
| B5''-1 | PASS | B5''-17 | PASS (retry 1) | C2-6 | PASS |
| B5''-1b | PASS | B5''-17b | PASS | C2-7 | PASS |
| B5''-1c | PASS | B5''-18 | PASS | C2-8 | PASS (after the grader fix) |
| B5''-2 | PASS | B5''-18b | PASS (retry 1) | C2-9 | PASS |
| B5''-3 | PASS | B5''-18c | PASS | C2-10 | PASS |
| B5''-4 | PASS | B5''-19 | PASS | C2-11 | **NOT_RUN** |
| B5''-4b | PASS | B5''-20 | PASS (retry 1) | C2-12 | PASS (retry 2) |
| B5''-5 | PASS | B5''-21 | PASS | C2-13 | **NOT_RUN** (by design) |
| B5''-6 | PASS | B5''-22, 22b | PASS | C2-14 | PASS |
| B5''-6b | PASS | B5f, B5g, B5h | PASS | C2-15 | **NOT_RUN** |
| B5''-7 | PASS | B5i | PASS | C2-16 | **FAIL** |

## 2. Every non-PASS, and its cause

| Step | Where | Cause | Status |
| --- | --- | --- | --- |
| B5''-17, B5''-18b, C2-1 | main run | stick-injection lapse in a graded window (0.28-0.33 s refresh gaps over the hotspot) | PASS on retry 1 |
| B5''-20 | main run | the bench sim had gone after the step's reboot (`AttributeError: 'NoneType' ... 'reply'`; the sim restart logged "process not found") | PASS on retry 1 |
| C2-12 | main run, retry 1 | tooling: the sim lowercases its stdin, so the mark `on-Home` was logged as `on-home` and never found | fixed (fe12d19, BENCH-044e); PASS on retry 2 |
| C2-8 | main run (lapse), retries 1-2 FAIL | grader: C2 §9 says "exactly one DISABLE in 600 ms", but after the stop's DISABLE (rows 55-56) the relock sends its own once the MCB has left ENABLED (C1; GLD-126's second tick `P(D)`): 214 ms later on the board. The firmware follows the table | grader fixed (dfee8f7, BENCH-043); PASS (`-c28`). Owner: confirm C2 §9's C2-8 row should read "one DISABLE while ENABLED, the relock's after" |
| C2-11 | main run and both retries | lapses of 1.0 s each time: a STATE reply takes ~1 s while the Settings screen loads, because the remote UI task (priority 5) sits on core 1 under `lv_task`; the STICK refresh waits behind it on the shared connection. Bench-only path (remote UI), the same starvation as §3 | open (bench). Options: the remote UI's own priority, or STICK on its own connection |
| C2-15 | main run and both retries | the 30 min soak always sees a few lapses of 0.28-0.5 s over the hotspot | open (bench network): NOT_RUN skips the soak's self test, so its `joy.*` values were not graded |
| C2-13 | all | by design: the way out is a calibration run, which writes flash, not allowed on the bench | owner's hand test |
| C2-16 | main run | STALL CADC suspends the X/Y producer, but the step injects the stick, and an injected sample is fresh each cycle (REQ-STK-26): the monitor never sees the starvation. C2 §9's C2-16 and REQ-STK-26 conflict | owner decision ([hazard-c2-cadc-priority.md](hazard-c2-cadc-priority.md) §4) |
| B5e | all | a characterisation step: RECORD by design | - |

Fixed in the tools on this branch before the run (473b8d5): C2-3 accepts the stop's notice
(STOPPING, MCB_DID_NOT_STOP) while the phase is EXITING or EXIT_REFUSED (C1 §2.7, the user's stop
ranks first); C2-10 expects the re-entry to be stopped on the same tick (a DISABLE within 1.0 s of
the re-enable, Locked within 1.5 s of it) instead of Drive polls.

## 3. What is left for the owner

1. The two priority proposals merged into this candidate, each with its evidence: the RTPS workers
   at 20 ([hazard-c4-rtps-priority.md](hazard-c4-rtps-priority.md); B5i passes on this image) and
   the ContinuousAdc task at 20 ([hazard-c2-cadc-priority.md](hazard-c2-cadc-priority.md); no
   STALE fault from UI work in this run). Without them B5i fails and C2 latches a stick FAULT on
   ordinary UI work.
2. C2-16 under injection (REQ-STK-26 vs C2 §9): keep the real windows' sequences in an injected
   sample, or run C2-16 with the injection paused.
3. C2-8's row (§2): two DISABLEs, as the table and GLD-126 give.
4. B5i's 240 ms bound: end-to-end over Wi-Fi or on the board's publish
   ([hazard-c4-rtps-priority.md](hazard-c4-rtps-priority.md) §5).
5. `joy.xy_age_max` at boot (C2-14 passed here; 203 ms seen once on another run; M8).
6. The bench's remote UI on core 1 (C2-11's lapses) and the boot DISABLE race B5''-16 (T1), both
   already on the list.
7. New user texts: the calibration reject "Calibration failed: an end reads too high." and the
   Joystick screen's fault words (C2 commit 8).
