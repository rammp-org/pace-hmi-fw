#pragma once
// The Settings rows' subjects (moved from main/frag_state.inc, frag_brightness.inc and app_main's
// settings wiring).

#include <cstddef>

#include "lvgl.h"

#include "settings_spec.hpp"

namespace hmi::ui {

/// @brief One subject per settings_spec.hpp parameter, in SETTINGS_PARAM_* order: what each
/// Settings row shows and steps (CS-UI-05). Whoever owns a setting observes its subject and
/// applies and saves it: the backlight is BrightnessView's, the theme is applied here, and the
/// rest go through main's setting_store_observer. One instance, main's, built at compile time.
class SettingSubjects {
public:
  constexpr SettingSubjects() noexcept = default;

  /// @brief The subject of @p param (a SETTINGS_PARAM_*), raw units.
  [[nodiscard]] constexpr lv_subject_t *value(int param) {
    return &values_[static_cast<size_t>(param)];
  }

  /// @brief Initialises every subject but the backlight's (BrightnessView::init does that)
  ///        from the saved settings, and adds their observers: the theme's here (it switches
  ///        the UI's palette), every other one @p store_observer with its SETTINGS_PARAM_* as
  ///        user data, which applies it on this first run too and saves any change.
  /// @param store_observer main's setting_store_observer.
  /// app_main, after settings_load and the theme's first application, before anything binds.
  void init(lv_observer_cb_t store_observer);

private:
  lv_subject_t values_[SETTINGS_PARAM_COUNT]{};
};

} // namespace hmi::ui
