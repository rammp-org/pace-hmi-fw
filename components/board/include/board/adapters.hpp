#pragma once
// The callbacks the BSP's own tasks run (app-main-shrink §3): the touch click and the side
// button. Moved from main.cpp's app_main wiring; each stays on the task that calls it.

#include <optional>

#include "logger.hpp"
#include "m5stack-tab5.hpp"

namespace hmi::board {

/// @brief The touch adapter, on the BSP's touch task: a click on each press (a release and a
/// new touch before it clicks again), and every change logged at debug level.
///
/// NOTE: since we're directly using the touchpad data, and not using the TouchpadInput +
/// LVGL, we'll need to ensure the touchpad data is converted into proper screen coordinates
/// instead of simply using the raw values.
class TouchClick {
public:
  struct Config {
    espp::M5StackTab5 &tab5; ///< The BSP, whose touchpad_convert maps a raw touch.
    espp::Logger &log;       ///< Where each change is logged (debug level).
    void (*click)();         ///< The click (main: play_click). Not null.
  };

  /// @brief Stores the Config; touches nothing.
  /// @param config See Config.
  explicit TouchClick(const Config &config);

  /// @brief One touchpad report, on the BSP's touch task.
  /// @param touch The raw report.
  void operator()(const espp::TouchpadData &touch);

private:
  espp::M5StackTab5 &tab5_;
  espp::Logger &logger_;
  void (*click_)();
  std::optional<espp::TouchpadData> previous_touchpad_data_;
  bool was_pressed_ = false;
};

/// @brief The side button's adapter, on the BSP's button task: brightness control.
struct SideButton {
  espp::Logger &logger; ///< Where each edge is logged.
  void (*on_press)();   ///< A press (main: brightness_step). Not null.

  /// @brief One edge of the button.
  /// @param state The edge; `active` is pressed.
  void operator()(const espp::Interrupt::Event &state) const {
    logger.info("Button state: {}", state.active);
    if (state.active) {
      on_press();
    }
  }
};

} // namespace hmi::board
