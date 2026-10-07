#pragma once
// The backlight level: one subject, whoever changes it.

#include <cstdint>

#include "lvgl.h"

namespace hmi::ui {

/// The backlight in percent, as a subject: the RTPS brightness command, the Tab5's side button
/// and the Settings Brightness row all set it. Its observer drives the backlight on every
/// change, and saves the level once it has stopped changing for the save delay, so a run of
/// steps is one flash write rather than one per step.
class BrightnessView {
public:
  struct Config {
    lv_subject_t *subject;            ///< main's brightness_subject (the Settings row steps it)
    int min_percent;                  ///< the lowest level `set` gives; above 0
    int max_percent;                  ///< the highest level `set` gives
    void (*backlight)(float percent); ///< the board's backlight call; any task
    void (*save)(int percent);        ///< persists the level (settings); UI task
  };

  constexpr explicit BrightnessView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Initialises the subject at `percent` and adds the observer, which applies it at
  ///        once. The save timer does not exist yet, so this first run saves nothing.
  /// @param percent the saved level
  /// app_main, before lv_task starts.
  void init(int32_t percent);
  /// @brief Creates the save timer, paused until the first change after this.
  /// @param delay_ms how long the level must stay unchanged before it is saved
  /// app_main, after `init`, before lv_task starts.
  void start_save_timer(uint32_t delay_ms);
  /// @brief Sets the level, clamped to `min_percent`..`max_percent`, so nothing can turn the
  ///        screen fully off.
  /// Any task, with lvgl_mutex held by the caller (main's brightness_set: the RTPS receive
  /// task).
  void set(int percent);
  /// @brief Steps to the next of 25/50/75/100 % above the current level, wrapping to 25.
  /// Any task, with lvgl_mutex held by the caller (main's brightness_step: the side-button
  /// task).
  void step();

private:
  // Runs on the setter's task, under lvgl_mutex (observers run synchronously).
  static void observer_cb(lv_observer_t *observer, lv_subject_t *subject);
  // UI task (an LVGL timer).
  static void save_cb(lv_timer_t *timer);

  Config config_;
  lv_timer_t *save_timer_ = nullptr; ///< null until start_save_timer
};

} // namespace hmi::ui
