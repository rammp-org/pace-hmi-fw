# Refactor progress (living report)

Updated by the orchestrator after each merge into `dev_refactor`. Last update: 2026-10-07 (PAUSED at the owner's request),
`dev_refactor` at ec219e4. Nothing has gone to `dev` or `main`; no PR was opened.

## Where it stands

**Refactor plan: about 75% done. Full fw-standards compliance: about 65%.**

| Area | Done | State |
| --- | --- | --- |
| Phase A: overnight drafts | 100% | stick, drive (table correction approved), settings, cal, topology, oracle: merged, each board-benched |
| Phase B: test tooling | 100% | sim fault modes + B5a-e, quick POST evaluator, oracle by input, requirement matrix, stick injection |
| Infrastructure | 100% | ratchet package, guard tools (G2/G4/G10/G11), fw flags really applied, gnu++23 everywhere first-party, bench reset fix |
| `main.cpp` unit (main.cpp + fragments) | ~70% | 3,983 → **1,896** code lines (target ≤1,000, CS-FIL-01) |
| `app_main` | ~40% | 1,007 → **742** lines (target ≤300, CS-LAY-01); task lifts in progress |
| `main/` holds only main.cpp (CS-LAY-01) | ~50% | storage, settings, ota, remote_ui moved (branch, in review); rtps_comms, selftest, log_capture and the 4 `*_ui.cpp` adapters stay by documented deviation until the islands/channels rework |
| Boot-panic bug | done | root cause (picolibc stdio off-by-one in the log tee) proven by A/B on the board; fix merged |
| Hazard fixes C1-C4, seat | ~5% | specs and tooling ready; each needs your approval (CS-SAF-05) |

Other counts in the main unit: mutable globals 143 → 73, function-local statics 40 → 28,
direct prints 6 → 0.

## Components now (V15: by concern)

`hmi_ui` (all screens and views, nav, display glue), `feedback` (haptics, audio), `stick`
(pipeline, config, button edges), `drive_session` + `drive_adapter`, `joystick_cal`, `post`,
`topology` (not wired), plus `fw_core`, `hmi_format`, `hmi_models`, `ota_parse`. On the modules
branch: `storage`, `settings`, `ota`, `remote_ui`.

## Running lanes

| Lane | Next |
| --- | --- |
| K1 agent (paused) | branch `dev_ai_refactor_shrink_k1` f806197 (CI pending, not benched): demo screen deleted (behaviour commit), housekeeping island (components/housekeeping) and UiIsland lifted; `app_main` 742 → 559 on the branch. Next: StickIsland, adapters, RtpsUiBridge, §4 view inits; then bench and merge |
| modules agent (paused) | branch `dev_ai_refactor_modules` c3c7856 (storage, settings, ota, remote_ui moved; reviewer acceptance done). Next: selftest deviation row, merge dev_refactor, final image, bench, merge |
| bench (Sonnet) | graded run of each image before merge |

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

## Waiting for you

1. Approve or adjust the decisions above.
2. Topology TASKS rows (before task_config wiring).
3. POST limits (D4 placeholders in `components/post`).
4. Hazard specs C1-C4 and the seat path (behaviour changes on safety code).
5. MCB team answer (D3: XYTwist timeout, DISABLE semantics).
6. fw-standards branch `ai/cmake-include-order`.

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

### State at the 2026-10-07 evening reboot
- Bench network: the hotspot will not start after the first reboot, so the bench moves to Ethernet.
  `dev_refactor` ec219e4 adds `BENCH_NET=lan:<pc ip>/<prefix>` (B0 skips tethering, B2 expects
  `Network: Ethernet` and `network 0`). Board 2 is on Ethernet (saved setting network 0) and gets
  10.0.0.197 from a 10.0.0.1 router; the PC is on Skynet_AZ 192.168.9.x and reaches 10.0.0.x only
  through Tailscale, which is not a valid bench path (no multicast; unknown peers). Needed: the
  PC's Ethernet port cabled to the same PoE switch (the PC then gets its own 10.0.0.x lease).
- Board: final-a9a040f, no lease held.
- K1 lane: `dev_ai_refactor_shrink_k1`, d192403 green (app_main 237) plus c20e603 (DiagnosticsView
  owns its subjects) and work in progress (UI build into hmi_ui, Set A item 7). Unit-size decision:
  Set A items 7, 4, 6 now; 1 and 3 after the modules merge; 2 and 5 skipped; no option C
  (ratchet frozen list stays). Set A + 1 + 3 lands near 1320 lines; ≤1000 (CS-FIL-01) needs
  Set B (nav/refusal/lock glue behind name-preserving aliases): an owner decision, or a deviation row.

### Resume checklist
1. Modules branch `dev_ai_refactor_modules` bb37381: confirm CI green (bench_inject init_order is CI-only), build the Wi-Fi bench image, Sonnet bench run, merge, ratchet update.
2. K1 branch `dev_ai_refactor_shrink_k1` f806197: confirm CI, bench (G10 task dump, mem.stk_lvgl, self-test IMU/battery), merge. Then pop stash@{0} in C:\w\k1 ("StickIsland step a, unbuilt"), build, check the ADC frames (-fstack-usage: 208 B invoker + 240 B read_twist_mv), and continue: components/control, adapters, RtpsUiBridge, §4 view inits.
3. fw-standards: merge `ai/cmake-include-order` into `dev` and push (P5).
4. Regenerate D3 (housekeeping) after the merges; refresh how-the-firmware-works.md and this report.
5. POST limits characterisation (P3) when the bench has time; boards 1 and 3 aren't on the bench, so propose from board 2 and mark the rest as to-measure.

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

About 6-10 hours of wall clock for the plan's scope (app_main lifts, modules merge, docs refresh),
more if the API session limit stops the agents again.
