# board

The HMI's Tab5 board: its bring-up on the BSP in the order the HMI needs, and the adapters the
BSP's own tasks call (CS-LAY-02). Namespace `hmi::board`.

Built on the vendored BSP (`components/m5stack-tab5`, `espp::M5StackTab5`): every driver, bus
and LVGL display is the BSP's. This component only calls it, in `app_main`'s order, with
`app_main`'s log lines and failure policy, and adapts its callbacks to the rest of the firmware.
Moved out of `main.cpp`'s `app_main` (owner, 2026-10-08), clean then move (app-main-shrink V2).

| File | What |
| --- | --- |
| `board.hpp`, `board.cpp` | `Board`: the bring-up steps, each a BSP call (or a few) with `app_main`'s log lines, returning false where `app_main` stopped: `probe_internal_i2c` (0x08..0x77, kept for the DA7280 bench test and the self test), `start_io_expanders` (and the EXT_5V_EN read-back), `start_display` (the LCD, its controller's name, the LVGL display with full-screen buffers), `start_imu` (with the housekeeping island's orientation filter), `start_sdcard` (a missing card only warns), `start_rtc` (the system clock seeded from a plausible RTC time), `start_battery` (and charging on), `start_audio` (the codecs), `start_side_button` (the `SideButton` adapter, a failure only warns), `start_touch` (the touch controller, its reports to the `TouchClick` it holds), `start_speaker` (the click's sample rate, unmuted, volume 60 %). Built once, an `app_main` local (app-main-shrink V6), on `app_main`'s task |
| `board_port.hpp`, `board_port.cpp` | `backlight` and `present`: the board's side of hmi_ui's `BoardPort` (the backlight, the vsync-gated panel swap of DisplayFlip's flush), each one BSP call. main's port table points at them; the Restart tile's `restart` stays main's |
| `adapters.hpp`, `adapters.cpp` | `TouchClick`: the BSP touch task's callback, a click on each press and every change logged at debug level. `SideButton`: the BSP button task's callback, each edge logged and a press stepping the brightness. Both reach main through a plain function in their `Config` (the click, the brightness step) |

## Requirements

None written yet. The behaviour is `app_main`'s as it was; the bench checks it (B0-B5, the
boot log's `main` lines in B2).

## Tasks

None of its own. `Board` runs on `app_main`'s task; the adapters run on the BSP's touch and
button tasks, as before the move.

## Dependencies

`m5stack-tab5` (the BSP), `housekeeping` (`SystemClock`, which the RTC seeds), `hmi_format`
(`clock_plausible`), espp `logger`.

## Ratchet

Clean: no transfer grants.
