# alloc_guard

The on-chip half of the allocation guard (TESTING_SPEC TS-DET-08; compliance-gaps.md gap 9). It
counts the heap allocations chosen tasks make once start-up is complete, so the bench can show
that the safety task `Read ADC` allocates nothing after start-up (CS-SAF-04, CS-MEM-02). The
host half is `tests/host/alloc_guard.{hpp,cpp}` (`--wrap` on the allocators, for L1 apps).

It exists only in bench builds with `CONFIG_HMI_BENCH_ALLOC_GUARD` (needs `CONFIG_HMI_REMOTE_UI`;
selects `CONFIG_HEAP_USE_HOOKS`). Without it the component compiles no source and its callers
reach it only inside `if constexpr (hmi::alloc_guard::ENABLED)`: a release image has no hook and
no symbol of it (the Build workflow checks the release ELF and sdkconfig). The CI variant is
`alloc_guard` in `l0.yml`: `sdkconfig.defaults;ci/sdkconfig.bench;ci/sdkconfig.allocguard`.

## How it counts

- ESP-IDF calls `esp_heap_trace_alloc_hook` (`esp_heap_caps.h`) after every successful
  allocation, on the allocating task's stack, possibly in an interrupt or with the flash cache
  off. `src/alloc_guard.cpp` defines it in IRAM. It reads the stack pointer and calls
  `AllocCounter::note` (`alloc_counter.hpp`, always inlined): an armed counter adds one to the
  watched slot whose stack range holds the stack pointer, and keeps the first allocation's
  size. Relaxed and acquire atomics only: no lock, no call, no allocation, no log.
- A task is told apart by its stack range, not by asking FreeRTOS for the current task (those
  functions are in flash, and two reads can straddle a core switch). An interrupt runs on its
  own stack, so an allocation from one is never charged to the task it interrupted.
- `start(tasks)` runs at app_main's park: it watches each named task's stack (slot i = task i;
  `xTaskGetHandle`, `xTaskGetStackStart` and `vTaskGetSnapshot`'s high end) and arms the
  counter. A task that does not exist yet (`rtps_pub` waits for the network) is looked up again
  by `count()`, and counts from then on; one made again under the same name keeps its slot's
  count.
- Not `CONFIG_HEAP_TASK_TRACKING`: it crashes core 1's start-up on the P4.

## The self test

`main/selftest_spec.hpp` names the watched tasks (`kAllocRows`) and their rows, `Need::BENCH`:
reported only by a build with the guard, after every other row, so any other build's report
is unchanged (same rows, same indices, `ready: 54 checks`).

| Row | Task | Limit |
| --- | --- | --- |
| `mem.alloc_adc` | `Read ADC` | to be 0..0 (a safety task allocates nothing after start-up); report-only today, see below |
| `mem.alloc_lvgl` | `lv_task` | counted, no limit (LVGL's own allocations are allowed, CS-MEM-02) |
| `mem.alloc_rtps` | `rtps_pub` | counted, no limit |

The detail of a non-zero row gives the first allocation's size. Read them with
`python scripts/rtps_selftest.py` against an image built with the guard. Against the plain
bench baseline (`tests/characterisation/baseline-e2047a4`) such an image differs by exactly
these three rows: B2's `ready: 57 checks` and B3's check list.

**`mem.alloc_adc` is not 0 today, and is report-only until it is** (owner decision T12,
2026-10-11; deviation row in `docs/project-profile.md`). The `Read ADC` task publishes XYTwist
itself, and espp's `RtpsParticipant::publish` (managed component `espp/rtps`) looks the writer up
with `writers_.find(std::string(topic))`: "rammp/joystick/xy_twist" is longer than the string's
inline buffer, so every publish allocates 24 B, about 30 times a second while a peer is matched.
Expected on the bench: the count grows with uptime, the detail reads `first 24 B`. The row's
limit is 0..kAnyHi so the self test does not fail on it, and the count stays in the report.
When espp looks the topic up without a temporary string (a transparent comparator), set the
limit back to 0..0 and drop the deviation row.

TS-DET-08 also asks for the task and a backtrace of an offending allocation. The hook cannot
take a backtrace (RISC-V without frame pointers, and nothing that is not IRAM-safe), so the row
names the task and the first size; `heap_trace` (`CONFIG_HEAP_TRACING_STANDALONE`) is the tool
to find the call once a count is not zero.

## Requirements

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-AGC-01 | Once armed, an allocation counts for the watched slot whose stack range [lo, hi) holds the stack pointer at the allocation; nothing counts before arming. | AGC-001..003, AGC-010 |
| REQ-AGC-02 | An allocation whose stack pointer is in no watched range (an interrupt's stack, an unwatched task) counts nowhere. | AGC-004 |
| REQ-AGC-03 | Slots count independently; each keeps the size of its first allocation; a slot moved to a new range keeps its count; an empty range or a slot out of range is ignored. | AGC-005..007 |
| REQ-AGC-04 | Counting is lock-free and loses nothing under concurrent allocations, and the counter itself allocates nothing. | AGC-008, AGC-009 |
| REQ-AGC-05 | A release build (`CONFIG_HMI_BENCH_ALLOC_GUARD` off) compiles no source of this component and has no heap hook. | the release ELF and sdkconfig check in `build.yml`; the `alloc_guard` variant build in `l0.yml` |

## Interface

| Header | What |
| --- | --- |
| `alloc_guard/config.hpp` | `ENABLED` (the Kconfig option), `MAX_TASKS` |
| `alloc_guard/alloc_counter.hpp` | `AllocCounter`, `Count`: the counting, plain C++ (host-tested) |
| `alloc_guard/alloc_guard.hpp` | `start`, `count`: the firmware side (guard builds only) |

## Tasks and dependencies

- No task of its own. The hook runs on whichever task allocates; `start` on app_main, `count`
  on the self test's task.
- Dependencies: private `freertos`, `heap`, `esp_hw_support` (guard builds only).
- `src/alloc_guard.cpp` holds one mutable object at namespace scope, the counter: ESP-IDF's
  hook takes no context argument, so it can reach its state only by name. Allowed as C-ABI hook
  state by the ratchet's `C_HOOK_STATE` rule (owner decision T13; tools/l0/README.md), which
  checks that the file stays bench-only. Constant-initialised, internal RAM.
