// split_main.py fragment of main.cpp (frag_fps: original lines 78-107); one TU, see main.cpp.
// ---------------------------------------------------------------------------
// Frame-rate instrumentation: a debug feature, off unless CONFIG_HMI_DEBUG_FPS
// (CS-LAY-09), and never in sdkconfig.defaults, which ships.
//
// Reports over serial rather than the LVGL perf overlay, so throughput can be
// measured without eyes on the panel. RENDER_START/RENDER_READY fire only when
// LVGL actually rasterizes, so these numbers are real frame cost -- REFR_*
// would tick even on an idle screen and read as a meaningless "infinite fps".
//
// kFpsStress forces a full-screen invalidation every LVGL cycle. Without it a
// static SquareLine screen invalidates nothing and renders nothing, which
// measures the redraw path not at all. With it we get sustained worst case
// (CONFIG_HMI_DEBUG_FPS_STRESS).
//
// The once-a-second report goes through its own espp::Logger at debug level,
// tag "fps" (CS-LOG-01/03): developer detail, and only in this debug build.
// The counters are hmi::ui::FpsMeter's members.
// ---------------------------------------------------------------------------
static constexpr bool kFpsInstrument = CONFIG_HMI_DEBUG_FPS_AS_INT != 0;
static constexpr bool kFpsStress = CONFIG_HMI_DEBUG_FPS_STRESS_AS_INT != 0;

namespace hmi::ui {
namespace {

/// @brief Frame-rate instrumentation (CONFIG_HMI_DEBUG_FPS): render time per frame, and
/// a once-a-second [FPS] debug line. main owns the one instance (constinit, no global
/// constructor) and attaches it only in the FPS debug build.
class FpsMeter {
public:
  constexpr FpsMeter() noexcept = default;

  /// @brief Counts every frame `display` renders, from RENDER_START to RENDER_READY.
  /// @param display The display to measure.
  /// app_main, before lv_task starts.
  void attach(lv_display_t *display) {
    lv_display_add_event_cb(display, render_start_cb, LV_EVENT_RENDER_START, this);
    lv_display_add_event_cb(display, render_ready_cb, LV_EVENT_RENDER_READY, this);
  }

  /// @brief Once a second, logs the frames, the average and the worst render time
  /// since the last report, and starts counting again. The first call only starts the clock.
  /// lv_task, after lv_task_handler.
  void report_if_due() {
    if (!started_) {
      last_report_us_ = esp_timer_get_time();
      started_ = true;
    }
    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_report_us_ >= 1000000) {
      // Made per report rather than kept in a static: debug build, once a second.
      const espp::Logger fps_log({.tag = "fps", .level = espp::Logger::Verbosity::DEBUG});
      const uint32_t frames = frames_.exchange(0);
      const uint64_t total_us = render_us_total_.exchange(0);
      const uint32_t max_us = render_us_max_.exchange(0);
      const float secs = static_cast<float>(now_us - last_report_us_) / 1e6f;
      last_report_us_ = now_us;
      fps_log.debug("[FPS] {:.1f} fps | render avg {:.2f} ms | max {:.2f} ms",
                    static_cast<float>(frames) / secs,
                    frames ? (static_cast<float>(total_us) / 1000.0f) / static_cast<float>(frames)
                           : 0.0f,
                    static_cast<float>(max_us) / 1000.0f);
    }
  }

private:
  // LVGL task, inside lv_task_handler: user data is the meter.
  static void render_start_cb(lv_event_t *e) {
    static_cast<FpsMeter *>(lv_event_get_user_data(e))->render_start_us_ = esp_timer_get_time();
  }

  static void render_ready_cb(lv_event_t *e) {
    FpsMeter &self = *static_cast<FpsMeter *>(lv_event_get_user_data(e));
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - self.render_start_us_);
    self.frames_.fetch_add(1, std::memory_order_relaxed);
    self.render_us_total_.fetch_add(us, std::memory_order_relaxed);
    uint32_t prev = self.render_us_max_.load(std::memory_order_relaxed);
    while (us > prev && !self.render_us_max_.compare_exchange_weak(prev, us)) {
    }
  }

  std::atomic<uint32_t> frames_{0};
  std::atomic<uint64_t> render_us_total_{0};
  std::atomic<uint32_t> render_us_max_{0};
  int64_t render_start_us_ = 0;
  int64_t last_report_us_ = 0;
  bool started_ = false;
};

} // namespace
} // namespace hmi::ui

// The one meter. Attached and reported only when kFpsInstrument.
static constinit hmi::ui::FpsMeter fps_meter;
