# Decision note: UI work starves C2's X/Y producer into a STALE stick fault

Status: **PROPOSAL for the owner** (2026-10-11), decision E5 of
[hazard-decisions.md](hazard-decisions.md) ("ContinuousAdc: detect only, decide pinning from M8
and C2-16"). Branch `dev_ai_hazard_c2_cadcprio` (off `dev_ai_hazard_c2` f03d07a). Task configs are
ask-first, so this is not on the C2 branch. Companion: [hazard-c4-rtps-priority.md](hazard-c4-rtps-priority.md)
(the same starvation, of the RTPS send workers).

## 1. The finding

C2's monitor judges the X/Y windows of espp's `ContinuousAdc Task` (priority 5, pinned to the core
of its first FPU use: core 1 on the boots measured). `lv_task` runs at 20 on core 1. While the UI
is busy (a screen load, a stall), the producer does not run, the window sequences stop, and after
`XY_STALE_MS` (300 ms) the monitor sees STALE; three such samples latch a FAULT, which holds the
stick until a recalibration or a reboot (REQ-STK-22).

## 2. Evidence

| Source | What | Result |
| --- | --- | --- |
| C2 bench run `c2-2b2b219-20261011-082039` | before the first hazard step (after B2-B5) | FAULT STALE horizontal, `xy_age_max_ms` 977 |
| same | every visit to Skunk Works (B5''-16, 20, 21 clean-ups) | FAULT STALE horizontal, X/Y age 617-772 ms |
| same | B5''-1c FAIL, B5''-2, 3, 4, 4b, 6, 6b, 8 NOT_RUN | their set-ups saw hold reason STICK_FAULT (the latched fault above), until B5''-10's reboot |
| probe `l4-probe-cadc-1` (C2 image, injection off, Locked) | STALL UI 150 / 250 / 500 ms | X/Y age 177 (window period) / 253 / 538 ms; the 500 ms stall latched FAULT STALE |

## 3. Options

| # | Change | Consequences |
| --- | --- | --- |
| 1 (**proposed**, this branch) | `task_priority = 20` in main's StickIsland `ContinuousAdc` config (espp's default 5); G10 row 5 → 20 | equal to `lv_task`: on core 1 the producer shares time with a busy UI instead of starving; below `Read ADC` (21) on core 0. Its work is one window every 128 ms. No vendored code changes (the config field exists) |
| 2 | pin it to core 0 | espp's `ContinuousAdc` takes no core in its config; needs a change to the vendored copy beyond REQ-CTL-16 |
| 3 | raise `XY_STALE_MS` (D5) | hides the starvation, does not stop it: a screen load of ~1 s still latches, and stale X/Y drive for longer |
| 4 | none (detect only, E5 as answered) | C2 latches a stick FAULT on ordinary UI work (a Skunk Works visit): not usable on the chair |

## 4. Bench results of this branch

Image `C:\dev_ai_hazard_c2_cadcprio_bench`, v3.0.0-alpha-649-g7a10576 (bench + stick inject +
wifi.local). Full run with all hazard groups (`c2cadc-7a10576-20261011-091102`, tools at f03d07a):
51 PASS, 1 RECORD, 5 FAIL, 7 NOT_RUN (C2-13 by design).

- No STALE fault from UI work any more: B5''-1c PASS, and B5''-2, 3, 4, 4b, 6, 6b, 8 PASS (on the
  2b2b219 image they were FAIL / NOT_RUN on a latched STALE fault). C2-1..C2-7 and C2-10 PASS.
- B5i FAIL: known (XYTwist stops in a UI stall; [hazard-c4-rtps-priority.md](hazard-c4-rtps-priority.md)).
- B5j FAIL: a grader boundary (the silence began one sim clock tick before the mark); fixed in
  the tools on `dev_ai_hazard_c2` (c649508).
- C2-8 FAIL, C2-9 NOT_RUN: they grade C2b's drive-table rows (lane D), not in C2; the tools now
  report them NOT_RUN without C2b (c649508).
- C2-14 FAIL: `xy_age_max_ms` 203 > 200 after a reboot (boot work); M8 to measure. The 1.5x
  margin of the pass band over the 128 ms window is thinner at boot than the spec assumed.
- C2-16 FAIL ("FAULT STALE: never"): STALL CADC suspends the producer, but the step injects the
  stick, and an injected sample is fresh each cycle (REQ-STK-26): the monitor judges the
  injection's sequences, not the starved producer's. The spec's C2-16 and REQ-STK-26 conflict.
  Owner question: (a) let an injected sample keep the real windows' sequences (unless mask bit 6
  freezes them), so the injection shows the producer's liveness; or (b) run C2-16 with the
  injection paused (the real stick at rest) and grade FAULT STALE only.
- B5''-7, C2-2, C2-11, C2-15 NOT_RUN: stick-injection lapses in graded windows (0.30-0.36 s;
  C2-11's 0.9 s is a STATE reply during the Settings screen's load, the remote UI on core 1).
  C2-12 NOT_RUN followed C2-11's clean-up (Skunk Works did not open); the tools now fall back
  to a port reset (c649508).

## 5. What the owner decides

1. Option 1 (`task_priority` 20) yes / no (E5).
2. C2-16 under injection: (a) or (b) above.
3. `joy.xy_age_max` / C2-14's 200 ms band at boot (M8).
