#pragma once
// Frame-rate instrumentation (CONFIG_HMI_DEBUG_FPS). Moved from main.cpp's frag_fps.inc.

#include <atomic>
#include <cstdint>

#include "lvgl.h"

namespace hmi::ui {

/// @brief Frame-rate instrumentation (CONFIG_HMI_DEBUG_FPS): render time per frame, and
/// a once-a-second [FPS] debug line. main owns the one instance (constinit, no global
/// constructor) and attaches it only in the FPS debug build.
class FpsMeter {
public:
  constexpr FpsMeter() noexcept = default;

  /// @brief Counts every frame `display` renders, from RENDER_START to RENDER_READY.
  /// @param display The display to measure.
  /// app_main, before lv_task starts.
  void attach(lv_display_t *display);

  /// @brief Once a second, logs the frames, the average and the worst render time
  /// since the last report, and starts counting again. The first call only starts the clock.
  /// lv_task, after lv_task_handler.
  void report_if_due();

private:
  // LVGL task, inside lv_task_handler: user data is the meter.
  static void render_start_cb(lv_event_t *e);

  static void render_ready_cb(lv_event_t *e);

  std::atomic<uint32_t> frames_{0};
  std::atomic<uint64_t> render_us_total_{0};
  std::atomic<uint32_t> render_us_max_{0};
  int64_t render_start_us_ = 0;
  int64_t last_report_us_ = 0;
  bool started_ = false;
};

} // namespace hmi::ui
