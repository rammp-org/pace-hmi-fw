#pragma once
// The BenchGateScreen: a 4-digit PIN in front of the bench tools.

#include <array>
#include <cstddef>
#include <string_view>

#include "lvgl.h"

#include "hmi_models/pin.hpp"
#include "hmi_ui/button_grid.hpp"

namespace hmi::ui {

/// The PIN pad (eleven buttons: 1-9, 0, backspace), the four dots above it and the line over
/// them. The digits and the verdict are the PinModel's (hmi_models); this view draws them and
/// walks the joystick over the pad with a ButtonGrid. The PIN is a "not by accident" barrier
/// in front of a bench screen, not a secret, and nothing here is safety-relevant.
class BenchPinView {
public:
  static constexpr int PIN_LEN = PinModel::PIN_LEN; ///< the screen draws exactly four dots
  static constexpr int KEY_BACK = -1;               ///< the backspace key, in DIGITS
  /// The pad as drawn: three rows of three, then 0 and backspace under the middle and right
  /// columns (the bottom-left cell is a hole). The digit each key types, by grid cell.
  static constexpr int DIGITS[GRID_MAX_ROWS][GRID_MAX_COLS] = {
      {1, 2, 3},
      {4, 5, 6},
      {7, 8, 9},
      {0, 0, KEY_BACK},
  };
  static constexpr char PROMPT_TEXT[] = "Enter PIN to proceed";
  static constexpr char WRONG_TEXT[] = "Incorrect PIN - try again";

  struct Config {
    std::string_view pin;             ///< the PIN (a build constant); PIN_LEN digits
    void (*accepted)();               ///< the right PIN: main opens the actuators page
    void (*off_bottom)();             ///< down off the pad: main's nav_to_key
    void (*style_key)(lv_obj_t *key); ///< main's focus ring and pressed look, for each key
  };

  constexpr explicit BenchPinView(const Config &config) noexcept
      : config_(config)
      , grid_{.off_bottom = config.off_bottom}
      , pin_({.pin = config.pin}) {}

  /// @brief Wires the screen: the line and the dots to their subjects, and the pad's keys to
  ///        the grid, a new group and the keypad callback.
  /// @return the pad's group, for the joystick
  /// app_main, before lv_task starts.
  lv_group_t *init();
  /// @brief Empties the entry and puts the prompt back (every visit to the screen).
  /// UI task.
  void reset();
  /// @brief Puts the cursor and the focus on one key, by grid index (row * GRID_MAX_COLS +
  ///        column), so the first joystick nudge moves between keys.
  /// @param index the key
  /// UI task.
  void focus(int index);

private:
  static constexpr size_t MESSAGE_SIZE = 32;
  static_assert(MESSAGE_SIZE > sizeof(WRONG_TEXT), "label buffer too small");

  // LV_EVENT_CLICKED on a key (touch, or the stick button through the group); user_data is
  // the view. UI task.
  static void keypad_cb(lv_event_t *e);
  void draw_empty();

  Config config_;
  ButtonGrid grid_;
  PinModel pin_;
  lv_subject_t len_subject_{};     ///< int: digits typed; the dots are bound to it
  lv_subject_t message_subject_{}; ///< string: the line above the dots
  std::array<char, MESSAGE_SIZE> message_buf_{};
  std::array<char, MESSAGE_SIZE> message_prev_buf_{};
};

} // namespace hmi::ui
