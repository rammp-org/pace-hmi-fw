// split_main.py fragment of main.cpp (frag_brightness: original lines 718-766); one TU, see main.cpp.
#include "hmi_ui/brightness_view.hpp"
static lv_subject_t brightness_subject; // backlight %, kBrightnessMin..MaxPercent

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
