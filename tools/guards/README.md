# Guards for the app_main shrink

Host tools for the guards in `docs/plans/app-main-shrink.md` (§5, revisions V6, V7, V11).
Stdlib Python 3.12+, Windows or WSL. Each exits 0 PASS, 1 FAIL, 2 bad input (INVALID: no
verdict). Scripts decide; nobody reads a diff and calls it a pass (TS-PRI-02).

| Tool | Guard | Reads | Decides |
| --- | --- | --- | --- |
| `init_order.py` | G4 static init (V6) | an IDF build folder: objects via `readelf`/`objdump` | no new global constructor, lazy static or cross-TU init read outside `baselines/init_order.<variant>.json` |
| `exports.py` | G2 exported symbols (V11) | an IDF build folder: objects via `nm`; headers under `--src` | no duplicate strong symbol, no duplicate internal `_ZZ`/`_ZGV` local static, no export missing from every first-party header |
| `task_dump.py` | G10 task table (V11) | the bench board's `TASKS` answer (remote UI) | the running tasks equal `baselines/tasks.json` (or topology `TASKS`) |
| `observer_census.py` | G11 observer census (V7) | the board's `OBSERVERS` answer | each subject's observer count equals `baselines/observers.json` |

`guardlib.py` holds the shared parts: the build folder (first-party objects from
`compile_commands.json`: sources under the project folder, not `managed_components`; keyed
`<component>/<object>` so keys match across build folders), toolchain discovery
(`CMakeCache.txt`'s `CMAKE_NM`, else the newest `C:/Espressif/tools/riscv32-esp-elf/*`, or
`--toolchain-prefix`), parsers for readelf/objdump/nm text, `c++filt` demangling, and the
Unity-format case runner the `selftest` modes print with.

## Running

```
python tools/guards/init_order.py check --build C:\b\integ6b_default                  # default variant
python tools/guards/init_order.py check --build C:\b\final_bench --variant bench      # CONFIG_HMI_REMOTE_UI
python tools/guards/exports.py check --build C:\b\integ6b_default --src C:\w\<tree>
python tools/guards/task_dump.py fetch --ip <board> --out tasks.json --into selftest.json
python tools/guards/task_dump.py check --dump tasks.json
python tools/guards/<tool>.py selftest                                               # inline samples only
```

`--src` is the tree whose headers count as declarations; it defaults to the build's own
project folder (`project_description.json`). `--json out.json` writes the verdict, failures
and notes for a runner.

## Baselines: what they are, who updates them

| File | Made by | Update |
| --- | --- | --- |
| `baselines/init_order.default.json` | `init_order.py write --build C:\b\integ6b_default` (dev_refactor 5e035fc) | `write` after the diff is reviewed; `allow` entries ({object or `*`, symbol, reason}) are hand-kept and survive `write` |
| `baselines/init_order.bench.json` | `write --build C:\b\final_bench --variant bench` | same; differs from default only by `remote_ui.cpp`'s `cfg` |
| `baselines/exports_allowlist.json` | by hand | one entry per excused finding, each with a reason; printed on every run, stale ones reported |
| `baselines/tasks.json` | by hand from source (file:line per task) | `task_dump.py baseline --dump <dump>` adds observed IDF/espp tasks and fills nulls, never overwrites a declared value |
| `baselines/observers.json` | not yet (no firmware half) | `observer_census.py baseline` from a reviewed boot |

A baseline change is a declaration change: never to silence a check (CORE never-list). A
static that moves TU in a shrink step fails `init_order` with "moved?"; the reviewer accepts
the move by regenerating the baseline in that step's PR.

## Rules in one place

- **init_order** (per object): `.init_array*`/`.ctors*` relocations name the init functions;
  their relocations (and `__static_initialization_and_destruction_*`) name what they touch:
  writable data of the object (`objects`), data of another TU (`reads_external`), calls.
  `_ZGV*` guards are lazy statics. COMDAT/weak entities (fmt's locale facet id, espp `get()`
  singletons) are `shared` and compared by name once, so a new TU that merely includes fmt
  is not a new constructor. FAIL: new init function touching anything not baselined; new
  object / lazy static / external read in an object; new shared name. Removals and changed
  calls are notes.
- **exports**: E1 strong symbol in 2+ first-party objects; E2 internal-linkage `_ZZ`/`_ZGV`
  in 2+ objects (a `static` helper with a local static, in a header); E3 strong export whose
  identifier is in no first-party header (accidental external linkage). E3 skips
  `components/ui` (generated) and `components/m5stack-tab5` (vendored), the ratchet's scope,
  and prints their count each run. Considered and left out: local functions with the same
  name in 2+ objects. Today's build has 33, all GCC clones (`.isra`, `.part`) of fmt/std
  templates, so the rule would be noise.
- **task_dump**: T1 a `boot` task is missing; T2 prio/core differ, or stack outside
  [declared-16 B, declared]; T3 a running task is undeclared; T4 the list was cut; T6 a name twice in a complete dump
  (unless the table says `"multi_instance": true`) or `dup_handles` > 0; T5
  `stack_free` under a declared `stack_free_min` floor (none yet: CS-MEM-04 margins wait for the
  stress test; a fixed `stack_free` is never compared). Names compare on 15 characters
  (`configMAX_TASK_NAME_LEN` 16). ESP-IDF pins an unpinned task to the core of its first FPU
  or PIE use, which differs between boots, so such tasks declare core `"fpu"`: any core with
  `coproc_pinned: true`, or -1 before the first use. A task declared -1 that reports a core
  fails. The table holds effective values: espp/pthread priority 0 runs at 5 (`pthread.c:336`).
