// split_main.py fragment of main.cpp (frag_brightness: original lines 718-766); one TU, see main.cpp.
#include "hmi_ui/brightness_view.hpp"
static lv_subject_t brightness_subject; // backlight %, kBrightnessMin..MaxPercent

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

// The one instance. brightness_subject stays out here: the Settings row steps it too.
static constinit hmi::ui::BrightnessView brightness_view{{
    .subject = &brightness_subject,
    .min_percent = kBrightnessMinPercent,
    .max_percent = kBrightnessMaxPercent,
    .backlight = [](float percent) { espp::M5StackTab5::get().brightness(percent); },
    .save = settings_set_brightness,
}};

// Any task. Clamped, so nothing can turn the screen fully off.
static void brightness_set(int percent) {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  brightness_view.set(percent);
}

// The Tab5's side button: the next of 25/50/75/100 % above the current level.
static void brightness_step() {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  brightness_view.step();
}

/////////////////////////////////////////////////////////////////////////////
// TopBar clock
//
// The MIB sends Unix time plus its UTC offset in every MibStatus. It sets the
// system clock and the RTC, so the time keeps running through a lost link, and
// across a reboot without the MIB. Clock1 on every TopBar shows the system
// clock. No TZ is set, so the system clock simply holds the local wall time the
// MIB reported and nothing converts again.
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kClockPollMs = 1000;
static constexpr int64_t kClockMaxDriftS = 2; // disagree by more and the MCB wins
