// split_main.py fragment of main.cpp (frag_brightness: original lines 718-766); one TU, see main.cpp.
static lv_subject_t brightness_subject; // backlight %, kBrightnessMin..MaxPercent
static lv_timer_t *brightness_save_timer = nullptr;

static void brightness_save_cb(lv_timer_t *timer) {
  lv_timer_pause(timer);
  settings_set_brightness(lv_subject_get_int(&brightness_subject));
}

static void brightness_observer(lv_observer_t *, lv_subject_t *subject) {
  espp::M5StackTab5::get().brightness(static_cast<float>(lv_subject_get_int(subject)));
  if (brightness_save_timer != nullptr) { // null on the first run, at bind time
    lv_timer_reset(brightness_save_timer);
    lv_timer_resume(brightness_save_timer);
  }
}

// Any task. Clamped, so nothing can turn the screen fully off.
static void brightness_set(int percent) {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  lv_subject_set_int(&brightness_subject,
                     std::clamp(percent, kBrightnessMinPercent, kBrightnessMaxPercent));
}

// The Tab5's side button: the next of 25/50/75/100 % above the current level.
static void brightness_step() {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  const int32_t now = lv_subject_get_int(&brightness_subject);
  int32_t next = 25;
  for (int32_t level : {25, 50, 75, 100}) {
    if (level > now) {
      next = level;
      break;
    }
  }
  lv_subject_set_int(&brightness_subject, next);
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