- **observer_census**: O1 missing subject, O2 count differs, O3 new subject, O4 list cut.

## Host app wrappers

`tests/host/guard_<tool>/Makefile`: `make test` runs `<tool>.py selftest` and prints Unity's
case lines and summary line. That is the host L1 app contract minus C++: no compile, so
`BUILD_DIR`/`UNITY_DIR` are unused and `make coverage` adds no gcov line. There are no manifest
entries yet: `run.py check` requires `TEST_CASE` in C/C++ sources for an L1 app and has no L0
runner (Parked, see below).

## CI

- `l0.yml` job `checks`: the four `selftest`s (no toolchain needed).
- `l0.yml` job `bench-build`: after the bench build, `init_order.py check --variant bench` and
  `exports.py check` on `build_bench`, in the IDF image (they need its readelf/objdump/nm).
- `build.yml` job `build`: after the default build, the same two on `build` (`--variant default`).
- `task_dump` / `observer_census` checks need a board: bench runner only.

## G10 firmware half: the remote UI `TASKS` verb

The plan asks for the task dump "in the self-test JSON". Two things stand in the way:
1. The self-test JSON is built by `rtps_selftest.py` from the RTPS report rows, which are a fixed
   check table (`selftest_spec.hpp`) on a fixed message. A variable-length task list does not fit
   without changing that external interface (CORE ask-first).
2. `main/selftest.cpp` cannot grow. It is over the 1000-line limit, and the L0 ratchet
   (`tools/l0/ratchet.py`) fails any non-blank line added: tried, `lines 1089 > baseline 1030`,
   plus `if_config` and `log_direct`.

So the dump rides the bench-only remote UI instead (orchestrator decision, recorded under V11 in
the plan). `TASKS` answers one JSON line, and `task_dump.py fetch --into selftest.json` merges
it into the self-test JSON file on the PC. `take_tasks`/`send_tasks` in `main/remote_ui.cpp`
(compiled only with `CONFIG_HMI_REMOTE_UI`) walk the task list with `uxTaskGetSnapshotAll`
while holding the kernel lock (`prvTakeKernelLock`, IDF's wrapper for `xKernelLock`; see
below), then read name, priority, core (`xTaskGetCoreID`), the created stack size and the high-water
mark. The stack size is `pxEndOfStack` minus the start the task was created with: once a task
has used a coprocessor, IDF 6.0's RISC-V port carves the save area (132 B for the FPU) from the
bottom of the stack and moves the TCB's `pxStack` past it (`port.c:787-815`). So
`xTaskGetStackStart` alone reads 132-147 B short, as the first board dump did. The original
start is in the coprocessor save area at the stack top (`sa_tcbstack`). `coproc_pinned` is
that area's enable mask (`sa_enable`), bit FPU or PIE. The trap on a task's first use of either
sets the bit and pins the task (`portasm.S:99,111-114`). `sa_allocator` is no use as a flag:
the HWLP check on every switch-in sets it for every task that has run.

Why the kernel lock: on this dual-core (non-SMP) IDF FreeRTOS, `vTaskSuspendAll` stops only the
calling core's scheduler (`tasks.c:2499-2524`), and the list walk takes no lock. So the other core
could move a task between lists mid-walk: one e2f59ec dump in 9 listed `tab5_audio micr` twice, and
a task deleted there could have left a dangling TCB. `uxTaskGetSystemState` (trace facility) takes
the same lock, but it returns the name as a pointer into the TCB and no `pxEndOfStack`, so those
would be read after unlocking. Everything is copied inside the lock. The answer carries
`dup_handles` (a TCB seen twice: should stay 0) and `locked_us` (how long both cores' kernel was
held, mostly the high-water scans). Nothing runs unless the PC sends `TASKS`.

First use on the bench: `task_dump.py fetch`, then `task_dump.py baseline --dump` (review the
observed IDF/espp tasks it adds), then `check`.

## G11 firmware design (parked)

Counting observers is easy: `lv_ll_get_len(&subject->subs_ll)`. Knowing which subjects to
count is the hard part. main registers 35 subjects through `lv_subject_init_*` in `main.cpp`
(plus joystick_cal 3, log_view 1, selftest 5), all file-static. Design:
- V7 already brings `subject_init_checked()`, which aborts in test builds if observers
  exist. Give it a `name` argument and let it record `{name, subject*}` in a fixed
  `std::array<Entry, 64>` owned by the UI task (filled at start-up, no allocation after,
  overflow counted and reported as `complete:false`).
- A remote-UI `OBSERVERS` verb (bench only, same pattern as `TASKS`) takes the LVGL lock and
  answers `OK {"subjects":[{"name","observers"}...],"complete":...}` for
  `observer_census.py fetch`.
- Land it with or after V7's `subject_init_checked` commit: it touches every `lv_subject_init_*`
  call in main's fragments, which is not a small hook.
