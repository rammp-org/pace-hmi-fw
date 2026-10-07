#pragma once
// Characterisation oracle: the ButtonGrid cursor walk and the bench PIN entry exactly as main/
// had them before they moved to components/hmi_models (dev_refactor ad152dd). legacy_ui.cpp
// holds verbatim copies of their bodies, with the LVGL calls replaced by recording stubs, so
// the golden tables in goldens.hpp record what the firmware did. Test-only code: nothing in the
// firmware links it.

#include <cstdint>

#include "shapes.hpp"

namespace legacy {

/// What one arrow key did to a grid, as the pre-move grid_key_cb left it.
struct KeyResult {
  models_test::Move move; ///< MOVED, or which edge handler ran instead
  int row;                ///< the grid's cursor afterwards
  int col;
  bool focused; ///< lv_group_focus_obj ran (on the cell at row, col)
};

/// One arrow key on one of the firmware's three grids, built as app_main builds them. The
/// grid's cursor is set to (row, col) first and focus is on that cell, or on an object outside
/// the grid when the cell is a hole or outside the shape. 0 <= row < 4, 0 <= col < 3.
KeyResult grid_key(models_test::GridId grid, int row, int col, models_test::Key key);

/// The shape of one of the firmware's grids, read back from the ButtonGrid app_main builds.
models_test::Shape grid_shape(models_test::GridId grid);

/// The same walk on any shape: a ButtonGrid is filled with one stand-in button per present
/// cell. `shape.left_edge_is_back` decides whether the walk runs on seat_adjust_grid (the grid
/// whose left edge is "back") or on another grid.
KeyResult grid_key_on(const models_test::Shape &shape, int row, int col, models_test::Key key);

/// The PIN pad's state after a key, as the pre-move code left its two subjects.
struct PinState {
  int dots;         ///< rd_pin_len_subject
  bool wrong;       ///< rd_pin_message_subject reads kRdPinWrongText (else kRdPinPromptText)
  int pages_opened; ///< setting_page_open(kActuatorsPage) calls since the last pin_reset
  int first_dots;   ///< the first value this key wrote to rd_pin_len_subject, -1 if none
};

/// rd_pin_reset, as nav_enter_screen calls it on each visit (and at start-up state).
void pin_reset();
/// A digit key (0..9) or kRdBack (-1) on the pad: rd_keypad_cb.
PinState pin_key(int digit_or_back);
/// The state now, without a key.
PinState pin_state();

/// The PIN the pre-move code compared against: kRdPin.
const char *pin_constant();

} // namespace legacy
