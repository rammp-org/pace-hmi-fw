// ButtonGrid: the joystick's walk over a page of buttons (moved from main/frag_seat.inc).

#include "hmi_ui/button_grid.hpp"

#include <cstddef>

// The grid as the cursor model sees it: row lengths, and a hole where a cell is
// null.
hmi::ui::GridShape hmi::ui::grid_shape_of(const ButtonGrid *g) {
  hmi::ui::GridShape shape{};
  shape.rows = g->rows;
  shape.left_edge_is_back = g->left_edge_is_back;
  for (size_t r = 0; r < shape.cols.size(); r++) {
    shape.cols[r] = g->cols[r];
    for (size_t c = 0; c < shape.button[r].size(); c++) {
      shape.button[r][c] = g->cell[r][c] != nullptr;
    }
  }
  return shape;
}

// Arrow keys arrive here as LV_EVENT_KEY on the focused button: lv_indev only
// consumes NEXT/PREV/ENTER/ESC itself and passes everything else through
// lv_group_send_data. That is what lets a grid do its own 2D movement.
//
// user_data is the grid the button belongs to, so one callback serves every
// grid. Leaving the screen is a menu row now, so LV_KEY_LEFT only moves the
// cursor.
void hmi::ui::grid_key_cb(lv_event_t *e) {
  auto *g = static_cast<ButtonGrid *>(lv_event_get_user_data(e));
  // The cursor is whichever cell holds focus, not wherever it was last left:
  // coming back up from the burger key, the group put focus on a cell without
  // telling the grid.
  const lv_obj_t *here = lv_event_get_target_obj(e);
  for (int r = 0; r < g->rows; r++) {
    for (int c = 0; c < g->cols[r]; c++) {
      if (g->cell[r][c] == here) {
        g->row = r;
        g->col = c;
      }
    }
  }
  hmi::ui::GridKey key{};
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    key = hmi::ui::GridKey::UP;
    break;
  case LV_KEY_DOWN:
    key = hmi::ui::GridKey::DOWN;
    break;
  case LV_KEY_LEFT:
    key = hmi::ui::GridKey::LEFT;
    break;
  case LV_KEY_RIGHT:
    key = hmi::ui::GridKey::RIGHT;
    break;
  default:
    return;
  }
  // Clamped rather than wrapped, and a hole is stepped over: see grid_step.
  const hmi::ui::GridStep step = hmi::ui::grid_step(grid_shape_of(g), {g->row, g->col}, key);
  switch (step.move) {
  case hmi::ui::GridMove::OFF_BOTTOM:
    g->off_bottom(); // below the bottom row is the burger key
    return;
  case hmi::ui::GridMove::OFF_LEFT:
    g->off_left();
    return;
  case hmi::ui::GridMove::MOVED:
    break;
  }
  g->row = step.cursor.row;
  g->col = step.cursor.col;
  if (step.on_button) {
    lv_group_focus_obj(g->cell[g->row][g->col]);
  }
}

// Puts the cursor on `button`, so joystick movement carries on from wherever a
// finger last landed instead of from where the joystick left off.
void hmi::ui::grid_sync_cursor(ButtonGrid *g, lv_obj_t *button) {
  for (int r = 0; r < g->rows; r++) {
    for (int c = 0; c < g->cols[r]; c++) {
      if (g->cell[r][c] != nullptr && g->cell[r][c] == button) {
        g->row = r;
        g->col = c;
      }
    }
  }
  lv_group_focus_obj(button);
}
