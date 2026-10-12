# Refactor progress (living report)

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
| Components | board (bring-up on the Tab5 BSP), drive_ui (safety), control, housekeeping, hmi_rtps_spec, adc_window, alloc_guard, espp_adc (vendored) … see docs |
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
