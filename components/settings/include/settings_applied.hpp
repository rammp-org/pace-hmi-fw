#pragma once
/// @file settings_applied.hpp
/// @brief The settings other tasks read without the LVGL lock, as atomics (moved from
///        main/frag_state.inc and setting_store_observer).

#include <atomic>

#include "settings_spec.hpp"

namespace hmi::settings {

/// @brief What the tasks outside the UI read of the settings: the stick's sensitivity, drive
/// speed and mapping (the Read ADC task, every cycle), and the sounds (the touch task and the
/// LVGL task, on every click). Written by apply() from the Settings rows' observer on the UI
/// task, which also runs once per row at boot with the saved value. One instance, main's
/// (constinit: no global constructor).
class AppliedSettings {
public:
  constexpr AppliedSettings() noexcept = default;

  /// @brief Stores @p value when @p param is one of the settings this holds.
  /// @param param A SETTINGS_PARAM_*.
  /// @param value The row's new value.
  /// @return Whether @p param is one of them (MENU_SLIDE, FLIP, THEME... are not).
  /// UI task.
  bool apply(int param, int value) noexcept;

  /// Settings "Stick sensitivity", SETTINGS_STICK_SENSITIVITY_MIN..MAX. Any task.
  [[nodiscard]] int stick_sensitivity() const noexcept { return stick_sensitivity_.load(); }
  /// Settings "Speed sensitivity", SETTINGS_DRIVE_SPEED_MIN..MAX (tenths). Any task.
  [[nodiscard]] int drive_speed() const noexcept { return drive_speed_.load(); }
  /// Left and right swap over. Any task.
  [[nodiscard]] bool stick_invert_x() const noexcept { return stick_invert_x_.load(); }
  /// Forward and back swap over. Any task.
  [[nodiscard]] bool stick_invert_y() const noexcept { return stick_invert_y_.load(); }
  /// The stick's X and Y trade places. Any task.
  [[nodiscard]] bool stick_swap() const noexcept { return stick_swap_.load(); }
  /// Settings "Sounds": false = the clicks and refusals are silent. The click sound reads it
  /// on every play.
  [[nodiscard]] const std::atomic<bool> &sounds_on() const noexcept { return sounds_on_; }

private:
  std::atomic<int> stick_sensitivity_{SETTINGS_STICK_SENSITIVITY_MAX - 1};
  std::atomic<int> drive_speed_{SETTINGS_DRIVE_SPEED_MAX};
  std::atomic<bool> stick_invert_x_{false};
  std::atomic<bool> stick_invert_y_{false};
  std::atomic<bool> stick_swap_{false};
  std::atomic<bool> sounds_on_{true};
};

} // namespace hmi::settings
