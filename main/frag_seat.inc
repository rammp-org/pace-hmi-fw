// split_main.py fragment of main.cpp (frag_seat: original lines 1833-2046); one TU, see main.cpp.
#include "hmi_models/grid.hpp"
#include "hmi_ui/button_grid.hpp"
using hmi::ui::ButtonGrid;
using hmi::ui::grid_key_cb;
using hmi::ui::grid_sync_cursor;

// Down off any grid is the burger key; left off the adjustment page's first
// column is "back", as "<" is.
static ButtonGrid seat_buttons_grid{.off_bottom = nav_to_key};
static ButtonGrid seat_adjust_grid{
    .left_edge_is_back = true, .off_bottom = nav_to_key, .off_left = seat_show_buttons_page};

static_assert(kGridMaxRows == hmi::ui::GRID_MAX_ROWS && kGridMaxCols == hmi::ui::GRID_MAX_COLS,
              "ButtonGrid and the cursor model must agree on the grid's size");

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

// Which seat function the SeatAdjustmentPanel is showing. A subject rather than
// an lv_label_set_text from the click handler, so the label is reached the same
// way as every other piece of UI state (see CLAUDE.md). Buffers are static for
// the same reason the subject is — the subject stores its copy in them, and both
// have to outlive the label bound to them.
static char seat_function_buf[24];
static char seat_function_prev_buf[24];
static_assert(sizeof(seat_function_buf) > sizeof("Elevation"), "label buffer too small");
static lv_subject_t seat_function_subject;

// Which seat axis each function button adjusts. The buttons take the rows of
// RAMMP_SEAT_AXIS_TABLE in order, so adding a row to the table is all it takes
// to give the next button a job; -1 is a button with no row behind it, which
// today is the Static/Dynamic pair on the third row.
static constexpr int seat_button_axis(int row, int col) {
  const int index = row * 2 + col;
  return row < 2 && index < static_cast<int>(rammp::kSeatAxisCount) ? index : -1;
}
// The axis the adjustment page is showing; -1 until a function button is pressed.
static int seat_selected_axis = -1;

// All three defined with the settings rows further down, which own the seat
// values and the path that asks the MCB to move one.
static void seat_step(size_t row, int direction);
static void seat_request(rammp::SeatAxis axis, int32_t target);
static void seat_angle_refresh();

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

// A press on the adjustment page. "-" and "+" move the selected axis one step
// from where the MCB last said it is; the three presets send the angle written
// on their own label. Every one of them is an absolute target, so a preset and a
// step are the same message and a lost one cannot leave the seat drifting.
//
// The preset reads its number off the label rather than from a table here, so
// relabelling the button in SquareLine changes what it asks for with nothing in
// the firmware to keep in step. The number is in the axis' display units - "15"
// on a one-decimal axis is 15.0 degrees, raw 150.
static int32_t seat_preset_target(size_t row, const lv_obj_t *label) {
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[row];
  const char *text = lv_label_get_text(label);
  int32_t whole = 0;
  const bool negative = text != nullptr && *text == '-';
  for (const char *c = negative ? text + 1 : text; c != nullptr && *c >= '0' && *c <= '9'; c++) {
    whole = whole * 10 + (*c - '0');
  }
  for (uint8_t i = 0; i < spec.decimals; i++) {
    whole *= 10;
  }
  return negative ? -whole : whole;
}

// Takes focus, then acts. Nothing happens before a function button has picked an
// axis: the page can only be reached through one, but the cursor is not proof of
// that on its own.
static void grid_click_cb(lv_event_t *e) {
  auto *button = lv_event_get_target_obj(e);
  grid_sync_cursor(static_cast<ButtonGrid *>(lv_event_get_user_data(e)), button);
  if (seat_selected_axis < 0) {
    return;
  }
  const auto row = static_cast<size_t>(seat_selected_axis);
  if (button == ui_SeatAdjustmentButton1 || button == ui_SeatAdjustmentButton2) {
    seat_step(row, button == ui_SeatAdjustmentButton1 ? -1 : +1);
    return;
  }
  // Named rather than reached with lv_obj_get_child: each button holds a
  // container, and the label is inside that.
  const lv_obj_t *label = button == ui_SeatAdjustmentButton3   ? ui_SeatAdjustmentButtonLabel3
                          : button == ui_SeatAdjustmentButton4 ? ui_SeatAdjustmentButtonLabel4
                          : button == ui_SeatAdjustmentButton5 ? ui_SeatAdjustmentButtonLabel5
                                                               : nullptr;
  if (label == nullptr) {
    return; // a preset button the export no longer has
  }
  seat_request(rammp::kSeatAxes[row].id, seat_preset_target(row, label));
}

// Both a tap and the joystick button land here: with seat_group owning the
// indev, LVGL raises LV_EVENT_CLICKED on the focused button for an ENTER key
// exactly as it does for a touch release.
//
// user_data is the button's own label for the four live buttons and null for the
// inert pair, so those only take focus.
static void seat_click_cb(lv_event_t *e) {
  lv_obj_t *button = lv_event_get_target_obj(e);
  grid_sync_cursor(&seat_buttons_grid, button);

  auto *label = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  const int axis = seat_button_axis(seat_buttons_grid.row, seat_buttons_grid.col);
  if (!label || axis < 0) {
    return; // Static and Dynamic, and any button with no row in the table
  }
  lv_subject_copy_string(&seat_function_subject, lv_label_get_text(label));
  // What the adjustment page's buttons and its two numbers now refer to.
  seat_selected_axis = axis;
  seat_angle_refresh();

  // Spec 04b: the motion's own page, over the function buttons. The joystick
  // moves across with it -- its own group -- onto "-" rather than the back
  // button above, since adjusting is what the page is for.
  lv_obj_remove_flag(ui_SeatAdjustmentPanel, LV_OBJ_FLAG_HIDDEN);
  nav_use_group(seat_adjust_group, ui_SeatScreen);
  seat_adjust_grid.row = 1;
  seat_adjust_grid.col = 0;
  lv_group_focus_obj(seat_adjust_grid.cell[1][0]);
  seat_page = 1;
}

// Declared up with the seat grids, which are what call it. Also the back
// button's, and the screen's arrival: the page left up on the last visit must
// not greet the next one, or the stick's cursor sits on the function buttons
// hidden underneath it.
static void seat_show_buttons_page() {
  lv_obj_add_flag(ui_SeatAdjustmentPanel, LV_OBJ_FLAG_HIDDEN);
  seat_page = 0;
  nav_use_group(seat_group, ui_SeatScreen);
  // Back to the button the user selected rather than the top-left one: the
  // grid's cursor still holds it, and returning to where you were is less
  // jarring than being bounced to the corner.
  lv_group_focus_obj(seat_buttons_grid.cell[seat_buttons_grid.row][seat_buttons_grid.col]);
}

