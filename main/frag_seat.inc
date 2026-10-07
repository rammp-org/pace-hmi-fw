// split_main.py fragment of main.cpp (frag_seat: original lines 1833-2046); one TU, see main.cpp.
#include "hmi_models/grid.hpp"
#include "hmi_ui/button_grid.hpp"
#include "hmi_ui/seat_view.hpp"
using hmi::ui::ButtonGrid;
using hmi::ui::grid_key_cb;
using hmi::ui::grid_sync_cursor;

static_assert(kGridMaxRows == hmi::ui::GRID_MAX_ROWS && kGridMaxCols == hmi::ui::GRID_MAX_COLS,
              "ButtonGrid and the cursor model must agree on the grid's size");

// Which seat axis each function button adjusts. The buttons take the rows of
// RAMMP_SEAT_AXIS_TABLE in order, so adding a row to the table is all it takes
// to give the next button a job; -1 is a button with no row behind it, which
// today is the Static/Dynamic pair on the third row.
constexpr int hmi::ui::SeatView::button_axis(int row, int col) {
  const int index = row * 2 + col;
  return row < 2 && index < static_cast<int>(rammp::kSeatAxisCount) ? index : -1;
}

// Both defined with the settings rows further down, which own the seat values
// and the path that asks the MCB to move one.
static void seat_step(size_t row, int direction);
static void seat_request(rammp::SeatAxis axis, int32_t target);

