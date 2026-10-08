# Compliance gaps: pace-hmi-fw against fw-standards

Audit of `dev_refactor` at 4d82b9f against fw-standards `dev` (CORE.md, CODING_SPEC.md v0.2, TESTING_SPEC.md v0.2). Read-only: no code changed, no build run. Written 2026-10-08.

## Executive summary

- **Overall: about 49% of the rules met** (146 rules scored: met = 1, partly = 0.5, not met = 0). CODING_SPEC 49% (96 rules), TESTING_SPEC 48% (50 rules). Counted strictly: 26 met, 90 partly, 30 not met, 0 n/a (a rule that is a SHOULD or a [review] item with only sampled evidence is scored from that sample; the table says so).
- progress.md says 'about 65%' for full compliance. This audit says 49%: it scores each of the 146 rules equally from its text, which is a different method, so the two numbers are not a trend. The unmet rules are mostly the islands/channels family (CS-OWN, CS-CON-02, TS-DET-07..09), the L2/L3/system test levels, and CI jobs that do not exist yet.
- Out of scope as asked: fragments and `main.cpp` size (lanes D and S), and hazard fixes C1-C4 (rows marked 'tracked in hazard-fixes.md').
- Ratchet: `python tools/l0/ratchet.py check` passes (190 units, 0 violations); `selftest` passes. The four guard selftests pass (9, 6, 10, 4 cases). `tests/run.py check`: 17 entries, 452 L1 cases, 0 errors. `ui_contract.py`: OK. CI on 4d82b9f: Build, L0 and Static analysis all green.
- Four rules that are tagged [auto] have no CI job at all: CS-FMT-01 (pre-commit), CS-DOC-01 (Doxygen), clang-tidy half of CS-FMT-02, and the coverage gate of CS-SAF-05. Several other [auto] rules (CS-NAM-01, CS-ERR-02, CS-FLW-01) rest on clang-tidy, so they are unchecked too.

### Top 10 gaps, by value over effort

| # | Gap | Rules | Fix | Size | Kind |
| --- | --- | --- | --- | --- | --- |
| 1 | No pre-commit / clang-format job in CI | CS-FMT-01 | Add `pre-commit run --all-files` to `l0.yml`; one reformat commit if it finds drift. | S | infra |
| 2 | Profile is stale and missing deviation rows | CS-DOC-03, CS-LAY-01, CS-FLW-02, CS-SAF-01, CS-NAM-02, CS-LAY-05 | Add 9 `main/` files to the CS-LAY-01 row, the two CS-FLW-02 rows, 9 missing components, safety column, namespace line, vendored-README line. | S | docs |
| 3 | No CODEOWNERS, PR template or recorded branch protection | CS-OWN-13, CS-GIT-04, CS-GIT-05, TS-DET-05 | Add `CODEOWNERS` (topology, tables, goldens) and `.github/pull_request_template.md`; record protection in the profile. | S | owner decision (handles, protection) |
| 4 | 11 L1-tested requirements are not cited (CAL-01..07, SET-01..04) | TS-COV-01, CS-SAF-01 | Cite the REQ IDs in the existing cases: 97 to 108 of 137 covered. Then make an uncovered safety requirement fail L0. | S | test |
| 5 | No coverage gate | TS-COV-02, CS-SAF-05 | Wire `gcov_summary.py`/gcovr: 100% branches for the table components, 80/70 for the rest. | S-M | infra |
| 6 | clang-tidy / clang-check never runs | CS-FMT-02, CS-NAM-01, CS-ERR-02, CS-FLW-01 | Add an `idf.py clang-check` job, opt in per component (safety first), ratchet the rest. Expect a backlog. | M-L | infra |
| 7 | No Doxygen job | CS-DOC-01 | Add Doxygen with `WARN_IF_UNDOCUMENTED` + `WARN_AS_ERROR`; baseline legacy headers. | M | infra |
| 8 | Small cleanups in one PR | CS-LAY-06, CS-TYP-03/04, CS-SAF-07, CS-LAY-10, CS-LAY-03 | Pin the vendored BSP's espp deps; `actions_spec.h` X-macro to a `constexpr` array; scope cppcheck category suppressions away from safety code; delete `#if 0`; add a `src/`-include grep. | S each | refactor / infra |
| 9 | No allocation guard | TS-DET-08, CS-MEM-02, CS-SAF-04 | Host `--wrap` guard on the L1 apps first, chip hook later. Shows where rtps_comms allocates. | S then M | test |
| 10 | selftest.cpp rework | CS-FIL-01, CS-UI-02, CS-UI-06, CS-CON-01, CS-MEM-01, CS-LOG-01, CS-OWN-08 | Largest single block of legacy: 1030 lines, 136 `lv_*` calls, 30 locks, 11 raw allocations, 6 `vTaskDelay`, 1 `xTaskCreate`. Rework as a table-driven POST/self test on the UI-island request path. | L | refactor (owner scope call) |

### Needs an owner decision

| # | Decision | Opens |
| --- | --- | --- |
| 1 | Topology: review the TASKS rows and wire `task_config` (progress.md P2 said 'decide later') | CS-CON-02, CS-OWN-01..07, -11, -12, TS-DET-07, TS-DET-09: about 12 rules, none movable without it |
| 2 | CODEOWNERS handles, branch protection on main/dev, a second safety reviewer | CS-OWN-13, CS-GIT-05, CS-SAF-05 |
| 3 | selftest: rework (L) or keep as a wider deviation. The CS-LAY-01 row covers only the folder rule, not CS-FIL-01, CS-UI-02, CS-CON-01, CS-MEM-01 | selftest.cpp legacy counts |
| 4 | `components/ui/generated/` move (M, touches `import_ui.ps1`) or a deviation row | CS-LAY-04 |
| 5 | Behaviour changes found while auditing: version `settings.txt` with migration (CS-CFG-02), bound the OTA release count (CS-FLW-02), drop the `assert`s in the vendored joystick (CS-ERR-04, with C2), adopt `check()` in stick (CS-FLW-03) | each needs approval; CS-SAF-05 applies to the safety ones |
| 6 | POST limits and a stress test on a board; a switchable supply for the nightly boot loop (P3) | TS-POST-*, TS-TGT-03, CS-MEM-04 |
| 7 | Release flow: `version.txt`, report, sign-off | TS-REL-01..03 |
| 8 | clang-check scope: accept a findings backlog and start safety-first | CS-FMT-02 and its three dependants |

## Ratchet: what is left to fall to zero

