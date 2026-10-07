#pragma once
// The Joystick test screen's live readouts: the three axis bars and the GPIO48 button's count.

#include "lvgl.h"

namespace hmi::ui {

/// One instance. Owns the readouts' subjects (CS-UI-05): the calibrated axes as percentages
/// (-100..+100, centred at 0), the stick button's press count and its pressed level. The ADC
/// task writes the axes under a try_lock of lvgl_mutex; the button's interrupt task writes the
/// count and the level under lvgl_mutex. Both writers go through the accessors.
class JoystickView {
public:
  constexpr JoystickView() noexcept = default;

  /// @brief Initialises the three axis subjects to 0, sets the bars' range to -100..+100 and
  ///        binds them, in that order (X, Y, twist each time). app_main, before lv_task starts.
  void init_bars();
  /// @brief Initialises the count and the pressed level to 0, binds the count label ("%d")
  ///        and the observer that paints its colour from the level. app_main, before the
  ///        button's interrupt task starts (it writes them).
  void init_button();

  /// int: the calibrated X, Y and twist, percent.
  [[nodiscard]] lv_subject_t *x() { return &x_; }
  [[nodiscard]] lv_subject_t *y() { return &y_; }
  [[nodiscard]] lv_subject_t *twist() { return &twist_; }
  /// int: the button's press count (debounced), and its level (every edge).
  [[nodiscard]] lv_subject_t *count() { return &count_; }
  [[nodiscard]] lv_subject_t *pressed() { return &pressed_; }

private:
  // The count's colour: there's no built-in binding for a style property. UI task, or the
  // button's task under lvgl_mutex.
  static void pressed_observer(lv_observer_t *observer, lv_subject_t *subject);

  lv_subject_t x_{};
  lv_subject_t y_{};
  lv_subject_t twist_{};
  lv_subject_t count_{};
  lv_subject_t pressed_{};
};

} // namespace hmi::ui
