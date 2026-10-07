// BrightnessView: the backlight level (moved from main/frag_brightness.inc).

#include "hmi_ui/brightness_view.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>

void hmi::ui::BrightnessView::save_cb(lv_timer_t *timer) {
  lv_timer_pause(timer);
  const auto *view = static_cast<const BrightnessView *>(lv_timer_get_user_data(timer));
  view->config_.save(lv_subject_get_int(view->config_.subject));
}

void hmi::ui::BrightnessView::observer_cb(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *view = static_cast<const BrightnessView *>(lv_observer_get_user_data(observer));
  view->config_.backlight(static_cast<float>(lv_subject_get_int(subject)));
  if (view->save_timer_ != nullptr) { // null on the first run, at bind time
    lv_timer_reset(view->save_timer_);
    lv_timer_resume(view->save_timer_);
  }
}

void hmi::ui::BrightnessView::init(int32_t percent) {
  lv_subject_init_int(config_.subject, percent);
  lv_subject_add_observer(config_.subject, observer_cb, this);
}

void hmi::ui::BrightnessView::start_save_timer(uint32_t delay_ms) {
  save_timer_ = lv_timer_create(save_cb, delay_ms, this);
  lv_timer_pause(save_timer_);
}

void hmi::ui::BrightnessView::set(int percent) {
  lv_subject_set_int(config_.subject,
                     std::clamp(percent, config_.min_percent, config_.max_percent));
}

void hmi::ui::BrightnessView::step() {
  const int32_t now = lv_subject_get_int(config_.subject);
  int32_t next = 25;
  for (int32_t level : {25, 50, 75, 100}) {
    if (level > now) {
      next = level;
      break;
    }
  }
  lv_subject_set_int(config_.subject, next);
}