Source: `tools/l0/baseline.json` plus the per-line grants under `transfers` (debt moved verbatim into components; they cannot be edited, only cleaned). Counts are as of 4d82b9f. `main/main.cpp` is the unit of main.cpp plus all `frag_*.inc`.

| Category (rule) | Legacy in baseline | Granted in components | Total | Where |
| --- | --- | --- | --- | --- |
| lines over limit (CS-FIL-01) | 2502 | 0 | 2502 | main.cpp 1472, selftest.cpp 1030 |
| functions over 120 (CS-FIL-01) | 1 | 0 | 1 | main.cpp 1 |
| functions over 60 (SHOULD) | 8 | 0 | 8 | rtps_comms.cpp 4, selftest.cpp 2, joystick/src/joystick.cpp 1, main.cpp 1 |
| app_main over 300 (CS-LAY-01) | 0 | 0 | 0 | none |
| function-local statics (CS-CMP-03) | 12 | 3 | 15 | main.cpp 7, rtps_comms.cpp 5; granted: remote_ui/remote_ui.cpp 2, ota/github_ota.cpp 1 |
| mutable globals (CS-CMP-03) | 146 | 13 | 159 | main.cpp 51, rtps_comms.cpp 32, selftest.cpp 25, joystick_cal.cpp 15, log_capture.cpp 8, hmi_rtps_spec.hpp 5, internet_ui.cpp 4, update_ui.cpp 4, about_ui.cpp 1, log_view.cpp 1; granted: remote_ui/remote_ui.cpp 6, ota/github_ota.cpp 3, ota/fw_info.cpp 2, settings/settings.cpp 2 |
| mutexes/semaphores (CS-OWN-08) | 110 | 56 | 166 | rtps_comms.cpp 36, selftest.cpp 30, main.cpp 19, joystick_cal.cpp 11, log_capture.cpp 7, internet_ui.cpp 2, update_ui.cpp 2, internet_ui.hpp 1, selftest.hpp 1, update_ui.hpp 1; granted: feedback/da7280.hpp 18, ota/github_ota.cpp 11, settings/settings.cpp 11, remote_ui/remote_ui.cpp 10, ota/fw_info.cpp 5, remote_ui/remote_ui.hpp 1 |
| `lv_*` outside UI paths (CS-UI-02) | 149 | 0 | 149 | selftest.cpp 136, lv_mem_psram.c 13 |
| `#if CONFIG_` (CS-TYP-05) | 0 | 1 | 1 | granted: remote_ui/remote_ui.cpp 1 |
| `std::thread` (CS-CON-01) | 2 | 3 | 5 | internet_ui.cpp 1, update_ui.cpp 1; granted: ota/fw_info.cpp 1, ota/github_ota.cpp 1, remote_ui/remote_ui.cpp 1 |
| `xTaskCreate` (CS-CON-01) | 1 | 0 | 1 | selftest.cpp 1 |
| `vTaskDelay` (CS-CON-01) | 6 | 0 | 6 | selftest.cpp 6 |
| `sleep_for` (CS-CON-01) | 6 | 17 | 23 | rtps_comms.cpp 5, main.cpp 1; granted: feedback/da7280_bench.cpp 11, remote_ui/remote_ui.cpp 5, feedback/haptics.cpp 1 |
| `esp_timer_create` (CS-CON-01) | 0 | 0 | 0 | none |
| direct log/print (CS-LOG-01) | 5 | 5 | 10 | selftest.cpp 5; granted: remote_ui/remote_ui.cpp 5 |
| raw memory (CS-MEM-01) | 19 | 7 | 26 | selftest.cpp 11, lv_mem_psram.c 5, log_capture.cpp 1, log_view.cpp 1, rtps_comms.cpp 1; granted: ota/github_ota.cpp 3, remote_ui/remote_ui.cpp 2, hmi_ui/display_flip.cpp 1, ota/fw_info.cpp 1 |
| `typedef` (CS-NAM-01) | 7 | 0 | 7 | hmi_rtps_spec.hpp 5, joystick/include/joystick.hpp 2 |
| `assert` (CS-ERR-04) | 18 | 0 | 18 | joystick/src/joystick.cpp 17, main.cpp 1 |
| `ESP_ERROR_CHECK` (CS-ERR-04) | 0 | 0 | 0 | none |
| C-style casts (CS-TYP-02) | 29 | 39 | 68 | hmi_rtps_spec.hpp 28, lv_mem_psram.c 1; granted: feedback/da7280.hpp 39 |

Counting everything except `lines`: 519 legacy hits in the baseline plus 144 granted lines. The `lv_outside_ui` metric excludes the frozen UI file list, so it understates the number of main/ files that call LVGL.

Per file (legacy baseline only):

| File | Counts |
| --- | --- |
| `main/main.cpp` | lines 1472, mutable_globals 51, locks 19, static_state 7, assert 1, fn_over_120 1, fn_over_60 1, sleep_this_thread 1 |
| `main/selftest.cpp` | lines 1030, lv_outside_ui 136, locks 30, mutable_globals 25, heap_raw 11, vtaskdelay 6, log_direct 5, fn_over_60 2, xtaskcreate 1 |
| `main/rtps_comms.cpp` | locks 36, mutable_globals 32, sleep_this_thread 5, static_state 5, fn_over_60 4, heap_raw 1 |
| `main/hmi_rtps_spec.hpp` | c_cast 28, mutable_globals 5, typedef 5 |
| `main/joystick_cal.cpp` | mutable_globals 15, locks 11 |
| `main/lv_mem_psram.c` | lv_outside_ui 13, heap_raw 5, c_cast 1 |
| `components/joystick/src/joystick.cpp` | assert 17, fn_over_60 1 |
| `main/log_capture.cpp` | mutable_globals 8, locks 7, heap_raw 1 |
| `main/internet_ui.cpp` | mutable_globals 4, locks 2, std_thread 1 |
| `main/update_ui.cpp` | mutable_globals 4, locks 2, std_thread 1 |
| `main/log_view.cpp` | heap_raw 1, mutable_globals 1 |
| `components/joystick/include/joystick.hpp` | typedef 2 |
| `main/internet_ui.hpp` | locks 1 |
| `main/selftest.hpp` | locks 1 |
| `main/update_ui.hpp` | locks 1 |
| `main/about_ui.cpp` | mutable_globals 1 |

What the ratchet does not measure: function-like macros (CS-TYP-04), `default:` in switches (CS-TYP-06), `new` in headers that are not scanned (tests are excluded), unused return values of IDF calls (CS-ERR-02), recursion, `Doxygen` (CS-DOC-01), namespaces (CS-NAM-02), and the `src/`-include rule (CS-LAY-03).

