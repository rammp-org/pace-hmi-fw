# Refactor progress (living report)

## Release candidate `dev_ai_rc` (2026-10-15)

**RC commit:** the `dev_ai_rc` commit that adds this line. Its code is `f3574c4` (the cppcheck
fix 61d91b7 and the stale-comment fix f3574c4 on top of 478d03d); the commit itself changes
only this file and testing-overview.md. The bench images are built from it, so a board running
the RC shows a version ending in its own short SHA.

**Status:** built, partly benched, **TEST-ONLY and held**. Not merged into `dev_refactor` (the
owner's "hold the candidate" decision stands); nothing went to `dev` or `main`; no PR.
**Why test-only:** every espp component comes from the local espp worktree
`~/Documents/ATDev/w/espp-738` through `override_path` (`main/idf_component.yml`): espp `main`
v1.3.8 + espp PR #738 + our seven PR branches (1-4, A, B, C, drafts in
`~/Documents/ATDev/espp_pr_drafts`). CI's runners do not have that folder, so CI cannot build
the RC until espp merges the PRs and the RC moves to released espp.

Terms: MCB = main control board (the chair's motor controller); RTPS = Real-Time
Publish-Subscribe (the wire protocol to it); POST = power-on self test; C1-C4, C2b = the
hazard-fix groups; B0..B5e, B5'', B5f-j, C2-n, B5s-n = bench steps
([testing-overview.md](../testing-overview.md)); J1/J2 = the joystick items of the
vendored-joystick review; T-numbers = owner decisions (hazard-decisions.md).

### What the RC contains

| Layer | From |
| --- | --- |
| `dev_refactor` (de6ad14): structural refactor, C1 + C3 hazard fixes, coverage and Doxygen ratchets, on-chip allocation guard | merged in |
| Hazard candidate: C4 motion guard and watchdogs, C2 stick monitor, C2b stop on a stick FAULT, RTPS workers and the ContinuousAdc task at priority 20 (T11, E5), T1 boot DISABLE after the writer matches | `dev_ai_hazard_candidate` |
| Seat path: one seat gate, seat values validated, publish checked, never re-sent (H6 seat, H8, H10 seat) | `dev_ai_hazard_seat` 440665b |
| No vendored espp: `espp_adc`, `adc_window`, `espp_rtps`, `m5stack-tab5`, `joystick` deleted; J1 (twist mapper) and J2 (`hmi::stick::mapping_selftest`) in `components/stick` | `dev_ai_integ_espp_prs` |
| espp PR #738 base; the flush callbacks signal LVGL themselves (#738's contract) | 65d3c51, 8b365c6 |
| `main.cpp` shrink, no behaviour change: new components `log_capture`, `net_link`, `post_facts`; main's wiring into `ui_ports`, `cues`, `stick_io`, `task_configs`, `bench_hooks`, `selftest_platform`; `DisplayFlip::start_direct_render` | dc88fae..ba2c7b3 |
| Bench tools on Linux; B3 accepts a SKIP of `net.wifi_rssi` on Ethernet (owner, 2026-10-15) | `dev_ai_bench_linux`, `dev_ai_bench_b3` 71c0990 |
| Docs: [fw-overview.md](../fw-overview.md), [testing-overview.md](../testing-overview.md), every doc and diagram brought to the RC | `dev_ai_docs_final` 1fb0ba1 + this update |

### What was benched (board 2, `~/bench/results`)

| Run | Image | Result |
| --- | --- | --- |
| `integ-0eba858-20261014-221329` (+ `-retry1`) | the RC's code on espp `test-all-prs` (before #738), wired LAN | 77 PASS, 2 FAIL, 2 NOT_RUN, 1 RECORD, 2 SKIP; after one retry 78 PASS, 1 FAIL: B3 `net.wifi_rssi` SKIP on Ethernet (now accepted by the B3 rule). C2-15 (30-min soak) and C2-16 PASS for the first time |
| `integ-0eba858-allocguard-…` | same, allocation-guard build | `mem.alloc_adc` 491 (first 20 B), `mem.alloc_rtps` 0: PR 3 is not the fix |
| `integ738-8b365c6-20261015-012759` | on espp `test-all-on-738` (with #738) | 73 PASS, 1 FAIL (B3 `net.wifi_rssi`, as above), 7 NOT_RUN (stick-injection lapses B5''-1b, -18c, C2-6, -11, -15; a network stall B5''-20; C2-13 by design), 1 RECORD, 2 SKIP. Owner's hand test: OK, POST 19/19 |
| `ctl-0eba858-lan-20261015-030101` | control: the same network without #738 | 74 PASS, 2 FAIL (B3, C2-8), 5 NOT_RUN: the lapses are the network, not #738 |
| `heapdiag-integ738-20261015-041528` | heap-trace build | Read ADC's allocations attributed (PR 3 item below) |

The RC's own commits after 8b365c6 are refactors with no behaviour change: each was built in
all four variants with 0 warnings, G4 init_order, G2 exports and the release-ELF checks PASS,
and the stick task's cycle compared with 8b365c6's by objdump: identical, except
`StickIsland::read_twist`'s register allocation (76e4137). No
board run of `ba2c7b3` itself is in `~/bench/results` at the time of writing. The B3 Ethernet
rule is proven by its selftests (CMP-010..013), not yet on a board.

### Open items for the owner

1. **J2 coverage decision.** `components/stick/src/mapping_selftest.cpp` is a new safety file
   below the 100 % branch rule: every gcov branch in it is a compiler-made exception edge. The
   options are in `docs/plans/j2-coverage-review.md` on branch `dev_ai_stick_j2` (not merged).
2. **PR 3 rework after the heap trace.** The Read ADC allocations are in espp's RTPS publish
   path, not where PR 3 looked: `SimpleHistoryCache::addChange` 20 B per publish, the
   `ThreadPool` deque 504 B every 21st publish, `Publisher` `buffer_` once (the guard's "first
   20 B"). See `~/Documents/ATDev/espp_pr_drafts/PR-3-rtps-publish-noalloc.md`. PR 3 stays on
   hold until it is reworked.
3. **PR A/B: NOT READY.** Rebase onto #738's head, then a wired re-run to confirm that the
   stick-injection lapses on the 192.168.1.x LAN were the network.
4. **`main.cpp` is 491 lines.** What is left is `lv_*` and lock code. The ratchet allows
   `lv_*` only in the main unit and the UI paths (`lv_outside_ui`) and a lock only where its
   legacy count is (`locks`: `main.cpp` 19, new files 0), so it can live only in `main.cpp` or
   `hmi_ui`. Getting to ~250 lines needs a ratchet-rule decision (for example, a UI-path and
   lock allowance for one more main file).
5. **C2-11: the slow Settings screen.** While the Settings screen loads (~1 s) the remote UI
   task (priority 5) waits behind `lv_task`, so C2-11 is NOT_RUN every time (injection lapse).
   Bench-only path; needs a faster Settings load or a decision on the step.

Also found while writing this (not in the list above):
- Fixed (61d91b7): CI's cppcheck failed the RC on one style finding, `main/cues.cpp:20`
  `cues_attach`'s parameter `cues` in the definition and `feedback` in the declaration
  (`funcArgNamesDifferent`). The declaration now says `cues`.
- With espp's `get_windows()` (PR 1) the stick task's window read takes a lock inside espp,
  where the vendored copy was lock-free (owner decision pending, `components/control/README.md`).
- Fixed (f3574c4, comments and citations only): `main/idf_component.yml`'s comment now names
  `../../espp-738` / `test-all-on-738`; `tools/guards/baselines/tasks.json`'s `tab5_audio`,
  `tab5_audio microphone` and `tab5 interrupts` rows cite espp's m5stack-tab5 (the guard reads
  neither `source` nor `origin`); `display_flip.cpp`'s header names espp's board support;
  `components/alloc_guard/README.md` says 60 checks (63 with the alloc rows) and 20 B per
  publish from espp's RTPS publish path, as the heap trace found.
- Diagram D3 folds espp into the ESP-IDF node on the RC (the `override_path` folders sit
  outside the project); architecture.md lists which ones are espp.

### Checks on this RC commit

- cppcheck 2.20.0 (the version in CI's `jdomagala/static_analysis` image), CI's arguments and
  file list (403 files, `components/ui` out): 0 findings; the same run on 478d03d reproduces
  CI's one finding.
- Bench images `build_bench` and `build_bench_inject` (stick injection), clean builds with
  IDF 6.0.0: 0 warnings. G4 init_order and G2 exports on them, L0 and gen_diagrams check: PASS.
- On 478d03d (docs): L0 as in `l0.yml` (ratchet, include_boundary, pre-commit, ui_contract,
  gen_diagrams, guard selftests, sim and bench selftests, `tests/run.py` selftest and check):
  PASS. Doxygen gate (doxygen 1.9.8): PASS.
- Not run on this commit: host L1, the release and alloc-guard variants (the code change since
  ba2c7b3's four-variant build is one parameter name).

---

Last update: 2026-10-12 (end of the weekend run), `dev_refactor` at 82880cc. Nothing went to
`dev` or `main`; no PR.

## Where it stands

**Structural refactor: done (~97%).** fw-standards rules fully met: ~58% (strict count).
Hazard fixes: C1+C3 merged; C4, C2, C2b and the seat path implemented and benched on branches,
waiting for ONE owner decision (below) before they merge.

| Area | State |
| --- | --- |
| Fragments | none (26 → 0) |
| `main.cpp` | 900 physical / 536 code lines (CS-FIL-01 met); `app_main` 165 |
| Components | board (bring-up on the Tab5 BSP), drive_ui (safety), control, housekeeping, hmi_rtps_spec, alloc_guard … see docs (espp_adc and adc_window removed 2026-10-13: espp as released) |
| On `dev_refactor` | C1 + C3 hazard fixes; coverage ratchet; Doxygen ratchet; on-chip allocation guard (bench builds); settings post-check; all benched |
| Branch `dev_ai_hazard_candidate` (e65c051) | C4 + C2 + C2b + approved RTPS-worker / ContinuousAdc priority 20 + T1 (boot DISABLE after match, vendored espp_rtps) + all owner decisions of 2026-10-11. Bench cand4: 59 PASS; remaining FAILs: `joy.xy_age_max` 210 > 200 at boot (B3, C2-15), see decision below |
| Branch `dev_ai_hazard_seat` (440665b) | Seat path on top of the candidate: B5s-1..17 + B5''-19 PASS. Merges after the candidate |
| Branch `dev_ai_docs_final` (1fb0ba1) | `docs/fw-overview.md` (whole-FW diagram: tasks, components, 26 channels, walk-throughs), `docs/testing-overview.md` (all testing in one diagram), refreshed how-the-firmware-works and other diagrams. Merges after the candidate |

## Decision needed to finish (one)

`joy.xy_age_max` self-test limit: the boot transient varies 112–245 ms run to run (the same
firmware read 114 and 210); the self test's limit is 200 ms while your T16 set C2-14's band to
250 ms until measurement M8. Options: (a) 250 ms for the self-test row too, until M8; (b) start
the maximum after warm-up (first OK sample). L1 showed the T1 code is not the cause. With either,
the candidate, the seat branch and the docs merge after one confirming bench run.

## For owner review (taken under the standing rules, recorded in hazard-decisions.md)

- Measurement fixes: sim stamps samples on a receive thread; B5''-18 graded from the gate leaving
  PASS (+35 ms); a proven link stall makes a step NOT_RUN and retried, never PASS; clock check
  (the IDF Python 3.11 timer has 15.6 ms steps); B5''-18c graded like your C2-8 decision.
- T1 needed a per-topic match signal espp-rtps does not have: espp-rtps vendored (like espp_adc)
  with a small `matched_readers(topic)` accessor, offered upstream.
- A permission-system block was not worked around: an A/B diagnostic build for the boot X/Y age.

## Findings for you

- Two I2C boot flakes on board 2 this weekend (one missing device; once 11 devices timed out);
  POST caught both and the next boot was clean. Check the internal I2C bus.
- Read ADC allocates after start-up (940 by the end of a bench run, first 20 B, not the 24 B of
  espp's topic string): source not yet pinned (T12 accepted for now; a heap trace would show it).
- The remote UI (priority 5) starves on core 1 while a Settings page loads (~1 s): bench-only
  path, makes C2-11 NOT_RUN.
- MCB team questions (hazard-fixes.md §9 and seat S1-S9) and measurements M1-M8 are still open.

## Later list

- Oracle reduction (today 354 M steps, ~40 min): D (parallel, nightly), then A (split the drive
  session by concern, ~615 k steps) with a one-time equivalence proof.
- Remaining compliance (~4-6 days + decisions): topology wiring, CODEOWNERS/branch protection,
  clang-tidy backlog, selftest rework, coverage up to the spec thresholds (12 of 16 below),
  Doxygen warnings (878).

## Decisions taken on your delegation (review these)

| Decision | Choice |
| --- | --- |
| Cal codec | `DecodeError` enum instead of `std::error_category` (no guard allowance); CAL-1xx mapped 1:1 |
| Language level | gnu++23 for all first-party code (main's image code byte-identical) |
| Drive adapter instance | init_order allow entry accepted (planned in §2.1) |
| Lock visuals | stay in main (the frozen golden 2 boundary is not re-baselined) |
| Topology wiring | waits for your review of the TASKS rows (names, CONTROL prio/core/stack = C4/H11) |
| Module moves | init_order "moved?" entries accepted after review (self-contained objects only) |
| remote_ui | keeps its UI-path status at its new path (exact file only) |
| rtps_comms, selftest | stay in main until the channels rework (CS-LAY-01 deviation rows) |
| Bench last-good | saves on PASS, or RECORD with clean graded parts |
| fw-standards | branch `ai/cmake-include-order` (not merged): CMake include order, CS-CON-02 wording |

## Owner decisions at the pause (2026-10-07), to act on when resuming

| # | Question | Decision |
| --- | --- | --- |
| P1 | The decisions taken on the owner's delegation (table above) | **Approved, all of them** |
| P2 | Topology TASKS rows vs today's task configs | **Decide later**: keep literal TaskConfigs in the islands; no topology wiring for now |
| P3 | POST limits (D4 placeholders) | **Measure first**: bench characterisation across boards 1-3 to propose values (board 2 only on this bench today) |
| P4 | Hazard fixes C1-C4 and the seat path | **Not yet**: finish the refactor only; hazards in a later session |
| P5 | fw-standards branch `ai/cmake-include-order` | **Merge into fw-standards `dev`** (approved) |
| P6 | Scope when resuming | **Plan scope**: finish app_main ≤300 (StickIsland, adapters, RtpsUiBridge, §4 view inits), bench and merge the modules and K1 branches, refresh the docs; rtps_comms and selftest stay in main |
| P7 | Agent budget | **2 Opus lanes + Sonnet bench** |

## Findings worth knowing

- Burger key while driving: the gate stays open until the MIB stops (12 non-zero XYTwist in
  0.39 s on the bench).
- ADC task free stack can be as low as ~1.35 KB; size it in C4.
- A Wi-Fi scan briefly interrupts RTPS (the HMI relocks on link loss: the safe direction).
- The network round-trip check (B3 rtt) flakes on the hotspot in daytime; judged by same-hour
  alternation, never by hand.
- picolibc's buffered-FILE off-by-one should be reported to Espressif; IDF HEAP_TASK_TRACKING
  crashes core-1 start-up on P4.

## Remaining time (estimate)

Refactor plan: about 1-2 hours (docs refresh once the denied actions are
decided). Hazard fixes C1-C4: about 6-10 hours after the owner's review. Full
fw-standards compliance: about 3-5 days of agent work plus the owner decisions above.