// A press on the adjustment page. "-" and "+" move the selected axis one step
// from where the MCB last said it is; the three presets send the angle written
// on their own label. Every one of them is an absolute target, so a preset and a
// step are the same message and a lost one cannot leave the seat drifting.
//
// The preset reads its number off the label rather than from a table here, so
// relabelling the button in SquareLine changes what it asks for with nothing in
// the firmware to keep in step. The number is in the axis' display units - "15"
// on a one-decimal axis is 15.0 degrees, raw 150.
int32_t hmi::ui::SeatView::preset_target(size_t row, const lv_obj_t *label) {
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
void hmi::ui::SeatView::adjust_click_cb(lv_event_t *e) {
  auto *view = static_cast<SeatView *>(lv_event_get_user_data(e));
  auto *button = lv_event_get_target_obj(e);
  grid_sync_cursor(&view->adjust_grid_, button);
  if (view->selected_axis_ < 0) {
    return;
  }
  const auto row = static_cast<size_t>(view->selected_axis_);
  if (button == ui_SeatAdjustmentButton1 || button == ui_SeatAdjustmentButton2) {
    view->config_.step(row, button == ui_SeatAdjustmentButton1 ? -1 : +1);
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
  view->config_.request(rammp::kSeatAxes[row].id, preset_target(row, label));
}

// Both a tap and the joystick button land here: with the buttons' group owning
// the indev, LVGL raises LV_EVENT_CLICKED on the focused button for an ENTER key
// exactly as it does for a touch release.
//
// The label is the button's own for the four live buttons and null for the
// inert pair, so those only take focus.
void hmi::ui::SeatView::click_cb(lv_event_t *e) {
  auto *view = static_cast<SeatView *>(lv_event_get_user_data(e));
  lv_obj_t *button = lv_event_get_target_obj(e);
  grid_sync_cursor(&view->buttons_grid_, button);

  lv_obj_t *label = view->labels_[view->buttons_grid_.row][view->buttons_grid_.col];
  const int axis = button_axis(view->buttons_grid_.row, view->buttons_grid_.col);
  if (!label || axis < 0) {
    return; // Static and Dynamic, and any button with no row in the table
  }
  lv_subject_copy_string(&view->function_subject_, lv_label_get_text(label));
  // What the adjustment page's buttons and its two numbers now refer to.
  view->selected_axis_ = axis;
  view->angle_refresh();

  // Spec 04b: the motion's own page, over the function buttons. The joystick
  // moves across with it -- its own group -- onto "-" rather than the back
  // button above, since adjusting is what the page is for.
  lv_obj_remove_flag(ui_SeatAdjustmentPanel, LV_OBJ_FLAG_HIDDEN);
  view->config_.nav->use_group(view->adjust_group_, ui_SeatScreen);
  view->adjust_grid_.row = 1;
  view->adjust_grid_.col = 0;
  lv_group_focus_obj(view->adjust_grid_.cell[1][0]);
  *view->config_.page = 1;
}

void hmi::ui::SeatView::back_cb(lv_event_t *e) {
  static_cast<SeatView *>(lv_event_get_user_data(e))->show_buttons_page();
}

// Also the back button's, and the screen's arrival: the page left up on the last
// visit must not greet the next one, or the stick's cursor sits on the function
// buttons hidden underneath it.
void hmi::ui::SeatView::show_buttons_page() {
  lv_obj_add_flag(ui_SeatAdjustmentPanel, LV_OBJ_FLAG_HIDDEN);
  *config_.page = 0;
  config_.nav->use_group(buttons_group_, ui_SeatScreen);
  // Back to the button the user selected rather than the top-left one: the
  // grid's cursor still holds it, and returning to where you were is less
  // jarring than being bounced to the corner.
  lv_group_focus_obj(buttons_grid_.cell[buttons_grid_.row][buttons_grid_.col]);
}

// The adjustment page's two numbers: where the selected axis is, and how far it
// goes. Written here rather than bound, because which subject they show changes
// with the function button - see seat_angle_observer.
//
// Spec 04b: the big one is the number, with "°" on an angle -- the word "deg"
// at 264 px was most of the screen -- and the small one says "of 25°", or
// "of 50.0 mm" on a length, where the unit has no symbol to shrink to.
void hmi::ui::SeatView::angle_refresh() {
  if (selected_axis_ < 0) {
    return;
  }
  const format::StepperSpec &spec = formats_[static_cast<size_t>(selected_axis_)];
  const int32_t raw = lv_subject_get_int(&config_.values[selected_axis_]);
  char text[32];
  hmi::format::seat_reading_text(spec, raw, text);
  lv_label_set_text(ui_AngleLabel, text);

  char footer[40];
  hmi::format::seat_range_text(spec, footer);
  lv_label_set_text(ui_MaxAngleLabel, footer);

  // A long reading ("-42.5°") at 264 px runs into the "of": shrink it, about
  // its bottom-left corner, to fit what the "of" leaves.
  const lv_font_t *font = lv_obj_get_style_text_font(ui_AngleLabel, LV_PART_MAIN);
  const lv_font_t *small = lv_obj_get_style_text_font(ui_MaxAngleLabel, LV_PART_MAIN);
  auto width_of = [](const char *s, const lv_font_t *f) {
    lv_point_t size;
    lv_text_get_size(&size, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
  };
  const int32_t wide = width_of(text, font);
  const int32_t room = 660 - width_of(footer, small) - 24;
  const int32_t scale =
      wide > room && wide > 0 ? LV_SCALE_NONE * std::max<int32_t>(room, 1) / wide : LV_SCALE_NONE;
  lv_obj_set_style_transform_pivot_x(ui_AngleLabel, 0, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(ui_AngleLabel, lv_pct(100), LV_PART_MAIN);
  lv_obj_set_style_transform_scale(ui_AngleLabel, scale, LV_PART_MAIN);

  // The preset the motion sits at is the selected one: the negative (spec
  // 04b, "15°" filled).
  for (lv_obj_t *preset :
       {ui_SeatAdjustmentButton3, ui_SeatAdjustmentButton4, ui_SeatAdjustmentButton5}) {
    const lv_obj_t *label = preset == ui_SeatAdjustmentButton3   ? ui_SeatAdjustmentButtonLabel3
                            : preset == ui_SeatAdjustmentButton4 ? ui_SeatAdjustmentButtonLabel4
                                                                 : ui_SeatAdjustmentButtonLabel5;
    lv_obj_set_state(preset, LV_STATE_CHECKED,
                     raw != format::VALUE_UNKNOWN &&
                         preset_target(static_cast<size_t>(selected_axis_), label) == raw);
  }
}

// One per axis, all pointed at ui_AngleLabel: whichever axis moves, the page
// redraws the one it is showing. Cheaper than it looks - a sample only arrives
// twice a second, and refreshing costs two lv_label_set_text.
void hmi::ui::SeatView::angle_observer(lv_observer_t *observer, lv_subject_t *) {
  static_cast<SeatView *>(lv_observer_get_user_data(observer))->angle_refresh();
}

// The number under a function button. An observer for the same reason
// setting_value_observer is one: the value is scaled by its decimals.
void hmi::ui::SeatView::button_value_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *spec = static_cast<const format::StepperSpec *>(lv_observer_get_user_data(observer));
  char text[32];
  format::seat_format(*spec, lv_subject_get_int(subject), text);
  lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

// SeatScreen. Both grids are built here rather than declared with initialisers
// because the ui_* globals only exist once ui_init has run.
//
// The numbers skip 3 and jump to 7: SquareLine renumbered these when the
// screen was renamed, and re-lettering them would be a permutation that a
// name-keyed rename pass cannot apply twice. Phase 5 rebuilds this screen
// from a SeatTile component and settles the names then; until it does, the
// grid below is the only place the visual order is written down.
void hmi::ui::SeatView::fill_grids() {
  buttons_grid_.rows = 3;
  buttons_grid_.cols[0] = 2;
  buttons_grid_.cell[0][0] = ui_SeatButton1; // Elevation
  buttons_grid_.cell[0][1] = ui_SeatButton2; // Backseat
  buttons_grid_.cols[1] = 2;
  buttons_grid_.cell[1][0] = ui_SeatButton4; // Side Tilt
  buttons_grid_.cell[1][1] = ui_SeatButton7; // Setback
  buttons_grid_.cols[2] = 2;
  buttons_grid_.cell[2][0] = ui_SeatButton5; // Static
  buttons_grid_.cell[2][1] = ui_SeatButton6; // Dynamic

  // Spec 04b, top to bottom: the back button, "-" and "+", the three presets.
  adjust_grid_.rows = 3;
  adjust_grid_.cols[0] = 1;
  adjust_grid_.cell[0][0] = ui_SeatBackButton;
  adjust_grid_.cols[1] = 2;
  adjust_grid_.cell[1][0] = ui_SeatAdjustmentButton1;
  adjust_grid_.cell[1][1] = ui_SeatAdjustmentButton2;
  adjust_grid_.cols[2] = 3;
  adjust_grid_.cell[2][0] = ui_SeatAdjustmentButton3;
  adjust_grid_.cell[2][1] = ui_SeatAdjustmentButton4;
  adjust_grid_.cell[2][2] = ui_SeatAdjustmentButton5;

  // The label each function button names on the adjustment page, or null for the
  // two inert ones. Indexed to match the buttons' grid.
  labels_[0][0] = ui_SeatButtonLabel1;
  labels_[0][1] = ui_SeatButtonLabel2;
  labels_[1][0] = ui_SeatButtonLabel4;
  labels_[1][1] = ui_SeatButtonLabel7;
}

void hmi::ui::SeatView::init_pages() {
  fill_grids();

  // Neither page's buttons carry a FOCUSED style in the export, so joystick
  // focus would be invisible. Recolour the 2 px border they already have, the
  // same way the settings rows do, so it tracks the day/dark theme instead of
  // being a hardcoded accent.
  //
  // The selector is spelled out rather than written `LV_PART_MAIN |
  // LV_STATE_FOCUSED` as the C export does: C++ deprecates a bitwise OR between
  // two different enum types, and -Werror turns that into a build failure.
  auto style_focus = [](lv_obj_t *button) {
    static constexpr lv_style_selector_t kFocused =
        static_cast<lv_style_selector_t>(LV_PART_MAIN) |
        static_cast<lv_style_selector_t>(LV_STATE_FOCUSED);
    ui_object_set_themeable_style_property(button, kFocused, LV_STYLE_BORDER_COLOR,
                                           _ui_theme_color_focused);
    ui_object_set_themeable_style_property(button, kFocused, LV_STYLE_BORDER_OPA,
                                           _ui_theme_alpha_focused);
  };

  buttons_group_ = lv_group_create();
  for (int r = 0; r < buttons_grid_.rows; r++) {
    for (int c = 0; c < buttons_grid_.cols[r]; c++) {
      lv_obj_t *button = buttons_grid_.cell[r][c];
      lv_group_add_obj(buttons_group_, button);
      lv_obj_add_event_cb(button, grid_key_cb, LV_EVENT_KEY, &buttons_grid_);
      lv_obj_add_event_cb(button, click_cb, LV_EVENT_CLICKED, this);
      style_focus(button);
    }
  }

  // The adjustment page: "-"/"+" and the three presets, handled by
  // adjust_click_cb against whichever axis the function button picked, and the
  // back button, which closes the page. The design draws them in the negative
  // when pressed or checked; the ring is the cursor, as everywhere else.
  adjust_group_ = lv_group_create();
  for (int r = 0; r < adjust_grid_.rows; r++) {
    for (int c = 0; c < adjust_grid_.cols[r]; c++) {
      lv_obj_t *button = adjust_grid_.cell[r][c];
      lv_group_add_obj(adjust_group_, button);
      lv_obj_add_event_cb(button, grid_key_cb, LV_EVENT_KEY, &adjust_grid_);
      if (button == ui_SeatBackButton) {
        lv_obj_add_event_cb(button, back_cb, LV_EVENT_CLICKED, this);
      } else {
        lv_obj_add_event_cb(button, adjust_click_cb, LV_EVENT_CLICKED, this);
      }
      config_.nav->focus_ring(button);
      config_.nav->mirror_states(button);
    }
  }
  // It covers the function buttons: its fill is what hides them.
  config_.keep_overlay_fill(ui_SeatAdjustmentPanel);
}

void hmi::ui::SeatView::init_values() {
  lv_subject_init_string(&function_subject_, function_buf_.data(), function_prev_buf_.data(),
                         function_buf_.size(), lv_label_get_text(ui_AngleSettingLabel));
  lv_label_bind_text(ui_AngleSettingLabel, &function_subject_, nullptr);

  // The seat values, shared by this screen and the DEBUG ACTUATORS page. These come
  // first because binding to an lv_subject_t means storing a pointer into it: every
  // observer below, and the settings rows built on demand later, read these.
  for (size_t i = 0; i < AXES; i++) {
    lv_subject_init_int(&config_.values[i], format::VALUE_UNKNOWN);
    const rammp::SeatAxisSpec &axis = rammp::kSeatAxes[i];
    formats_[i] = {axis.short_name, axis.label,    axis.min_value, axis.max_value,
                   axis.step,       axis.decimals, axis.unit};
  }

  // The number under each function button, and the pair on the adjustment page,
  // read the values the MCB reports and nothing else: a press asks, and the
  // screen moves when SeatState says the seat did. The export's placeholders
  // ("4.0 in", "12°") are replaced the moment the first sample lands, and read
  // "--" until then.
  lv_obj_t *values[] = {ui_SeatButtonValue1, ui_SeatButtonValue2, ui_SeatButtonValue4,
                        ui_SeatButtonValue5};
  for (size_t i = 0; i < AXES && i < std::size(values); i++) {
    lv_subject_add_observer_obj(&config_.values[i], button_value_observer, values[i],
                                &formats_[i]);
    lv_subject_add_observer_obj(&config_.values[i], angle_observer, ui_AngleLabel, this);
  }
}

// Raw value per seat axis, in the table's units, as the MCB last reported it
// (seat_apply_state). Shared by the seat view and the DEBUG ACTUATORS rows;
// the command path (seat_step, seat_request) reads it too.
static lv_subject_t seat_axis_value[rammp::kSeatAxisCount];

// The one instance.
static constinit hmi::ui::SeatView seat_view{{
    .nav = &ui_nav_port,
    .values = seat_axis_value,
    .step = seat_step,
    .request = seat_request,
    .show_buttons_page = seat_show_buttons_page,
    .keep_overlay_fill = keep_overlay_fill,
    .page = &seat_page,
}};
// frag_nav resets the function buttons' cursor on arrival.
static constinit ButtonGrid &seat_buttons_grid = seat_view.buttons_grid();
static void seat_show_buttons_page() { seat_view.show_buttons_page(); }
