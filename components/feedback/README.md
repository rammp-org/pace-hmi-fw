# feedback

Plays the user's haptic and sound cues (CS-LAY-02). Namespace `hmi::feedback`.

Moved out of `main.cpp`'s one-TU fragments by app-main-shrink S2
([docs/plans/app-main-shrink.md](../../docs/plans/app-main-shrink.md)), clean then move
(V2): the code is the fragments' code, line for line where the ratchet allows.

| File | What |
| --- | --- |
| `da7280.hpp` | `espp::Da7280`, the DA7280 haptic driver (DRO mode), header-only. Moved from `main/` |
| `da7280_bench.hpp` | `run_da7280_bench`: the DA7280 register probe and functional test, bench only (`CONFIG_HMI_BENCH_DA7280_TEST`, default off) |
| `config.hpp` | the Kconfig options as `constexpr` (CS-TYP-05) |

## Requirements

None written yet. The behaviour is main's as it was; the bench checks it (B0-B5).

## Tasks

None of its own. The bench test runs on `app_main`'s task, at boot.

## Dependencies

espp `base_peripheral`, `i2c`, `logger`.

## Ratchet

`da7280_bench.cpp`'s `std::this_thread::sleep_for` lines (the bench test's timing) and
`da7280.hpp`'s C casts and bus locks are legacy debt moved verbatim with
`tools/l0/ratchet.py transfer` (grants in `tools/l0/baseline.json`).