## Documented deviations: do they still hold?

| Row (project-profile.md) | Still holds? | Note |
| --- | --- | --- |
| TS-UNIT-01, CS-HAL-04 (L1 host-native g++ in WSL) | yes | CI `host-l1` runs the same apps on the IDF image; 17 entries, 452 cases. Approved Q6. |
| CS-LAY (layout), `main/frag_*.inc` | yes, temporary | 26 fragments remain; lanes D/S dissolve them. |
| CS-LAY-01, rtps_comms, log_capture, selftest.*, selftest_spec.hpp, about/internet/update_ui, log_view | yes, but incomplete | All listed files still exist and the reasons hold (rtps_comms: 36 locks, 32 globals, 4 functions over 60, safety). Not listed but present in `main/`: `selftest_platform.{cpp,hpp}`, `joystick_cal.{cpp,hpp}`, `hmi_rtps_spec.hpp`, `rtps_comms.hpp`, `log_capture.hpp`, `actions_spec.h`, `lv_mem_psram.c`, `boot_logo.*`, `click.wav`. Also the row covers only the folder rule; selftest also breaks CS-FIL-01 and CS-UI-02 with no row. Text says 'four `*_ui.cpp`' and lists three; progress.md says four. |
| AI-UNA-02 'never merge' on `dev_refactor` | expired | Authorised for the 2026-10-06 run (per run). Later merges rely on the owner's delegation in progress.md; record it. |
| CS-SAF-05 two approvals | yes | Still one approver; nothing safety-relevant merges to `dev`. |
| CS-SAF-03 open circuit (joystick low rail) | yes | Residual hazard in hazard-fixes.md; needs an EE change. |
| CS-LNG-02 `main` keeps IDF warnings | yes, open | `main` has `-std=gnu++23` only. |
| Missing rows | to add | CS-FLW-02 (`ota_parse/src/fw_record.cpp:11`, `release_list.cpp:68`: spec-deviation comments without a profile entry); CS-LAY-04 (`components/ui/` layout) if kept; CS-TYP-03/04 (`actions_spec.h`) if kept. |

## CORE.md checkable items

Derived from the CS/TS rows below; not scored again.

| CORE item | Status | From |
| --- | --- | --- |
| 1 No recursion, goto, exceptions | met by grep; CI relies on clang-tidy that does not run | CS-FLW-01, CS-LNG-03 |
| 2 Loops bounded, waits have timeouts | partly | CS-FLW-02 (two undeclared deviations, sleep polling) |
| 3 Safety islands never allocate after start-up | not met | CS-SAF-04, TS-DET-08 |
| 4 Functions at most 120 (60 in safety) | partly | CS-FIL-01 (1 over 120 in main; rtps_comms 4 over 60) |
| 5 Recovering checks in safety code | partly | CS-ERR-04, CS-FLW-03 (hazards tracked) |
| 6 One owner per state, channels only | not met | CS-OWN-* |
| 7 Every return value checked | partly | CS-ERR-02 (`[[nodiscard]]` yes; IDF half unchecked) |
| 8 Preprocessor for configuration only | partly | CS-TYP-04/05, CS-LAY-09 (3 function-like macros) |
| 9 No raw memory, chrono, units in types | partly | CS-MEM-01 (26), CS-TYP-01/02 |
| 10 Zero findings; no suppressions in safety | partly | CS-FMT-02, CS-SAF-07 (clang-tidy not run) |
| Safety: no motion before POST and link | tracked | hazard-fixes.md C3 |
| Topology header pure C++ | met | `components/topology/include/topology.hpp`; mnc tests |

## Full table

Status: met / partly / not met. Basis: rules tagged [auto] are judged from what CI or the ratchet enforces; [review] rules from grep and sampling (the evidence says which). Kind: refactor, behaviour change (needs owner approval; safety ones need two approvals), infra (CI/tooling), docs, test, owner decision, tracked (hazard-fixes.md). Size: S under half a day, M about a day, L several days.

