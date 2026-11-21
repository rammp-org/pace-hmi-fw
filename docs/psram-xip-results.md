# Running code from PSRAM (XIP): bench results

**Verdict: not worth merging.** Menu navigation, the part users feel, did not get faster. The only clear gain was in a synthetic full-screen redraw test, and it costs 3.6 MB of PSRAM and 10 KB of internal RAM. This branch is kept for reference.

- **Change tested:** `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` (code and read-only data are copied from flash into PSRAM at boot and run from there). Config only, no code changes.
- **Baseline:** `main` at `0334045`.
- **Bench:** Tab5 over USB, 2026-11-21. Same code in every build; only the config differed.

## Menu navigation

A scripted touch walk over USB, repeated 5 times per run: open Diagnostics, Joystick and Log, scroll the menu focus through all 9 rows and back, return home. 3 runs per build.

| Build | Avg frame render | Slowest frame | FPS while animating |
|---|---|---|---|
| main | 26.6–27.5 ms | 169–171 ms | 12.2–12.9 |
| XIP from PSRAM | 25.7–26.9 ms | 153–161 ms | 12.3–12.8 |
| Flash at 120 MHz | 25.5–27.7 ms | 170–172 ms | 12.3–12.7 |

The differences are within run-to-run noise.

## Full-screen redraw stress

A test-only mode that forces the whole screen to redraw every frame, held for 12 s per screen. 2 runs per build; the runs agreed within 0.3 fps.

| Screen | main | XIP from PSRAM | Flash at 120 MHz |
|---|---|---|---|
| Menu | 9.1 fps | 11.0 fps (+21%) | 10.9 fps |
| Diagnostics | 9.6 fps | 13.1 fps (+36%) | 12.1 fps |
| Locked | 18.6 fps | 18.6 fps | 18.5 fps |

## Why it didn't deliver the 10x seen in tab5-emu

tab5-emu's gain was measured against flash running at 40 MHz. The HMI already runs its flash at 80 MHz in quad mode (QIO), so code fetch was never its bottleneck. Menu speed is limited by LVGL's rendering itself.

## Costs and other observations

- XIP: PSRAM heap 32,768 → 29,090 KB; internal RAM −10 KB (the same pool the W5500 bounce buffer uses).
- Flash at 120 MHz (`ESPTOOLPY_FLASHFREQ_120M` + `SPI_FLASH_HPM_ENA` + `BOOTLOADER_FLASH_DC_AWARE`) needs a new bootloader, which an OTA update cannot deliver.
- No display underruns (the old "black lines in icons" issue) were logged in any run.
- Bench limits: no MIB or Ethernet traffic during the tests; the W5500 failed to start on this bench in every build, including main.
- Render times step in whole display refreshes (~16.7 ms), so averages such as 99.7 ms and 83.4 ms are 6 and 5 refreshes.
