// Frame-rate instrumentation (CONFIG_HMI_DEBUG_FPS). Moved from main.cpp's frag_fps.inc.
#include "hmi_ui/fps_meter.hpp"

#include "esp_timer.h"
#include "logger.hpp"

namespace hmi::ui {

void FpsMeter::attach(lv_display_t *display) {
  lv_display_add_event_cb(display, render_start_cb, LV_EVENT_RENDER_START, this);
  lv_display_add_event_cb(display, render_ready_cb, LV_EVENT_RENDER_READY, this);
}

void FpsMeter::report_if_due() {
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

// LVGL task, inside lv_task_handler: user data is the meter.
void FpsMeter::render_start_cb(lv_event_t *e) {
  static_cast<FpsMeter *>(lv_event_get_user_data(e))->render_start_us_ = esp_timer_get_time();
}

void FpsMeter::render_ready_cb(lv_event_t *e) {
  FpsMeter &self = *static_cast<FpsMeter *>(lv_event_get_user_data(e));
  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - self.render_start_us_);
  self.frames_.fetch_add(1, std::memory_order_relaxed);
  self.render_us_total_.fetch_add(us, std::memory_order_relaxed);
  uint32_t prev = self.render_us_max_.load(std::memory_order_relaxed);
  while (us > prev && !self.render_us_max_.compare_exchange_weak(prev, us)) {
  }
}

} // namespace hmi::ui
