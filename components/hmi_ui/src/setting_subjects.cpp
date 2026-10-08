#include "hmi_ui/setting_subjects.hpp"

#include <cstdint>

#include "settings.hpp"
#include "ui.h"

namespace hmi::ui {

void SettingSubjects::store_cb(lv_observer_t *observer, lv_subject_t *subject) {
  const Store *store = static_cast<const Store *>(lv_observer_get_user_data(observer));
  store->self->store_(store->param, lv_subject_get_int(subject));
}

void SettingSubjects::init(Fn<void(int param, int32_t value)> store) {
  store_ = store;
  // Theme: the row switches the UI's palette; rtps_poll_cb notices the switch
  // (however it was made) and saves it, and keeps this subject in step.
  lv_subject_init_int(value(SETTINGS_PARAM_THEME), ui_theme_idx == UI_THEME_DAY ? 1 : 0);
  lv_subject_add_observer(
      value(SETTINGS_PARAM_THEME),
      [](lv_observer_t *, lv_subject_t *subject) {
        const uint8_t want = lv_subject_get_int(subject) != 0 ? UI_THEME_DAY : UI_THEME_DEFAULT;
        if (ui_theme_idx != want) {
          ui_theme_set(want);
        }
      },
      nullptr);
  // The rest of Settings: each row's subject starts at its saved value, and
  // UiApp's store_setting applies it (on this first run too, which is what
  // puts a saved flip or stick mapping back at boot) and saves any change.
  for (const int param :
       {SETTINGS_PARAM_MENU_SLIDE, SETTINGS_PARAM_FLIP, SETTINGS_PARAM_STICK_SENSITIVITY,
        SETTINGS_PARAM_DRIVE_SPEED, SETTINGS_PARAM_STICK_INVERT_X, SETTINGS_PARAM_STICK_INVERT_Y,
        SETTINGS_PARAM_STICK_SWAP, SETTINGS_PARAM_SOUNDS, SETTINGS_PARAM_NETWORK}) {
    lv_subject_init_int(value(param), settings_get(param));
    stores_[static_cast<size_t>(param)] = Store{.self = this, .param = param};
    lv_subject_add_observer(value(param), store_cb, &stores_[static_cast<size_t>(param)]);
  }
}

} // namespace hmi::ui
