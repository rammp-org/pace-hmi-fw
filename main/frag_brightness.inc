// split_main.py fragment of main.cpp (frag_brightness: original lines 718-766); one TU, see main.cpp.
#include "hmi_ui/brightness_view.hpp"

// The one instance. Its subject is the Settings row's (SettingSubjects): the row steps it too.
static constinit hmi::ui::BrightnessView brightness_view{{
    .subject = setting_subjects.value(SETTINGS_PARAM_BRIGHTNESS),
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
