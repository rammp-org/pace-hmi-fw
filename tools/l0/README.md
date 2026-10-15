# L0 ratchet

`ratchet.py` counts debt per file and compares it with `baseline.json`: legacy counts may only
fall, new files start clean (TS-LVL L0). Stdlib only, Python 3.12+. Rule text: the docstring at
the top of `ratchet.py`.

| Command | Does |
| --- | --- |
| `python tools/l0/ratchet.py check` | exit 0 = PASS, 1 = FAIL (CI runs it) |
| `python tools/l0/ratchet.py selftest` | the parser and every rule on inline samples and a temp git repo |
| `python tools/l0/ratchet.py update` | lowers the baseline to today's counts and drops spent grants; never raises. Only the orchestrator runs it, after merges (app-main-shrink V12) |
| `python tools/l0/ratchet.py transfer <dest> --from main/main.cpp --base <sha>` | moves per-line grants for debt moved verbatim (below) |

## Metrics

| Metric | Rule |
| --- | --- |
| `lines` | baseline path may not grow; other paths ≤ 1000 (header 800) (CS-FIL-01) |
| `fn_over_120` | forbidden (CS-FIL-01) |
| `fn_over_60` | SHOULD; forbidden in a component whose own `.clang-tidy` sets `readability-function-size.LineThreshold` to 60 |
| `app_main_lines` | non-blank code lines of `app_main`; baseline path may not grow; other paths ≤ 300 (CS-LAY-01) |
| `static_state` | non-const function-local statics and static data members (CS-CMP-03) |
| `mutable_globals` | mutable namespace-scope variables (CS-CMP-03); a C callback's state in a listed bench-only file is allowed (below, T13) |
| `locks` | mutexes, lock guards, semaphores (CS-OWN-08); allowed in the fw_core channel helpers and as the espp Task callback's own `(std::mutex &m, std::condition_variable &cv)` with `std::unique_lock<std::mutex> lock(m); cv.wait_for(lock, …)` (exact idiom) |
| `lv_outside_ui` | `lv_*` outside a UI path: `components/hmi_ui/`, `components/drive_ui/` (owner, 2026-10-08), `components/ui/`, the main unit, `log_view`, `joystick_cal`, the frozen `main/{about,internet,update}_ui.{cpp,hpp}`, `components/remote_ui/src/remote_ui.cpp` and `components/remote_ui/include/remote_ui.hpp` (moved from `main/remote_ui.*` with their status; no other file there), and legacy `main/sample_ui_*` |
| `if_config` | `#if…CONFIG_` forbidden, except in a component's `include/**/config.hpp` (CS-TYP-05) |
| others | `std_thread`, `xtaskcreate`, `vtaskdelay`, `sleep_this_thread`, `esp_timer_create`, `log_direct`, `heap_raw`, `typedef`, `assert`, `esp_error_check`, `c_cast`: forbidden |
| `heap_raw` | `new`, `malloc`/`calloc`/`realloc`/`free`/`aligned_alloc`/`strdup` calls and every `heap_caps_*` call, except the read-only `heap_caps_get_*` queries (free size, minimum free, largest block, info), which allocate nothing: exempt by the owner's decision of 2026-10-08 (the quick POST's memory facts, hazard-c3-spec.md §2.2). `heap_caps_malloc`, `_calloc`, `_free` and the rest still count |

`main/main.cpp` and every `main/frag_*.inc` are one unit named `main/main.cpp`.

## C-ABI hook state (owner decision T13, 2026-10-11)

A C callback that takes no context argument (an ESP-IDF hook such as
`esp_heap_trace_alloc_hook`) can reach its state only by name, so that state may be a mutable
namespace-scope object (CS-CMP-03, CS-NAM-02). This is a rule, not a baseline entry: a file in
`C_HOOK_STATE` (`ratchet.py`) may hold the listed number of `mutable_globals` when, on every
run:

- it defines the listed callback with C linkage (`extern "C" ... <callback>(...) {`);
- it is bench-only: its component's `CMakeLists.txt` compiles it only inside
  `if(<option>) ... else()/endif()`, and `sdkconfig.defaults` (release) does not set the option.

Each entry is reviewed: the objects are the callback's state and nothing else. The same pattern
in a file that is not listed, built unconditionally, turned on in release, or with one object
more fails as usual (selftest cases `c_hook: ...`). Today's one entry:
`components/alloc_guard/src/alloc_guard.cpp` (`CONFIG_HMI_BENCH_ALLOC_GUARD`, the counter of the
on-chip allocation guard, TS-DET-08).

## Moving code out of the main unit (app-main-shrink V2)

1. Clean, then move: commit (a) removes what can be removed inside the unit; commit (b) moves
   the code verbatim.
2. If the moved code still carries debt, run `transfer` in commit (b), before committing, with
   `--base` = the commit before the move (usually `HEAD`):

   ```
   python tools/l0/ratchet.py transfer components/haptics/src/haptics.cpp --from main/main.cpp --base HEAD
   python tools/l0/ratchet.py check
   ```

   Each violating line of the destination becomes a grant: the sha1 of the line with comments
   and literals stripped and whitespace normalised (re-indenting is fine). A grant needs the
   line to be a violation the source had at `--base` and has lost since; the source's baseline
   drops by the same count, so baseline + grants never rise. The command writes nothing unless
   every violation of the destination can be granted.
3. Commit the code and `baseline.json` together.

| Cannot | Why |
| --- | --- |
| transfer `fn_over_60`, `fn_over_120`, `lines`, `app_main_lines` | not per line; split the function, or stay within the limit (V2) |
| grant into `components/fw_core` or a 60-line (safety) component | safety code starts clean (V2) |
| grant into a path that has a baseline entry for that metric | legacy paths have no per-line record |
| edit a granted line | the key changes, so `check` fails: clean it instead |

`check` re-verifies every grant against git history, so CI checks out with `fetch-depth: 0`.
`update` drops grants whose line is gone; a dropped grant never comes back.

## include_boundary.py

`python tools/l0/include_boundary.py check` fails when a file includes another component's
private `src/` files (CS-LAY-03); `selftest` runs its cases. Both run in the `L0` workflow.

## Coverage and Doxygen gates

`coverage_gate.py` (TS-COV-02, CS-SAF-05) reads `results/<run>/summary.json` from
`tests/run.py run --all --coverage` and fails if a file's L1 line or branch coverage falls
below `coverage_baseline.json`; the gap to the spec thresholds is reported, not failed.
`update` raises the baseline. `doxygen_gate.py` (CS-DOC-01) runs `doxygen ci/Doxyfile` and
fails if the warning count of any file rises above `doxygen_baseline.json`; `update` lowers
it. Both have a `selftest`. Reduce the debt, run `update`, commit the new baseline.
