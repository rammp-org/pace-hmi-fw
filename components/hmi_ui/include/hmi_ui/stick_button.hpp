#pragma once
// The stick's own button (GPIO48) as the UI sees it: the Joystick screen's pressed panel and
// press counter, the level the ADC task publishes, and the select key a short press latches.

#include <atomic>

#include "lvgl.h"

#include "stick/button_edges.hpp"

#include "hmi_ui/joystick_view.hpp"

namespace hmi::ui {

/// One instance (main's, constinit). `edge` runs on the button's interrupt task (espp's
/// "Button" task), with lvgl_mutex held by main's caller: it writes subjects, whose observers
/// touch widgets. No lock here (CS-OWN-08: the caller keeps it).
class StickButton {
public:
  struct Config {
    JoystickView *view;        ///< the pressed panel and the press counter's subjects
    std::atomic<bool> *level;  ///< main's joy_button_pressed: the raw level, for the ADC task
    std::atomic<bool> *select; ///< main's select_key: one ENTER for the keypad read
  };

  constexpr explicit StickButton(const Config &config) noexcept
      : config_(config) {}

  /// @brief One edge of the button. In this order (H14): the pressed subject, the level, then
  ///        what the edge means (hmi::stick::ButtonEdges) at the time read now: the select key
  ///        on a short press's release, one more on the counter on a counted press.
  /// The button's interrupt task (or the remote UI's), lvgl_mutex held.
  void edge(bool active);

private:
  Config config_;
  hmi::stick::ButtonEdges edges_; ///< the select window and the counter's debounce
};

} // namespace hmi::ui
