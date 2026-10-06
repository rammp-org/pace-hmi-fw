# m5stack-tab5: vendored copy (CS-LAY-05)

This component is a modified copy of espp's M5Stack Tab5 BSP. `README.md` next to this file is
upstream's text, unchanged. This file records where the copy came from and what changed.

## Upstream

| | |
| --- | --- |
| Repository | https://github.com/esp-cpp/espp |
| Path | `components/m5stack-tab5` |
| Revision | `615b8dfde3e14e5a40ac4d1e2c605182b9d31bfd` |
| Version | espp/m5stack-tab5 1.2.0 (component registry) |
| Vendored | 2026-08-25, commit `5f5604b` ("Vendor the espp Tab5 BSP into components/m5stack-tab5 so the DPI panel config and LVGL buffer allocation can be changed") |
| License | MIT (espp) |

How this was checked (2026-10-06):
- The copied registry manifest `idf_component.yml` gives `version: 1.2.0` and
  `repository_info.commit_sha: 615b8df...`.
- `.component_hash` (`631cf860...`) matches the component manager's cached download
  `espp__m5stack-tab5_1.2.0_631cf860`, which passes its own `CHECKSUMS.json` (24/24 files).
- The cached `include/m5stack-tab5.hpp`, `src/video.cpp` and `src/sdcard.cpp` have the same git
  blob ids as the files on GitHub at `615b8df`.
- `upstream.diff` applies cleanly in both directions.

The copy is certain to be 1.2.0 at `615b8df`.

Not copied from upstream: `example/` and the registry's `CHECKSUMS.json`.
Local only: `.component_hash` (the registry archive's hash, kept from `managed_components/`),
`upstream.diff` and this file.

History: there was an earlier, separate fork of this BSP (2026-07). `d04ce5e` ("Migrate to espp
1.2.0: drop vendored Tab5 BSP fork") dropped it, and `5f5604b` vendored 1.2.0 again five days
later.

## Changes vs upstream

The full diff is `upstream.diff` (upstream 1.2.0 to this copy). `CMakeLists.txt`, `Kconfig`,
`README.md`, `idf_component.yml` and every source file not listed below are byte-identical to
upstream. `0ce1df3` added `set_flipped()` and touch mirroring, and `858c75b` reverted both
exactly ("the vendored board component goes back to what it was"). They leave no change.

| File | What changed | Why (commit) |
| --- | --- | --- |
| `src/video.cpp` `initialize_lcd()` | `dpi_cfg.num_fbs` 1 to 2 in all three panel branches (ILI9881, ST7123, ST7121) | LVGL renders straight into the panel's two frame buffers. A flush becomes a buffer switch instead of a 1.8 MB copy: full-screen render 75.6 to 48.6 ms (`5f5604b`). The comment was updated in `7660628` |
| `include/m5stack-tab5.hpp` | New `lcd_panel_handle()` | So `main` can call `esp_lcd_dpi_panel_get_frame_buffer()` (`main/main.cpp:208`) (`5f5604b`) |
| `src/video.cpp` `initialize_display()` | Fails if `initialize_lcd()` has not run. Gets the two DPI frame buffers and passes them to `Display` as `StaticMemoryConfig{vram0, vram1}` instead of `DynamicMemoryConfig` (double-buffered PSRAM) | No draw-buffer to frame-buffer copy. The driver's buffers are cache-line aligned, which LVGL's `LV_DRAW_BUF_ALIGN` 128 assert needs (`5f5604b`) |
| `src/video.cpp` `initialize_display()` | The 128-byte-aligned 1.8 MB `third_buffer` rotation scratch buffer is no longer allocated | Only the BSP's own `flush()` used it, and the application replaces that with a direct-mode flush. `flush()` already checks for null (`5f5604b`) |
| `include/m5stack-tab5.hpp` | `#include <freertos/semphr.h>`. New `present_frame(fb, timeout_ms = 100)`, `set_vsync_enabled()`, `vsync_enabled()`. Protected `notify_vsync()` ISR, `vsync_sem_`, `vsync_enabled_`, `vsync_timeout_logged_` | Vsync-gated, tear-free frame-buffer swap for LVGL DIRECT mode. `5f5604b` listed the missing gate as a known defect (`7660628`) |
| `src/video.cpp` `initialize_lcd()` | Creates the binary `vsync_sem_` (on failure, vsync turns off with a warning). Registers `on_refresh_done = notify_vsync` (was `nullptr`) | `on_refresh_done` fires when the DMA re-arms on the new `cur_fb_index`, which is the swap boundary (`7660628`) |
| `src/video.cpp` | New `present_frame()`: drains the semaphore, `esp_lcd_panel_draw_bitmap()` on the full frame, then waits for `notify_vsync`. On timeout it logs once and returns false without stalling the UI | Called from `main/frag_display_flip.inc` (`7660628`) |
| `src/video.cpp` | New `IRAM_ATTR notify_vsync()`: `xSemaphoreGiveFromISR`, and yields if a task woke | (`7660628`) |
| `src/sdcard.cpp` `initialize_sdcard()` | `host.slot = SDMMC_HOST_SLOT_0`. With `CONFIG_ESP_HOSTED_ENABLED`, `host.init` becomes a no-op. `SDMMC_HOST_FLAG_DEINIT_ARG` with `deinit_p = sdmmc_host_deinit_slot` | esp_hosted (WiFi through the ESP32-C6 on SDIO slot 1) claims the P4's only SDMMC controller before `app_main`, so a second host init would fail. Share the controller, and release only slot 0 when there is no card (`d893227`) |

## Static analysis status

- clang-tidy: excluded. `.clang-tidy` `ExcludeHeaderFilterRegex` matches
  `components/m5stack-tab5/` (CS-LAY-05).
- L0 ratchet (`tools/l0/ratchet.py`): excluded completely (`EXCLUDED_COMPONENTS`, "vendored"),
  so it has no baseline entries. This differs from `components/joystick`, which is ratcheted as
  legacy.
- clang-format: excluded in `.pre-commit-config.yaml` ("formatted by its upstream"), so the
  copy keeps upstream's formatting and `upstream.diff` stays readable.

## Dropping the fork, or keeping it current

- espp version: the copy is 1.2.0, while every other `espp/*` is pinned to 1.3.2
  (`main/idf_component.yml`, CS-LAY-06). The copy's own `idf_component.yml` asks for
  `espp/* >=1.0`, so it already builds against the 1.3.2 components. Between 1.2.0 and 1.3.2
  (`038eea4`) upstream changed only one hunk in the component's sources: `src/video.cpp`
  builds the DPI callback struct by assignment instead of a designated initializer, in the same
  place this fork registers `on_refresh_done`. Rebasing onto 1.3.2 means taking that hunk and
  keeping `on_refresh_done = notify_vsync`. Checked against GitHub at `038eea4`, file by file.
- To drop the fork, upstream to esp-cpp/espp (CS-LAY-07):
  1. A `num_fbs` setting (or 2 by default) and a getter for the panel handle or frame buffers.
  2. An option to render LVGL into the panel frame buffers (`StaticMemoryConfig`) and skip
     `third_buffer`.
  3. `present_frame()` with the `on_refresh_done` vsync gate.
  4. SD card on SDMMC slot 0 sharing the controller when esp_hosted owns it.

  Then put `espp/m5stack-tab5: '==<version>'` back in `main/idf_component.yml` at the same
  version as every other `espp/*`, and delete `components/m5stack-tab5/`.
