// BenchPinView: the BenchGateScreen's PIN pad (moved from main/frag_bench_pin.inc).

#include "hmi_ui/bench_pin_view.hpp"

#include "ui.h"

// Empties the entry and puts the prompt back. Runs on every BenchGateScreen
// load, so the PIN is asked again on every visit rather than latching once per
// boot.
void hmi::ui::BenchPinView::draw_empty() {
  lv_subject_set_int(&len_subject_, 0);
  lv_subject_copy_string(&message_subject_, PROMPT_TEXT);
}
void hmi::ui::BenchPinView::reset() {
  pin_.reset();
  draw_empty();
}

// Puts the cursor and the focus on one key, by grid index. Used on load so the
// first joystick nudge moves between keys instead of being spent picking one.
void hmi::ui::BenchPinView::focus(int index) {
  grid_.row = index / GRID_MAX_COLS;
  grid_.col = index % GRID_MAX_COLS;
  if (grid_.cell[grid_.row][grid_.col] != nullptr) {
    lv_group_focus_obj(grid_.cell[grid_.row][grid_.col]);
  }
}

// A key on the PIN pad: the digit (or backspace) is the one DIGITS gives the
// cell the cursor lands on; nothing else can reach here, so there is no key
// this has to ignore.
void hmi::ui::BenchPinView::keypad_cb(lv_event_t *e) {
  auto *view = static_cast<BenchPinView *>(lv_event_get_user_data(e));
  // Touch moves the cursor too, so the joystick carries on from where a finger
  // last landed rather than from where the stick left off.
  grid_sync_cursor(&view->grid_, lv_event_get_target_obj(e));
  const int digit = DIGITS[view->grid_.row][view->grid_.col];

  if (digit == KEY_BACK) {
    if (view->pin_.backspace()) {
      lv_subject_set_int(&view->len_subject_, view->pin_.digits());
    }
    return;
  }

  const PinPress press = view->pin_.press(digit);
  lv_subject_set_int(&view->len_subject_, press.typed);
  // Typing clears a previous rejection, so the line reads as a prompt for the
  // entry in progress rather than a verdict on the last one.
  lv_subject_copy_string(&view->message_subject_, PROMPT_TEXT);

  // Judged on the fourth digit rather than on an OK key: the row of dots makes
  // the length obvious, so a confirm step would add nothing. Either verdict
  // empties the entry (the model has already emptied its own).
  switch (press.verdict) {
  case PinVerdict::INCOMPLETE:
    return;
  case PinVerdict::ACCEPTED:
    view->draw_empty();
    view->config_.accepted();
    return;
  case PinVerdict::REJECTED:
    view->draw_empty();
    lv_subject_copy_string(&view->message_subject_, WRONG_TEXT);
    return;
  }
}

// The checkboxes need no observer of their own - each one is CHECKED exactly
// when the entry has reached it, which lv_obj_bind_state_if_ge says directly.
lv_group_t *hmi::ui::BenchPinView::init() {
  lv_subject_init_int(&len_subject_, 0);
  lv_subject_init_string(&message_subject_, message_buf_.data(), message_prev_buf_.data(),
                         message_buf_.size(), PROMPT_TEXT);
  lv_label_bind_text(ui_BenchIntro, &message_subject_, nullptr);

  // The dots. The export draws them as an empty outline with no CHECKED look of
  // its own, so a filled one is added here in the theme's text colour -- and
  // themeable rather than a literal, so it follows a day/dark switch.
  lv_obj_t *dots[PIN_LEN] = {ui_BenchPinDot1, ui_BenchPinDot2, ui_BenchPinDot3, ui_BenchPinDot4};
  static constexpr lv_style_selector_t kMainChecked =
      static_cast<lv_style_selector_t>(LV_PART_MAIN) |
      static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
  for (int i = 0; i < PIN_LEN; i++) {
    ui_object_set_themeable_style_property(dots[i], kMainChecked, LV_STYLE_BG_COLOR,
                                           _ui_theme_color_text);
    ui_object_set_themeable_style_property(dots[i], kMainChecked, LV_STYLE_BG_OPA,
                                           _ui_theme_alpha_text);
    lv_obj_bind_state_if_ge(dots[i], &len_subject_, LV_STATE_CHECKED, i + 1);
  }

  // The pad, in the order it is drawn (DIGITS). The grid gives the joystick the
  // 2D walk the buttonmatrix used to bring with it.
  grid_.rows = 4;
  lv_obj_t *keys[GRID_MAX_ROWS][GRID_MAX_COLS] = {
      {ui_BenchKey1, ui_BenchKey2, ui_BenchKey3},
      {ui_BenchKey4, ui_BenchKey5, ui_BenchKey6},
      {ui_BenchKey7, ui_BenchKey8, ui_BenchKey9},
      {nullptr, ui_BenchKey0, ui_BenchKeyBack},
  };
  lv_group_t *group = lv_group_create();
  for (int r = 0; r < grid_.rows; r++) {
    grid_.cols[r] = GRID_MAX_COLS;
    for (int c = 0; c < GRID_MAX_COLS; c++) {
      lv_obj_t *key = keys[r][c];
      grid_.cell[r][c] = key;
      if (key == nullptr) {
        continue; // the hole under "7"
      }
      lv_group_add_obj(group, key);
      lv_obj_add_event_cb(key, keypad_cb, LV_EVENT_CLICKED, this);
      lv_obj_add_event_cb(key, grid_key_cb, LV_EVENT_KEY, &grid_);
      // The cursor is the same focus ring as every other button; a press is
      // the key's negative (the export's PRESSED style), carried to its label.
      config_.style_key(key);
    }
  }
  return group;
}
