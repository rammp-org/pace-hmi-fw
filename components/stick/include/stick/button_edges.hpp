#pragma once
// hmi::stick::ButtonEdges: what one edge of the stick button (GPIO48) means. Pure logic: the
// caller passes the edge and the time; no LVGL, no ESP-IDF.
//
// Extracted from main/frag_stick_button.inc (stick_button_edge), refactor only: the same
// comparisons in the same order. The caller keeps the store order of that function (H14):
// the pressed subject, then joy_button_pressed, then this, then the counter.

#include <cstdint>

namespace hmi::stick {

/// A press shorter than this selects on its release (the keypad's ENTER). It equals a hold's
/// grace period, so a press short enough to select never starts a hold filling, and one long
/// enough to fill never selects (main static_asserts the two are equal).
inline constexpr int64_t SELECT_MAX_US = 500 * 1000;
/// The press counter's debounce: a press within this of the last counted one is not counted.
/// espp's GPIO glitch filters are ns-scale and do nothing against millisecond-scale bounce.
inline constexpr int64_t COUNT_DEBOUNCE_US = 30000;

/// The edges of one button. One instance, owned by the button's interrupt task.
class ButtonEdges {
public:
  /// What one edge asks the caller to do.
  struct Effect {
    bool select; ///< a release that ends a short press: latch the select key
    bool count;  ///< a press that counts: add one to the press counter
  };

  /// @brief One edge at `now_us`. A press restarts the select window; a release inside it
  ///        selects. Every press more than COUNT_DEBOUNCE_US after the last counted one
  ///        counts (the counter alone is debounced).
  /// The button's interrupt task.
  [[nodiscard]] constexpr Effect edge(bool active, int64_t now_us) noexcept {
    Effect effect{.select = false, .count = false};
    if (active) {
      pressed_at_us_ = now_us;
    } else if (now_us - pressed_at_us_ < SELECT_MAX_US) {
      effect.select = true;
    }
    if (active && (now_us - last_press_us_) > COUNT_DEBOUNCE_US) {
      last_press_us_ = now_us;
      effect.count = true;
    }
    return effect;
  }

private:
  int64_t pressed_at_us_ = 0; ///< the last press edge
  int64_t last_press_us_ = 0; ///< the last counted press
};

} // namespace hmi::stick
