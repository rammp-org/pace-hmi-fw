# Overnight report: pace-hmi-fw refactor, 2026-10-06 01:46–07:00 (unattended)

**Status:** done, with open items: `dev_refactor` carries 18 merged changes, 6 board-verified milestones and 7 host test apps (205 cases); six safety and topology drafts are pushed, unmerged, for review. CI is green on the last firmware change (a9a040f).
**Summary:**
- `main.cpp` is split into 27 one-TU fragments (binary-guarded). Four new host-tested components (`fw_core`, `hmi_format`, `hmi_models`, `ota_parse`), plus characterisation suites for the joystick, the self-test table and the RTPS spec. L0 tooling: ratchet, `.clang-tidy`, CI gates, diagrams.
- No safety behaviour changed. Hazards H1–H16, plus new ones found tonight (open-circuit stick = full forward, RTPS decode of NaN and unknown enums, a battery-read data race), are pinned by tests or listed for your decision.
- Board 2 passed B0–B5 at every merged milestone. The board checks are weaker than the plan claimed (see "Not verified"), and a 4.4 V battery read appeared on 3 draft runs out of 20.

**Next:** your review of the drafts (drive_session first), answers to the parked questions below, then the PR from `dev_refactor` to `dev` (not opened).

---

## Done

| # | Step | Commit(s) on `dev_refactor` | Gate that passed |
| --- | --- | --- | --- |
| 0 | Adopt: CLAUDE.md, profile, plan, baseline evidence | c9915f8, a56dc3c | build; host checks |
| 3 | Split `main.cpp` into 27 `frag_*.inc` (one TU) | 2e77bcd (d203f6d, 39cb637) | `split_main.py verify`; `split_guard.py` (only the assert's `__LINE__` load changed); board split-2e77bcd B0–B5 |
| 2 | Host L1 runner (`tests/run.py`, WSL g++ + Unity) and the joystick app | 659c472 | `run.py check` / `run` |
| 5 | `fw_core`: Message, Mailbox, Queue, AtomicValue, ThreadChecker, Owned, `check()`, tokens | 1cab1f8, 7ea7592, 6c84c8c | L1 25 cases + 9 must-not-compile; IDF build; guard (unlinked) |
| 13 | Delete `sample_ui_*` (14 files) | ee8c8a4 | grep evidence; guard (never linked) |
| 1 | Tooling: `fw_standards.cmake` (SYSTEM third-party, esp_libc kept non-SYSTEM), `.clang-tidy`, ratchet, CI (cppcheck c++20 pinned, L0, bench build, host-l1, release-defaults guard, diagrams) | 4f6688a, 1cb497b, 246396c, ad152dd | CI; guard: image unchanged; ratchet |
| 9 | Self-test table → typed `constexpr std::array`; scripts' parser in step | dba0cc1 | L1-SST parity (54 rows); board m3 B3 from the same tree |
| 7 | `hmi_format`: screen formatters out of main | cca9f41 | L1-FMT (goldens linked against LVGL's own printf); board m3 |
| — | Diagrams (`tools/gen_diagrams`, `docs/architecture.md` D1/D3) | da5cabb, bedfd64, 83aa7e6 | selftest 44/44; check |
| — | Vendored copies documented (CS-LAY-05) | 18989a1 | `git apply --check -R` |
| — | `hmi_models`: grid cursor walk, bench PIN entry | 24c5882 | L1-MOD; board m4 |
| — | CI fix: zero cppcheck findings without suppressions | a1c8809 | CI green on the branch |
| — | `ota_parse`: OTA and fwinfo parsers (hostile inputs) | 0231103 | L1-OTA 57 cases; board m5 |
| — | `hmi_format`: About/Update/Internet text helpers | 0a9ad37 | L1-FMT; board m5 |
| — | L1-RTPS: spec helpers, CDR codec, Python parity (tests only) | fbfbdb8 | L1-RTPS 51 cases |
| — | Review fixes: SSID redacted, ratchet lock placement, run.py duplicate case IDs | 6091b6f, 67662c0, 4116fae | selftest; check |
| — | CI fix 2: cppcheck findings from the later merges (one firmware refactor: `std::transform` in `github_ota.cpp`) | a9a040f | CI green on the branch and on `dev_refactor` |
| — | Bench B4b: PIN pad (wrong and right PIN) and the Seat cursor walk on the board | a4a323f | B4b PASS 6/6 on m5 and on the final build |

Ratchet totals, before → after (legacy counts only fall): mutable globals 292 → 271, C-style casts 77 → 72, functions over 60 lines 17 → 16; all others unchanged.

Drafts, pushed and **not merged** (each needs two human approvals):

| Branch | What | Evidence |
| --- | --- | --- |
| `dev_ai_refactor_oracle` | As-is drive-session transition table (41 rows + 11 hold rows), TABLE.md, D4 | self-check 11/11; spot-checked 11 rows against code; open questions U1–U7 |
| `dev_ai_refactor_drive` | `DriveSession` (switch on phase) + oracle test, and the firmware wiring; refreshed onto a9a040f (471438c) | oracle 1,351,680 combinations, 21 cases, 100 % branches (no sanitizers); IDF build of the refreshed draft PASS (0 warnings); bench B5 = m3; table blob unchanged. **Ratchet FAILS on the draft:** `drive_perform` ~114 lines in safety code (limit 60), the table header 915 lines (limit 800), +3 mutable globals. **Bench B3:** 4.4 V battery read in 2 of 5 runs |
| `dev_ai_refactor_stick` | `StickPipeline` (bit-exact against 1,898 golden rows of the old code) + wiring; refreshed onto a9a040f (59f2b9b) | L1 26 cases; IDF build of the refreshed draft PASS (0 warnings); bench B0–B5 PASS (before the refresh); ratchet PASS |
| `dev_ai_refactor_topology` | `topology.hpp` tables, `validate()` 14 rules, `Topology` on fw_core | L1 14 + 18 must-not-compile; not wired |
| `dev_ai_refactor_settings` | settings X-macro → typed table; 51 characterisation cases | IDF build; bench B0–B5 PASS; boot lines byte-identical |
| `dev_ai_refactor_cal` | joystick_cal codec component + run model with a 19-row table | L1 52 cases; IDF build; bench B3 FAIL (vbat, see below); boot lines byte-identical |

## Verified (commands)

| Check | Command | Result |
| --- | --- | --- |
| Host tests | `python tests/run.py check`; `run --all` (IDF venv python) | 7 apps, 205 cases, all PASS |
| Ratchet | `python tools/l0/ratchet.py check` / `selftest` | PASS |
| UI contract | `python scripts/ui_contract.py` | OK, 147 assumptions |
| Diagrams | `python tools/gen_diagrams/gen_diagrams.py check` | PASS |
| Split equivalence | `python tools/split_main.py verify`; `python tools/split_guard.py <ref> <cand>` | PASS at each image-neutral merge |
| IDF builds | `idf.py -B <dir> [-D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wifi.local"] build` | every merged state and every draft: exit 0. Warnings: 0 since the tooling merge (1 third-party `hal` warning before, and in drafts built from older bases) |
| Board | `python tools/bench/run_bench.py --build-dir <dir> --label <l> --flash --tree <tree>` | baseline, split, m2, m3, m4, m5: B0–B5 PASS; final build a9a040f: B0–B5 + B4b PASS (`C:/b/bench/results/final-a9a040f-20261006-042751`). The board runs final-a9a040f |
| CI | `gh run list --branch dev_refactor` | a9a040f (last firmware change): Static analysis 37456062431, L0 37456062371 (incl. host-l1), Build 37456062439, all success. HEAD a4a323f (tools only): Static analysis and L0 success; Build was still running at 04:45. Red earlier tonight: Static analysis from 02:25 (fw_core merge) to 03:3x and again 03:4x–04:2x, both fixed without suppressions |

## Not verified, or weaker than it looks

| Item | Why |
| --- | --- |
| Lock gating motion on the board | B5's "XYTwist zero while locked" can't tell locked from unlocked: the stick is at rest. No stick injection exists. |
| Moved screen texts on the board | B4 grades 6 static screens only. About, Update, Internet, Diagnostics and Joystick are reported, not graded. Settings row labels are masked. Correctness of the moved texts rests on L1 goldens. B4b (added at 04:30) does exercise PIN entry and the Seat cursor walk, but only on the final build and m5. |
| Stack and heap regressions | B3's `floor80` band compares a run at ~90 s uptime with a baseline taken at 500 s. `mem.stk_adc` varied 1,368–2,520 B on the same image. The ADC task's 4 KB stack with ~2.7 KB used is at or below CS-MEM-04's 1.5× margin. No stress test (TS-TGT-03). |
| `split_guard` scope | Compares `main.cpp.obj` content plus ELF section and symbol sizes. Same-size byte changes in other objects would pass. |
| Board flakiness | m3: `time.render_max` 66.9 ms against a 53.7 ms baseline, 1 of 6 runs. FAIL recorded, band not changed. |
| Battery read 4.4 V (`pwr.vbat` SKIP) | 3 of 20 B3 runs, all on draft images (drive 2/5, cal 1/1); 0 of 14 elsewhere. Lead (unproven): `M5StackTab5::get_battery_status()` reads `battery_status_` without `battery_mutex_` while the Data Display task writes it (`components/m5stack-tab5/src/power.cpp:68`), a data race. |
| Banner texts (DRIVE_STOPPED, NOT_GRANTED) | The remote UI reports only screen names. |
| Fragment glue equivalence for `hmi_models` | Rests on an uncommitted scratch harness (3,648 cases) and B4b's 6 board checks. |
| clang-tidy counts | Not run (`idf.py clang-check`); the ratchet has no clang-tidy metric yet. |
| CORE.md loading | Still NOT loaded at 04:10: `hasClaudeMdExternalIncludesApproved` is false for ui_squareline, and the refactor worktree has no entry in `~/.claude.json`. Headless check: "NOT LOADED" in both repos. Agents got the brief and CORE.md by path instead. |

## Decided unattended (revisit)

| Decision | Where |
| --- | --- |
| Split merged before its board run (binary guard), board run after | 2e77bcd |
| The oracle table written by a separate agent; implementer barred from editing it (hash checked) | drive drafts |
| Ratchet fixes during integration: commented fragment includes, brace-init defaults, rule placements (CS-OWN-08 narrowed to fw_core's port, CS-TYP-05 config headers) | 1cb497b, 67662c0 |
| Agents' choices: namespace `hmi::fw`; fw_core channel ends are free functions; host L1 contract; one manifest entry per app; ignored Unity case = FAIL | see branch commits |
| `selftest_spec.h` → `.hpp`; the boot log text keeps the old name (B2 marker) | a1c8809 |
| cppcheck true positives in test fixtures rewritten rather than suppressed (FIX-003 via `strtol`, SST-001 via span) | a1c8809; spec question below |
| Bench calibration: B3 `floor80` for low-water marks, B4 masks on scrolling Settings rows | tools/bench |
| OTA-H1: on an unterminated 32-byte project name the new code reads within the field. Also differs for multibyte UTF-8 names (log and refusal text only, not accept/refuse) | 0231103 |
| uitext date parsing assumes newlib behaviour; the target uses picolibc (10+ digit date parts may differ; unreachable from GitHub dates) | 0a9ad37 |
| Allowlist in both repos' `.claude/settings.local.json`; one line in the shared `pace-hmi-fw/.git/info/exclude` (`sdkconfig.wifi.local`) | remove: see Next |

## Parked questions (yours)

| # | Question | Recommendation |
| --- | --- | --- |
| P1 | The SSID `AlainH` is in pushed commit c9915f8 (no password). Rewrite history? | No: it is only an SSID; rotate the hotspot name if it matters |
| P2 | Drive-session table: U1–U7 (components/drive_session/TABLE.md §10), plus D's two suspected table errors (relock rows omit `nav_menu_on_arrival := false`; `now` sampled after follow-state) | Review the table with a second person before the drive draft |
| P3 | Battery 4.4 V reads on drafts: investigate the `power.cpp:68` race before merging the drive or cal draft? | Yes |
| P4 | `time.render_max` band (±20 %) on a worst-case value: keep, widen, or judge over repetitions? | Judge over 5 repetitions (TS-DET-04) |
| P5 | H1–H16 and the new hazards: which fixes first? | H1 (unlock on an unrequested ENABLED), H2 (POST before motion), H3/STK-010 (open circuit = full deflection), H5/H6 (DISABLE on lock, re-sent) |
| P6 | Topology: `validate()` rejected the plan's own table (SEAT_REQUEST, SELFTEST_REQ drop into safety tasks); the draft uses RAISE_FAULT. Agree? | Yes |
| P7 | Test-only "legacy" copies: may they carry a reasoned cppcheck suppression instead of being rewritten? | Allow reasoned suppressions in `test/legacy_*` only, recorded in the profile |
| P8 | OTA hardening P1–P9 (nesting limit, body cap, field limits, PSA checks) and settings P1–P5 (versioning, per-line parse, report clamps) | Schedule as behaviour-change PRs |

## Spec rules that proved wrong or impractical (proposals; specs repo untouched)

| Rule | Problem tonight | Proposed fix |
| --- | --- | --- |
| AI-SES-01 | An `@` import outside the repo isn't loaded until approved interactively; headless runs and agents never see CORE.md | Vendor the standards as a submodule at `docs/standards` (README "Adopting" step 1) and import that path |
| AI-UNA-02 "never merge" | You authorised merges into an integration branch | Allow merges into an owner-named integration branch (never dev or main) when the run's scope says so |
| AI-UNA-02 / CS-SAF-05 | "Safety changes stay drafts" vs "refactor-only safety extractions with a table oracle" | State that refactor-only safety extractions also need the two approvals before merging (as done tonight) |
| TS-UNIT-01, CS-HAL-04 | The IDF `linux` target is impractical on a Windows bench with no WSL admin rights | Allow host-native L1 with the same Unity sources as a profile option |
| TS-PRI-02 + binary guards | "Scripts decide verdicts" needs a way to accept a known-benign diff (`__LINE__`) | Guards declare their allow-list in code, with a rule ID |
| CS-FMT-02 + never-list | Zero findings plus "no suppression to make it pass" pushes agents to rewrite code to dodge the analyser (fixtures with deliberate UB, verbatim legacy oracles) | Allow a reasoned suppression in test fixtures and legacy oracles, reviewed like a deviation |
| CS-ARC-02 | One `architecture.md` edited by parallel agents conflicts | One diagram file per component, assembled by `gen_diagrams` (done tonight) |
| AI-AGT-03 | Agents can't run cppcheck locally, so "checks pass" didn't include the CI-only analyser; dev_refactor went red twice | Make CI on the source branch a merge gate for unattended runs |

## Next

1. Remove the temporary allowlists: `pace-hmi-fw-refactor/.claude/settings.local.json` and `ui_squareline/.claude/settings.local.json` (plus its `.git/info/exclude` line). Delete the `sdkconfig.wifi.local` line from `pace-hmi-fw/.git/info/exclude` if you don't want it.
2. Review the drafts, drive_session first (table → implementation → bench), then stick, settings, cal, topology. The topology draft still calls fw_core's old member API (`topology_espp.hpp:169-188`, `.writer()` etc.); it needs `fw::writer(ch)` before it can merge. The other drafts don't use it.
3. Answer P1–P8.
4. Open the PR `dev_refactor` → `dev` (CS-GIT-02: one change per PR; split it per step if you prefer).
5. Wire `idf.py clang-check` into the ratchet; add stick injection to the bench (test-only Kconfig) so B5 can see the gate.
