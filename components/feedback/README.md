# feedback

Plays the user's haptic and sound cues (CS-LAY-02). Namespace `hmi::feedback`.

Moved out of `main.cpp`'s one-TU fragments by app-main-shrink S2
([docs/plans/app-main-shrink.md](../../docs/plans/app-main-shrink.md)), clean then move
(V2): the code is the fragments' code, line for line where the ratchet allows.

| File | What |
| --- | --- |
| `feedback.hpp` | `Feedback`: owns the two below; `refusal()` is the double click felt and heard. Built once in `app_main` (app-main-shrink V6) |
| `haptics.hpp` | `Haptics`: the DRV2605 (the motor on the PCB's JST connector): `play`, and `status`/`play_click` for the self test |
| `click_sound.hpp` | `ClickSound`: the click and the refusal's double click. Every sound leaves through `Config::play` and the LVGL tick and timer come in through the `Config`, so the component calls no `lv_*` and one writer (H16) is a change to `play` alone |
| `da7280.hpp` | `espp::Da7280`, the DA7280 haptic driver (DRO mode), header-only. Moved from `main/` |
| `da7280_bench.hpp` | `run_da7280_bench`: the DA7280 register probe and functional test, bench only (`CONFIG_HMI_BENCH_DA7280_TEST`, default off) |
| `config.hpp` | the Kconfig options as `constexpr` (CS-TYP-05) |

## Requirements

None written yet. The behaviour is main's as it was; the bench checks it (B0-B5).

## Tasks

None of its own. Callers' tasks, as before the move: `Haptics` and `ClickSound` on the
LVGL task (and `play_click` from the touch task, `play_refusal(true)` under `lvgl_mutex` from an
RTPS handler: hazard H16, unchanged), `Haptics::status`/`play_click` on the self test's task, the
bench test on `app_main`'s task at boot.

## Dependencies

espp `base_component`, `base_peripheral`, `drv2605`, `i2c`, `logger`, `format`; ESP-IDF `esp_timer`.

## Ratchet

`da7280_bench.cpp`'s `std::this_thread::sleep_for` lines (the bench test's timing), `haptics.cpp`'s
one (the GO poll in `play_click`, bounded at 1 s), and
`da7280.hpp`'s C casts and bus locks are legacy debt moved verbatim with
`tools/l0/ratchet.py transfer` (grants in `tools/l0/baseline.json`).
