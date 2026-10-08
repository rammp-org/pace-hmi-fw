#pragma once
// The Settings rows' subjects (moved from main/frag_state.inc, frag_brightness.inc and app_main's
// settings wiring).

#include <array>
#include <cstddef>
#include <cstdint>

#include "lvgl.h"

#include "settings_spec.hpp"

#include "hmi_ui/fn.hpp"

namespace hmi::ui {

/// @brief One subject per settings_spec.hpp parameter, in SETTINGS_PARAM_* order: what each
/// Settings row shows and steps (CS-UI-05). Whoever owns a setting observes its subject and
/// applies and saves it: the backlight is BrightnessView's, the theme is applied here, and the
/// rest go through UiApp's store_setting. One instance (app_state's), built at compile time.
class SettingSubjects {
public:
  constexpr SettingSubjects() noexcept = default;

  /// @brief The subject of @p param (a SETTINGS_PARAM_*), raw units.
  [[nodiscard]] constexpr lv_subject_t *value(int param) {
    return &values_[static_cast<size_t>(param)];
  }

  /// @brief Initialises every subject but the backlight's (BrightnessView::init does that)
  ///        from the saved settings, and adds their observers: the theme's here (it switches
  ///        the UI's palette), every other one an observer that calls @p store with its
  ///        SETTINGS_PARAM_* and value, which applies it on this first run too and saves any
  ///        change.
  /// @param store UiApp's store_setting (it outlives this).
  /// app_main, after settings_load and the theme's first application, before anything binds.
  void init(Fn<void(int param, int32_t value)> store);

private:
  /// One store observer's user data: which setting it is.
  struct Store {
    SettingSubjects *self;
    int param;
  };
  // Runs `store_` for the observer's setting. UI task, or app_main during init.
  static void store_cb(lv_observer_t *observer, lv_subject_t *subject);

  lv_subject_t values_[SETTINGS_PARAM_COUNT]{};
  std::array<Store, SETTINGS_PARAM_COUNT> stores_{};
  Fn<void(int param, int32_t value)> store_;
};

} // namespace hmi::ui
