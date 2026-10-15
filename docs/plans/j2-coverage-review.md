# J2 coverage: review before merge (2026-10-15)

**Status:** needs decision. Do not merge `dev_ai_stick_j2` until the owner has read this.
**Summary:** `components/stick/src/mapping_selftest.cpp` has no source-level branch. Every gcov
branch in it is one the compiler generates for C++ exceptions. Excluding throw edges
project-wide (decision (b)) lifts it from 46.67 % to 87.50 %. 2 unwinding-cleanup branches
remain, and gcov does not mark them `throw`, so the 100 % gate still fails.
**Next:** the owner picks one of the options at the end.

## Split (CI compiler, gcc 13; local gcc 15 in brackets)

| kind | count | covered | where |
|---|---|---|---|
| real (source-level decisions) | 0 | n/a | none: `miss()`/`close()` have no branch by design |
| throw edges (call to landing pad, `"throw": true`) | 14 [14] | 0 | lines 57, 59, 60 x2, 61, 62 x2, 63, 64, 113-117 |
| normal-return side of those calls | 14 [14] | 14 | the same lines |
| unwinding cleanup flag, not marked throw | 2 [4] | 0 [1] | line 66, the `}` of `rectangular_failures()` |

Line 66 is GCC's cleanup flag for the temporary `espp::Joystick::Config` (its `std::function`)
built in `rect({...})` on lines 55-57. It is tested only in the landing pad, which runs only
when `espp::Joystick`'s constructor throws. gcc 15 also emits the same test on the normal
path: its true side is dead because the flag is cleared just before the constructor call. In
the firmware (`-fno-exceptions`) none of these branches exist. Built that way, the file has 0
branches, so gcov reports n/a and the gate passes (checked by hand with gcc 13).

(a) No host test was added, because there is no real branch to cover. STK-106 already covers
every line and every non-exception edge that can be reached. The (c) case does not arise.

## Diff (`dev_ai_stick_j2`, one commit f6123f3 on 8b365c6)

| file | change |
|---|---|
| `tests/host/gcov_summary.py` | Counts branches from gcov's JSON and drops `"throw": true` edges, the same rule as gcovr's `--exclude-throw-branches` (no new dependency). A branch counts as taken when its count is non-zero, which is gcov's own rule. `--include-throw-branches` prints the old figure. Lines are unchanged. |
| `components/drive_session/test/oracle_selfcheck/Makefile` | Its awk over gcov's text summary is replaced by `gcov_summary.py`. The bare name `drive_session_table.hpp` is kept and the figure is unchanged (91.67). |
| `tools/l0/coverage_baseline.json` | Regenerated with `coverage_gate.py update` on gcc 13 with the new rule. Every file rose or stayed and none dropped. `alloc_counter.hpp` and `seat_*.hpp` were added (they meet spec). `mapping_selftest.cpp` was **not** added, because baselining it would hide a new safety file that is below spec. |
| `tools/l0/README.md`, `tools/l0/coverage_gate.py` | Document the rule, why it exists, and the landing-pad gap. |

What was checked:
- With `--include-throw-branches`, every file reproduces the old figure exactly.
- gcc 13.4 (local, extracted to scratch) reproduces CI's gcc 13.3 figures on 0850934 for every
  gated file.
- `fw_core` already excluded throw edges (`coverage_report.py`).

Largest rises: `settings.cpp` 69.23 to 100, `topology_espp.hpp` 50 to 100, `cal_record.cpp`
80.95 to 98.59, and `ota_parse/*` +13 to +32 points. Throw edges were what held these files
down.

## For the owner

1. Read the measurement change above. It raises the bar for every file, because baselines can
   only go up from here.
2. Pick how the 2 remaining landing-pad branches are handled:
   - Build host L1 with `-fno-exceptions`, as the firmware is built. This removes every throw
     edge and landing pad at the source, so the exclusion rule is no longer needed.
   - Also drop branches on lines with no code, which is gcovr's
     `--exclude-unreachable-branches` (here the lone `}` on line 66).
   - Change `mapping_selftest.cpp` so it uses no temporary `Config`. That is safety code, so it
     needs your approval.
3. The `host-l1` and build jobs cannot run on this branch in CI, because espp comes from the
   local worktrees through `override_path`. The figures above are local.
