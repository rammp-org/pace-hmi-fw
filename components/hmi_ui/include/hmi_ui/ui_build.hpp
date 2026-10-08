#pragma once
// The UI island's build steps that need nothing of main's but what they are passed: the
// screens ui_init builds, the build's finish (the boot screen's exit) and the perf overlay's
// font. Moved from main.cpp's app_main wiring; app_main calls them in its order, on its task,
// before the UI task starts.

#include "lvgl.h"

namespace hmi::ui {

/// @brief Every screen ui_init builds, less the ones built on demand, and the boot logo.
/// @param boot_logo The pre-rasterised A8 wordmark (main/boot_logo.c).
/// @param benchmark_drive_screen Load the Drive screen at once (the FPS debug build).
/// app_main, before lv_task starts.
void build_screens(const lv_image_dsc_t *boot_logo, bool benchmark_drive_screen);

/// @brief The perf overlay hidden, @p strip_overdraw run, and the boot screen's exit armed.
/// @param strip_overdraw main's overdraw pass (it logs what it cleared).
/// app_main, before lv_task starts, once everything behind the first screen is wired.
void finish_build(void (*strip_overdraw)());

/// @brief The perf overlay's label in a readable font.
/// app_main, before lv_task starts.
void perf_overlay_font();

/// The "FPS counter" Skunk Works slot: LVGL's built-in perf overlay (lv_sysmon), which
/// renders FPS/CPU on the sys layer above every screen, shown and hidden in turn. One
/// instance (main's, constinit); starts hidden, as finish_build leaves the overlay.
class PerfOverlay {
public:
  /// @brief Shows the overlay if hidden, else hides it. UI task (an action), lvgl_mutex held.
  void toggle();

private:
  bool shown_ = false;
};

} // namespace hmi::ui