| Rule | Status | Evidence | Smallest fix | Size | Kind |
| --- | --- | --- | --- | --- | --- |
| CS-LNG-01 | met | Profile pins IDF v6.0; `IDF_VERSION: v6.0` in all workflows; `dependencies.lock` committed. |  |  |  |
| CS-LNG-02 | partly | `fw_component_options` is called by 16 of the 17 first-party components (`control` is missing; `topology` is not built by CMake yet), and `hmi_check_fw_options()` fails the configure if one is missing. `main` gets only `-std=gnu++23`, not the warning set (open profile deviation). `components/control` (header-only) has no call. | Give `main` the warning set once the legacy counts are down. Add the call to `control`, or a deviation row for header-only. | M | refactor |
| CS-LNG-03 | met | grep: no `try`/`catch`/`throw`/`dynamic_cast`/`typeid` in first-party code; IDF defaults. |  |  |  |
| CS-LNG-04 | met | C only in `main/boot_logo.c` (generated) and `main/lv_mem_psram.c` (LVGL allocator shim). |  |  |  |
| CS-LAY-01 | partly | `app_main` is 240 lines (ratchet `app_main_lines` empty). `main/` still holds 20+ non-fragment files. The profile row covers rtps_comms, log_capture, selftest.{cpp,hpp}, selftest_spec.hpp, the 3 `*_ui.cpp`, log_view. Not in any row: `selftest_platform.{cpp,hpp}`, `joystick_cal.{cpp,hpp}`, `hmi_rtps_spec.hpp`, `rtps_comms.hpp`, `log_capture.hpp`, `actions_spec.h`, `lv_mem_psram.c`, `boot_logo.*`, `click.wav`. Fragments: lanes D/S. | Add the missing files to the deviation row (S, docs). Move the `main/joystick_cal.cpp` view into `components/joystick_cal` (M; it carries 11 locks and 15 globals). Move the spec headers and assets into components. | S-M | refactor |
| CS-LAY-02 | partly | Components are by concern (progress.md V15). `hmi_ui` holds every view (the one-sentence test is strained). No driver has an `example/` folder (feedback, joystick). | Add `example/` for the drivers, or record that this SHOULD is not taken. Split `hmi_ui` if needed. | S | refactor |
| CS-LAY-03 | met | grep: no `#include` of another component's `src/` or `../`; dependencies are in `REQUIRES`/`PRIV_REQUIRES`. No CI check enforces it. | Add a short grep to `tools/l0` so it stays true. | S | infra |
| CS-LAY-04 | partly | The SquareLine export sits at `components/ui/` root with public `INCLUDE_DIRS "."`, not in a `generated/` folder behind `PRIV_INCLUDE_DIRS`. It is never hand-edited (import_ui.ps1 mirrors it). | Move into `components/ui/generated/` (change `import_ui.ps1` and the includes in hmi_ui/main), or write a deviation row. | M | refactor |
| CS-LAY-05 | met | `components/joystick` (README + upstream.diff) and `components/m5stack-tab5` (VENDORED.md + upstream.diff); both are in `ExcludeHeaderFilterRegex`. Profile line 112 ('neither has a README') is stale. | Fix the stale profile sentence. | S | docs |
| CS-LAY-06 | partly | `main/idf_component.yml` pins every `espp/*` to `==1.3.2`. `components/m5stack-tab5/idf_component.yml` uses `>=1.0` for 7 espp packages. | Pin the vendored BSP's espp dependencies to `==1.3.2` (note it in VENDORED.md). | S | infra |
| CS-LAY-07 | partly | Profile: 'espp alternatives considered: not recorded' for LVGL, w5500, esp_wifi_remote, esp_hosted, cjson, esp-dsp, littlefs. | One line per non-espp dependency in the profile. | S | docs |
| CS-LAY-08 | met | `sdkconfig.defaults` is 341 lines with 236 comment lines; `sdkconfig` is not tracked. |  |  |  |
| CS-LAY-09 | met | Debug options default off; `l0.yml` greps `sdkconfig.defaults`; `build.yml` checks the release ELF for stick-injection symbols; default, bench and bench_inject are built in CI. The debug-FPS variant is not built (its code is in `if constexpr` arms, always compiled). |  |  |  |
| CS-LAY-10 | partly | `#if 0` at `main/hmi_rtps_spec.hpp:246`. No sweep for unused files or dependencies done. | Delete the block; one sweep after lanes D/S land. | S | refactor |
| CS-FIL-01 | partly | Ratchet-enforced. Over the limit: `main.cpp` unit 1472 code lines (lanes D/S) and `selftest.cpp` 1030. `fn_over_120`: 1 (main). `fn_over_60` (SHOULD): 8 (joystick 1, main 1, rtps_comms 4, selftest 2). Safety components with the 60-line tidy: drive_adapter, drive_session, joystick_cal, post, stick. `fw_core`, `control` and `rtps_comms` (safety) do not have it. | Split `selftest.cpp` (L, with its rework). Add the 60-line `.clang-tidy` to fw_core and control (S). Split the 4 rtps_comms functions in its rework. | M-L | refactor |
| CS-FIL-02 | partly | Sampled: `hmi::` components use `.hpp/.cpp`, snake_case, `#pragma once`. Fragments (`.inc`) and `actions_spec.h` break it. | Falls out of lanes D/S; fold `actions_spec.h` into a component. | S | refactor |
| CS-NAM-01 | partly | `.clang-tidy` carries the espp naming table, but clang-tidy is not run in CI (see CS-FMT-02), so it is unchecked. Known breaks: `typedef` 7 (ratchet). | Run clang-tidy in CI, then fix findings. | M | infra |
| CS-NAM-02 | partly | Components use `hmi::<component>` (about 150 blocks). `main/*` and the fragments are global. Profile line 'Namespace: none yet' is stale. | Put main-side code into `hmi::` as it moves; update the profile line. | S | docs |
| CS-CMP-01 | partly | 39 headers have `struct Config`; `BaseComponent` is used in feedback, housekeeping, joystick. Not applied to stick, post, settings (free functions and tables). | Review per stateful class. | S | refactor |
| CS-CMP-02 | partly | New components take their dependencies in `Config`/ports (StickIsland, UiIsland, drive_adapter). `main` is full of globals (146 mutable globals in total) and a global recursive `lvgl_mutex`. | Falls with the fragment moves and the islands/channels rework. | L | refactor |
| CS-CMP-03 | partly | Ratchet: `mutable_globals` 146 legacy + 13 granted; `static_state` 12 + 3 granted. Enforced as 'may only fall'. | See the ratchet table; target 0. | L | refactor |
| CS-CMP-04 | partly | `initialize(std::error_code &)` only in `feedback/da7280.hpp`. Tasks in main/rtps_comms are never stopped or joined. | Add `initialize()` and joining destructors to the islands as they are lifted. | M | refactor |
| CS-ERR-01 | partly | New components return `bool` + `error_code` or a `DecodeError` enum (owner-approved, progress.md P1). `esp_err_t` is in no first-party header. Legacy main/ code returns mixed types. | Convert as code moves. | M | refactor |
| CS-ERR-02 | partly | `[[nodiscard]]`: 176 uses in component headers; `ESP_ERROR_CHECK` 0 (ratchet). `bugprone-unused-return-value` for IDF calls is in `.clang-tidy` but never run (no clang-tidy in CI). | Same fix as CS-FMT-02. | M | infra |
| CS-ERR-03 | partly | Not auditable by grep; sampled new components follow it. | Review item for the rtps_comms rework. | M | refactor |
| CS-ERR-04 | partly | `assert`: 18 legacy (joystick 17, main 1; ratchet). `ESP_ERROR_CHECK` 0. `abort` only in `fw_core/thread_checker.hpp`. Drive_session goes to a safe phase on corrupt input (DRV-016). Hazards C1-C4: tracked in hazard-fixes.md. | Replace the `assert`s in the vendored joystick when C2 touches it. | M | behaviour change |
| CS-FLW-01 | partly | `misc-no-recursion` is configured; grep finds no `goto`/`setjmp`/`longjmp` in non-test code. Not run in CI (no clang-tidy), so only grep. | Covered by the clang-tidy job. | S | infra |
| CS-FLW-02 | partly | Two `spec-deviation(CS-FLW-02)` comments (`ota_parse/src/fw_record.cpp:11`, `release_list.cpp:68`) have no profile entry. `sleep_this_thread` 6 + 17 granted; rtps_comms and remote_ui poll with sleeps. | Add the two profile rows (S, docs). Replace sleeps with `cv.wait_for`; bound the release count (OTA behaviour). | S-M | docs |
| CS-FLW-03 | partly | `fw_core::check()` exists and is tested (FWC-L1). Not used outside drive_session/post; stick and rtps_comms use plain returns. | Adopt `check()` in stick and the rtps_comms rework. | M | behaviour change |
| CS-LOG-01 | partly | Ratchet `log_direct`: 5 legacy (selftest) + 5 granted (`remote_ui.cpp` ESP_LOGE/W/I, lines 556-586). Direct prints in main: 0. 26 files use `espp::Logger`. | Move selftest and remote_ui to Logger (S each; remote_ui is debug-only). | S | refactor |
| CS-LOG-02 | partly | Few project enums have `to_string()` + `fmt::formatter`; not audited across the board. | Add as each enum is logged. | S | refactor |
| CS-LOG-03 | partly | No periodic steady-state logging seen in new components; rtps_comms and selftest not read line by line. | Review in the rtps_comms rework. | S | refactor |
| CS-CON-01 | partly | Ratchet: `std_thread` 2 + 3 granted (ota 2, internet_ui, update_ui, remote_ui), `xtaskcreate` 1 and `vtaskdelay` 6 (selftest), `esp_timer_create` 0, `sleep_this_thread` 6 + 17. New islands use `espp::Task`. | Move the ota/internet/update_ui workers and selftest to `espp::Task`. | M | refactor |
| CS-CON-02 | not met | `task_config(...)` is used only in topology and its tests; no task takes its config from `TASKS`. Owner decision P2: keep literal TaskConfigs, no topology wiring for now. `ui_island.cpp:34` and `housekeeping.cpp:26` set `.task_config` from a literal. | Wire `topo.task_config(TaskId{Task::X})` into each island. Needs the TASKS rows reviewed (CONTROL prio/core/stack = C4/H11). | M | owner decision |
| CS-CON-03 | partly | `StickIsland` uses `cv.wait_for` (`stick_island.hpp:88`). rtps_comms, selftest, remote_ui poll with sleeps. | With the CS-FLW-02 sleep fixes. | M | refactor |
| CS-CON-04 | partly | Not audited ISR by ISR; first-party ISR code is limited to fw_core's ISR calls (host-tested only; chip half missing). | L3 test of the ISR calls. | S | test |
| CS-CON-05 | partly | `now_fn`/`steady_clock` in 2 headers only (stick_island, bench_inject). Drive_session takes time in its Env. Loop-count timing not grepped in main. | Inject a clock into drive_adapter, post, housekeeping, rtps_comms. | M | refactor |
| CS-OWN-01 | not met | No islands as defined (topology not wired). `StickIsland`, `UiIsland`, `Housekeeping` are islands in code, but the state in `main` (51 globals in the unit) and `rtps_comms` (32 globals, 36 locks) is not owned by one. | Islands/channels rework (rtps_comms first). Needs the owner's TASKS decision. | L | owner decision |
| CS-OWN-02 | partly | Adapter pattern in `drive_adapter` and the `*_ui.cpp`; `rtps_comms` still decides (gate, link). | With the islands rework. | L | refactor |
| CS-OWN-03 | not met | `fw_core` has `Mailbox`/`Queue`/`AtomicValue`, tested at L1, but no firmware code uses them (profile: 'not used by the firmware yet'). Data crosses tasks through globals, `std::atomic`s and `lvgl_mutex`. | Instantiate topology storage, then convert one channel at a time (the hazard C-lanes do the safety ones). | L | owner decision |
| CS-OWN-04 | not met | No runtime queue depths or full policies exist (`CHANNELS` is a draft). | Follows CS-OWN-03. | L | owner decision |
| CS-OWN-05 | partly | `Message` concept, size `static_assert` and must-not-compile cases in fw_core/topology (FWC-024 + 3 mnc). Applied to no live channel. | Follows CS-OWN-03. | M | refactor |
| CS-OWN-06 | not met | No `Writer<>`/`Sender<>` handles in any Config. | Follows CS-OWN-03. | L | refactor |
| CS-OWN-07 | not met | Context tokens are defined in fw_core (tested). No live entry point takes one. | Adopt in `UiIsland` and `StickIsland` first. | M | refactor |
| CS-OWN-08 | partly | Ratchet `locks`: 110 legacy + 56 granted. Allowed only in fw_core. Biggest: rtps_comms 36, selftest 30, main 19, joystick_cal 11, settings 11 and ota 16 (granted). | Falls with CS-OWN-03; `lvgl_mutex` goes when everything sits behind the UI island. | L | refactor |
| CS-OWN-09 | partly | New islands document their loop; rtps_comms callbacks do not document their task. | Document callback tasks in rtps_comms. | S | docs |
| CS-OWN-10 | met | `components/fw_core` holds Message, Mailbox, Queue, context, ThreadChecker, Owned, check(); tests FWC-L1 on the host. The L3 half is not written (REQ-FWC-13 uncovered). | Write the L3 app (needs a board). | M | test |
| CS-OWN-11 | not met | `CONFIG_<PROJECT>_OWNERSHIP_CHECKS` not implemented (profile). ThreadChecker sits at no live thread boundary. | Place it in trampolines/readers once topology is wired. | M | refactor |
| CS-OWN-12 | partly | `topology.hpp` (219 lines), validate, `topology_espp.hpp` and must-not-compile tests exist and run in host L1. It is a draft, not wired; TASKS rows not reviewed (owner P2). | Owner review of the rows, then wiring. | M | owner decision |
| CS-OWN-13 | not met | No `CODEOWNERS` file in the repo. | Add `CODEOWNERS` (topology.hpp, drive_session_table.hpp, post checks, stick tables). Needs the owner's handles. | S | owner decision |
| CS-MEM-01 | partly | Ratchet `heap_raw`: 19 legacy + 7 granted (selftest 11, lv_mem_psram.c 5 by design, rtps_comms/log_capture/log_view 1 each; ota 3 and others granted). | Selftest rework; allowlist the LVGL allocator file with a deviation row. | M | refactor |
| CS-MEM-02 | partly | Not checked per path; TS-DET-08 guard missing. rtps_comms likely allocates after start-up (strings/vectors), not measured. | Guard first (TS-DET-08), then fix what it shows. | M | test |
| CS-MEM-03 | partly | New components take `std::span`/`string_view` (sampled hmi_format, ota_parse). Legacy raw pointer+length in rtps_comms/selftest. | With the rework. | M | refactor |
| CS-MEM-04 | not met | No stress test run; profile: 'CS-MEM-04 margins unknown'. Free stack after a self test: ADC 1496 B, RTPS 6112 B; ADC as low as ~1.35 KB (progress.md). | Run TS-TGT-03 on a board and record the 1.5x margins. ADC stack size is C4. | M | owner decision |
| CS-TYP-01 | partly | `std::chrono` in new components; raw `_ms` integers remain in main/rtps_comms/selftest. | With the rework. | M | refactor |
| CS-TYP-02 | partly | Ratchet `c_cast`: 29 legacy + 39 granted (hmi_rtps_spec.hpp 28, da7280.hpp 39 granted, lv_mem_psram 1). | Fix the hmi_rtps_spec.hpp casts (S-M) and the da7280 casts (M). | M | refactor |
| CS-TYP-03 | partly | Spec tables are typed in drive_session, post, settings, topology with `static_assert`s. `main/actions_spec.h` is an X-macro (`ACTIONS_TABLE(X)`). | Convert `actions_spec.h` to a `constexpr std::array` with a test. | S | refactor |
| CS-TYP-04 | partly | Function-like macros: `actions_spec.h` (2), `frag_actions.inc` (1). The ratchet does not check this rule. | Same fix as CS-TYP-03; add a ratchet metric for function-like `#define`. | S | refactor |
| CS-TYP-05 | met | Ratchet `if_config` is 0 legacy (one granted line in `remote_ui.cpp`); config headers follow the pattern (`fw_core/config.hpp`, `feedback/config.hpp`). | Resolve the granted line with the remote_ui rework. | S | refactor |
| CS-TYP-06 | partly | About 40 `default:` labels in first-party code. Safety code (drive_session, drive_adapter, post) uses a safe-state default, as allowed. Non-safety defaults in hmi_ui (9), ota, settings_applied, feedback, main. `-Wswitch-enum` is not on for safety code. | Drop the non-safety defaults (fallback after the switch); enable `-Wswitch-enum` in safety components. | M | refactor |
| CS-UI-01 | met | Export mirrored whole by `import_ui.ps1`; `scripts/ui_contract.py` passes (147 assumptions, 327 objects). | Folder name: see CS-LAY-04. |  |  |
| CS-UI-02 | partly | Ratchet `lv_outside_ui`: 149 legacy (selftest 136, lv_mem_psram 13). Frozen UI files still in main/ (3 `*_ui.cpp`, log_view, joystick_cal). `lvgl_mutex` is used by 10 main files; remote_ui is on the UI-path list. | Selftest overlay through a UI-island request (CS-UI-06); move the frozen UI files into hmi_ui. | L | refactor |
| CS-UI-03 | partly | Models in `hmi_models`, `hmi_format`, `joystick_cal`, `drive_session` (L1). Screen logic remains in fragments (nav, lock, hold, refusal) and `main/*_ui.cpp`. | Lanes D/S plus the *_ui adapters. | M | refactor |
| CS-UI-04 | partly | `hmi_ui` has one view per concern with a shared trampoline (sampled). Fragments hold the rest. | Lanes D/S. | M | refactor |
| CS-UI-05 | partly | Views own their subjects in hmi_ui (e.g. DiagnosticsView). Global subjects remain in `frag_state.inc` (mib_state, rtps_link, locked), `frag_refusal.inc`, `frag_seat.inc`, `main/joystick_cal.cpp`. | Lanes D/S plus the joystick_cal view move. | M | refactor |
| CS-UI-06 | not met | The self-test overlay (`selftest.cpp`, 136 `lv_*` calls) and the remote UI call LVGL from non-UI tasks under `lvgl_mutex`. | Request-to-UI-island design for both; remote_ui first (debug-only). | L | refactor |
| CS-UI-07 | met | `scripts/ui_contract.py` runs in `l0.yml`: OK. |  |  |  |
| CS-HAL-01 | partly | Fakes in test trees (stick, drive_adapter ports, joystick_cal fake_lvgl). main/rtps_comms/selftest touch hardware directly. | With the rework. | M | refactor |
| CS-HAL-02 | partly | feedback/da7280 and the vendored BSP take bus functions through Config; others are direct. Sampled only. | Low value; with the rework. | S | refactor |
| CS-HAL-03 | partly | See CS-CON-05: a clock is injected in 2 headers; others read `steady_clock` or `esp_timer` directly. | Inject in drive_adapter, post, housekeeping. | M | refactor |
| CS-HAL-04 | partly | Documented deviation: L1 apps run host-native g++ 13 in WSL, not the IDF `linux` target (approved Q6). 17 entries / 452 cases run in `l0.yml` job `host-l1`. The deviation row still holds. | Keep the row; revisit if the IDF linux target becomes installable. | M | infra |
| CS-CFG-01 | partly | Settings table has default, range and unit (`settings_spec.hpp`). Wi-Fi/network settings use text files, not espp Nvs. | Low priority. | S | refactor |
| CS-CFG-02 | partly | `joystick_cal.txt` is `version 1`; `settings.txt`, `fwinfo.txt`, `wifi.txt` are unversioned (profile). | Add a version line to settings.txt with migration (changes stored data). | M | behaviour change |
| CS-CFG-03 | partly | The profile lists the interfaces; none is versioned except the cal file. | Version remote_ui (TCP 3333), the self-test report lines and settings.txt. | M | docs |
| CS-DOC-01 | not met | No Doxygen run in any workflow (l0.yml, build.yml, static_analysis.yml). 87 of 99 component headers carry some `@brief`. | Add Doxygen with `WARN_IF_UNDOCUMENTED` and `WARN_AS_ERROR`, a ratchet-style baseline for legacy headers, then fix. | M | infra |
| CS-DOC-02 | partly | all 20 non-generated components have a README; REQ IDs in 15 of them; hardware findings with dates are rare. | Add REQ sections and D4s where missing. | S | docs |
| CS-DOC-03 | partly | README.md, CLAUDE.md, `docs/architecture.md`, `docs/project-profile.md` exist. `docs/how-it-works.md` is named `how-the-firmware-works.md`. The profile component table lists 12 of 21 components (missing control, drive_adapter, drive_session, housekeeping, joystick_cal, ota_parse, remote_ui, stick, topology). | Rename the file; refresh the profile component table and safety column. | S | docs |
| CS-ARC-01 | partly | `docs/architecture.md` and `docs/diagrams/project_components.json` exist. D4 is not complete for every safety component (stick, drive_adapter). | Add D4 for stick and drive_adapter. | S | docs |
| CS-ARC-02 | met | `l0.yml` 'Diagrams' step regenerates and diffs the generated parts. | D3 needs regenerating after the module merges (progress.md). | S | docs |
| CS-ARC-03 | partly | Legend and snippets not checked diagram by diagram. | Review once. | S | docs |
| CS-FMT-01 | not met | `.clang-format` and `.pre-commit-config.yaml` (clang-format v14.0.6) exist. No workflow runs `pre-commit run --all-files` (grep of `.github/`). | Add a pre-commit job to `l0.yml`. The first run may reformat files (one commit). | S | infra |
| CS-FMT-02 | partly | cppcheck runs in `static_analysis.yml` (green on 4d82b9f) at `--std=c++20`, with category suppressions in `suppressions.txt` (unusedFunction, unusedStructMember, missingInclude); the action's cppcheck is unpinned. clang-tidy (`idf.py clang-check`) is not run anywhere. | Add a clang-check job (expect many findings: opt in per component, safety first). Pin cppcheck and raise `--std`. | M-L | infra |
| CS-SAF-01 | partly | The profile marks main, joystick, post, fw_core as safety; drive_session, drive_adapter, stick, joystick_cal, control are not in its table although they carry REQ-DRV/DAD/STK/CAL IDs. Requirement matrix: 97 of 137 covered by a case, 40 not (CAL-01..07 and SET-01..04 have L1 tests that do not cite the IDs; UI, FWI, RUI, STO are bench-only). | Cite REQ IDs in the cal and settings L1 cases (S). Update the profile safety list (S). | S | test |
| CS-SAF-02 | partly | drive_session and post: hand-written switch + `constexpr` table + oracle (DRV-001..113, DSO-001..013). The stick pipeline is a pure function with golden tests. The rtps_comms gate/lock logic is not a table. | Table for the rtps_comms/gate machine in the islands rework. | L | refactor |
| CS-SAF-03 | partly | Open items (stale input, POST gate, open pot, link-loss paths): hazard-fixes.md C1-C4 and the seat path. | Tracked in hazard-fixes.md. |  | tracked |
| CS-SAF-04 | not met | `rtps_comms` and selftest allocate after start-up; no formatted-log queue; TS-DET-08 missing; safety logic not on its own island. | Island rework plus allocation guard. | L | owner decision |
| CS-SAF-05 | partly | Two approvals are not enforced (profile deviation row: one approver). `tests/host/gcov_summary.py` exists but no CI gate with `--fail-under-branch 100`. | Wire the gcovr gate for the table components (S-M). The second reviewer is an owner matter. | S-M | owner decision |
| CS-SAF-06 | not met | Profile: 'which safety tasks the TWDT covers is unknown'; the TWDT fired ~8 s into boot on 2026-10-06. | Tracked as C4/H11 in hazard-fixes.md. |  | tracked |
| CS-SAF-07 | partly | grep: 0 `NOLINT` in first-party code. But `suppressions.txt` suppresses whole cppcheck categories for all code, safety included (CS-SAF-07 allows only per-site NOLINT + deviation). | Scope the category suppressions away from safety components (`--suppress` by path). | S | infra |
| CS-SAF-08 | partly | drive_session/drive_adapter use direct calls (no `std::function` in drive_adapter). rtps_comms not reviewed for indirect calls. | Review in the rtps_comms rework. | S | docs |
| CS-GIT-01 | met | `main`/`dev` and `dev_<topic>`/`dev_ai_<topic>` branches; PRs #3 and #4 merged. |  |  |  |
| CS-GIT-02 | partly | K1 included a behaviour commit (demo screen deleted) in a refactor branch, flagged in progress.md. | Keep refactor and behaviour PRs apart for the C-lanes. | S | docs |
| CS-GIT-03 | partly | Recent commit messages say what and why; rule IDs are cited in some, not all. | Review only. | S | docs |
| CS-GIT-04 | not met | `.github/` holds only `workflows/`; no pull request template. | Add the espp template plus rule IDs, REQ IDs, profile changes, diagrams. | S | docs |
| CS-GIT-05 | not met | No `CODEOWNERS`; branch protection 'not recorded' in the profile. | Owner sets protection on main/dev and records it; add `CODEOWNERS`. | S | owner decision |
| CS-GIT-06 | met | Commits carry the author identity and co-author lines; owner reviews. |  |  |  |
| TS-PRI-01 | met | `python tests/run.py list` shows 17 entries; `run <ID>` and `--seed` exist. |  |  |  |
| TS-PRI-02 | met | Scripts decide verdicts (ratchet, guards, `run_bench.py`, compare_selftest); agents do not grade. |  |  |  |
| TS-LVL-01 | partly | Hazards found on the bench got L1 cases (oracle, by-input, equivalence). L2 is missing, so UI-level bugs have no lower-level home. | Build L2 (TS-RUN). | L | test |
| TS-LVL-02 | met | `l0.yml` ran 9m29s and Build 17m25s on 4d82b9f (green). |  |  |  |
| TS-DET-01 | partly | Host L1 uses fake time; firmware has `sleep_this_thread` 6 + 17 and bench scripts wait with timeouts. | With CS-FLW-02. | M | refactor |
| TS-DET-02 | met | `tests/run.py:505` shuffles with a logged seed; CI runs `--all --seed 1`. |  |  |  |
| TS-DET-03 | partly | `run_bench.py` flashes and resets; profile: 'no regions are erased by any runner'. | Add fixtures-restore step to B0. | M | test |
| TS-DET-04 | partly | `pass_threshold` fields in the manifest; Wi-Fi up judged over 5 boots by hand; no nightly. | Nightly runner (TS-PAR-02). | M | infra |
| TS-DET-05 | partly | drive_golden, stick golden and fixtures exist; goldens change by review only; screen goldens (TS-RUN-03) absent. | Add golden folders to `CODEOWNERS`. | S | docs |
| TS-DET-06 | met | Profile table of environment traps and preflight checks; `run_bench.py` B0 preflight. |  |  |  |
| TS-DET-07 | not met | CS-OWN-11 is not implemented, so nothing is on in test builds. | Follows CS-OWN-11. | M | refactor |
| TS-DET-08 | not met | No allocation guard (`CONFIG_HEAP_USE_HOOKS` or `--wrap`) anywhere. | Host `--wrap` guard for L1 first (S), chip hook after (M). | M | test |
| TS-DET-09 | partly | G10 (`task_dump.py`) compares running tasks with `tools/guards/baselines/tasks.json` on the bench, not with topology `TASKS`; not in firmware test builds. | Switch the baseline to `TASKS` after wiring. | M | test |
| TS-UNIT-01 | partly | 17 L1 apps built host-native with `-fsanitize=address,undefined` (`tests/host/common.mk`); not ESP-IDF test apps, no pytest-embedded runner, no L3 run (deviation approved Q6). | Keep the deviation; write the on-chip runner when a board slot exists. | L | test |
| TS-UNIT-02 | partly | 452 cases; names start with an ID; 202 cases cite no requirement. | Fine as is; cite where a requirement exists. | S | test |
| TS-UNIT-03 | partly | Fakes live in each component's test tree; no shared `test_support` component. | Extract when a second user needs a fake. | S | refactor |
| TS-UNIT-04 | partly | settings, post, topology, drive_session (DSO-001..013) test their tables. `actions_spec.h` is untested. | With CS-TYP-03. | S | test |
| TS-UNIT-05 | partly | FWC-L1 covers mailbox, queue and ThreadChecker on the host; ISR calls and the chip half are open (REQ-FWC-13). | L3 app. | M | test |
| TS-UNIT-06 | met | 29 must-not-compile cases in `run.py report`; topology `mnc/` has twin cases. |  |  |  |
| TS-UNIT-07 | partly | Hostile-input cases for ota_parse, the cal codec and rtps_spec. The remote_ui command parser and the settings file loader are not covered. | Add hostile-input cases for those two. | M | test |
| TS-UNIT-08 | met | DRV-001..012 and DRV-101..113 for the drive table; DAD-001..006; topology oracle. |  |  |  |
| TS-UNIT-09 | partly | Per-app time not reported; the whole L1 job fits in the 9 min L0 run. | Report per-app time in `run.py`. | S | test |
| TS-RUN-01 | not met | No host application with the real logic, models and views and a headless display; `tests/host` runs components only. The bench uses the real board. | Build L2 after lanes D/S (needs LVGL on linux). | L | test |
| TS-RUN-02 | not met | No `tests/l2/`. | With TS-RUN-01. | L | test |
| TS-RUN-03 | not met | No golden image per screen and theme. `docs/screenshots` are documentation, not goldens. | With TS-RUN-01. | L | test |
| TS-RUN-04 | partly | `tests/host/rtps_spec` and `mcb_sim` test the spec and the sim; rtps_comms does not build for the host. | After the rtps_comms rework. | L | test |
| TS-TGT-01 | not met | L3 is not run. | See TS-UNIT-01. | L | test |
| TS-TGT-02 | partly | Bench B3/B4 check the DRV2605 and joystick; no per-peripheral identity/read-back tests. | Driver tests as POST checks (C3). | M | test |
| TS-TGT-03 | not met | No stress test run (profile). | Run once with fault injection; record stack marks. | M | test |
| TS-POST-01 | not met | No boot POST; `components/post` is an evaluator, not wired. | Tracked in hazard-fixes.md (C3). |  | tracked |
| TS-POST-02 | partly | Extended self test exists (`selftest.cpp`, `rtps_selftest.py`). | Table-driven rework together with POST. | L | refactor |
| TS-POST-03 | partly | `components/post` has the typed check table with D4 placeholder limits; the self test has its own list. | Limits: owner P3 (measure first). | M | owner decision |
| TS-POST-04 | not met | Not wired; no safe state on a failed check. | Tracked in hazard-fixes.md (C3). |  | tracked |
| TS-POST-05 | partly | `SELFTEST` line format in `selftest_spec.hpp` (parity test); `POST` lines absent. | With C3. | S | refactor |
| TS-POST-06 | not met | No nightly boot loop; no power switching on the bench. | Needs bench hardware (a switchable supply). | M | owner decision |
| TS-SYS-01 | partly | Debug channel TCP 3333 plus the MCB sim; `run_bench.py` B0-B5. | Broaden to the TS-SYS-02 categories. | M | test |
| TS-SYS-02 | partly | B5 fault cases (sim modes ign/drop/ongone) exist. No persistence/power-cycle, OTA rollback or soak runs. | Add persistence, OTA and soak to the nightly. | L | test |
| TS-SYS-03 | partly | CI builds release and bench variants from one commit; the release artifact is not run through L4 or a smoke list. | Define the smoke list in the profile. | S | docs |
| TS-SYS-04 | not met | Profile: no manual checklist. | Write it (haptics, tones, stick). | S | docs |
| TS-DEF-01 | met | `tests/manifest.d/*.yaml`: 17 entries with the required fields. |  |  |  |
| TS-DEF-02 | met | `python tests/run.py check` -> `17 entries, 452 L1 cases, 0 errors -> PASS`; in `l0.yml`. |  |  |  |
| TS-DEF-03 | met | `run.py` has `list`, `check`, `run`, `report`, `selftest`; a run writes run.json, summary.json, junit.xml, report.txt (`run.py:23,568-572`). |  |  |  |
| TS-PAR-01 | met | Host tests have their own build dirs (`common.mk`); CI runs `--all`. |  |  |  |
| TS-PAR-02 | partly | Lease file (`rammp/.board-lease`) is used by `run_bench.py`; no nightly script. | Nightly script. | M | infra |
| TS-REL-01 | not met | No `version.txt` (`PROJECT_VER` from `git describe`); no release report or sign-off flow. | Add `version.txt`; release script later. | S | owner decision |
| TS-REL-02 | not met | No release gate defined. | Follows TS-REL-01. | M | docs |
| TS-REL-03 | not met | No release-candidate flow. | Follows TS-REL-01. | S | docs |
| TS-COV-01 | partly | `run.py report`: 137 requirements, 97 covered, 40 not; 29 must-not-compile cases. Informational in CI: an uncovered safety requirement does not fail L0. | Cite IDs for CAL-01..07 and SET-01..04 (S), then make safety gaps fail L0. | S | test |
| TS-COV-02 | partly | `tests/host/gcov_summary.py` exists; no CI threshold (80/70/100). | Gate the table components at 100% branches; others 80/70. | S-M | infra |
| TS-FLK-01 | met | No flaky L1 test known; bench flakes (time.render_max, rtt) are judged by script; none quarantined. |  |  |  |

## Dependencies between fixes

- Topology wiring (owner decision 1) comes before CS-OWN-03..11, CS-CON-02, TS-DET-07/09 and the channel half of the hazard fixes.
- The clang-check job (gap 6) comes before fixing CS-NAM-01, CS-ERR-02 and CS-FLW-01 findings.
- Lanes D/S (fragments) come before CS-UI-03..05, the global-subject cleanup, the `main.cpp` warning set (CS-LNG-02) and the L2 host app.
- The selftest rework comes after the UI-island request path (CS-UI-06) and shares its POST table with hazard C3.
- The rtps_comms rework (islands) comes before CS-SAF-02/04, CS-OWN-01 and TS-RUN-04.

## Method

Rules read from CODING_SPEC.md (96 CS rules) and TESTING_SPEC.md (50 TS rules) on fw-standards `dev`. Commands run on the audit worktree: `python tools/l0/ratchet.py check` and `selftest`; the four `tools/guards/*.py selftest`; `python tests/run.py list`, `check`, `report`; `python scripts/ui_contract.py`; greps with ripgrep for logging, threads, allocation, casts, macros, `default:`, namespaces, includes and CI contents; `gh run list` on `dev_refactor`. No build, no host test run (L1 host tests need WSL g++), no board.
