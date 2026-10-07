#pragma once
// A page of buttons the joystick walks as the grid the user sees.

#include "lvgl.h"

#include "hmi_models/grid.hpp"

namespace hmi::ui {

/// A page's focusable buttons in visual order, plus where the cursor is. The cursor's movement
/// is grid_step (hmi_models); a ButtonGrid is its view: the buttons, and the cursor the focus
/// calls follow. Cells are filled in at wiring time, since the ui_* globals are null until
/// ui_init runs. A cell may be null where the layout has a hole (the PIN pad's bottom row
/// starts at the middle column), and the cursor steps over those.
struct ButtonGrid {
  lv_obj_t *cell[GRID_MAX_ROWS][GRID_MAX_COLS]{};
  int cols[GRID_MAX_ROWS]{}; ///< buttons in each row; rows may differ in length
  int rows = 0;
  int row = 0; ///< cursor
  int col = 0;
  bool left_edge_is_back = false; ///< left from the first column leaves the page (off_left)
  void (*off_bottom)() = nullptr; ///< down from the bottom row; must be set (main: nav_to_key)
  void (*off_left)() = nullptr;   ///< left off the edge; set when left_edge_is_back
};

/// @brief The grid as the cursor model sees it: row lengths, and a hole where a cell is null.
/// @param g the grid
/// @return its shape
GridShape grid_shape_of(const ButtonGrid *g);

/// @brief LV_EVENT_KEY on a grid's button; user_data is the ButtonGrid. Arrow keys move the
///        cursor (clamped, holes stepped over), and leaving the grid calls off_bottom or
///        off_left. Other keys are left to LVGL.
/// @param e the event
/// UI task (LVGL event).
void grid_key_cb(lv_event_t *e);

/// @brief Puts the cursor on `button`, so joystick movement carries on from wherever a finger
///        last landed, and focuses it.
/// @param g the grid holding the button
/// @param button the button
/// UI task.
void grid_sync_cursor(ButtonGrid *g, lv_obj_t *button);

} // namespace hmi::ui
