// ActionsView: the SkunkWorksScreen's tiles (moved from main/frag_actions.inc). What each tile
// does stays in main.

#include "hmi_ui/actions_view.hpp"

#include <algorithm>
#include <cstdint>

#include "hmi_ui/widget_tree.hpp"
#include "ui.h"

#include "components/ui_comp_slottile.h"

lv_group_t *hmi::ui::ActionsView::init() {
  group_ = lv_group_create();
  return group_;
}

// Clamped rather than wrapped, for the reason in grid_key_cb.
void hmi::ui::ActionsView::focus(int index) {
  if (button_count_ == 0) {
    return;
  }
  cursor_ = std::clamp(index, 0, button_count_ - 1);
  lv_group_focus_obj(buttons_[cursor_]);
}

// A tap, or the stick button on the focused one (the keypad indev turns ENTER
// into LV_EVENT_CLICKED). A greyed tile takes the press and does nothing.
void hmi::ui::ActionsView::click_cb(lv_event_t *e) {
  auto *view = static_cast<ActionsView *>(lv_event_get_user_data(e));
  lv_obj_t *button = lv_event_get_target_obj(e);
  if (lv_obj_has_state(button, UNAVAILABLE)) {
    view->config_.refuse(); // felt, like a refusal
    return;
  }
  // Which tile: its place among the tiles, the index it was built with.
  int index = 0;
  while (index < view->button_count_ && view->buttons_[index] != button) {
    index++;
  }
  if (index == view->button_count_) {
    return; // not a tile of this view (cannot happen: only tiles carry this callback)
  }
  view->focus(index);
  view->config_.run[index]();
}

// The tiles are a grid (the flex panel wraps them cols_ to a row), so the
// stick walks them as one: up and down a row, left and right along it.
// The keypad indev hands arrow keys to the focused object rather than moving
// the group itself.
void hmi::ui::ActionsView::key_cb(lv_event_t *e) {
  auto *view = static_cast<ActionsView *>(lv_event_get_user_data(e));
  // The cursor is the tile that holds focus: coming back up from the burger
  // key, the group moved focus without telling the cursor.
  const lv_obj_t *here = lv_event_get_target_obj(e);
  for (int i = 0; i < view->button_count_; i++) {
    if (view->buttons_[i] == here) {
      view->cursor_ = i;
    }
  }
  const int cursor = view->cursor_;
  const int cols = view->cols_;
  const int count = view->button_count_;
  const int col = cursor % cols;
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    if (cursor >= cols) {
      view->focus(cursor - cols);
    }
    return;
  case LV_KEY_DOWN:
    if (cursor + cols >= count) {
      // No tile below. On the last row that means the burger key; on a
      // short last row seen from above, the last tile.
      if (cursor / cols == (count - 1) / cols) {
        view->config_.nav->to_key();
      } else {
        view->focus(count - 1);
      }
      return;
    }
    view->focus(cursor + cols);
    return;
  case LV_KEY_LEFT:
    if (col > 0) {
      view->focus(cursor - 1);
    }
    return;
  case LV_KEY_RIGHT:
    if (col + 1 < cols && cursor + 1 < count) {
      view->focus(cursor + 1);
    }
    return;
  default:
    return;
  }
}

// Deleting a button takes its observers and events with it and drops it from
// the group; the theme manager forgets it too.
void hmi::ui::ActionsView::clear() {
  for (int i = 0; i < button_count_; i++) {
    lv_obj_delete(buttons_[i]);
  }
  button_count_ = 0;
  cursor_ = 0;
}

// Builds the buttons and shows the screen. LVGL task (a hold gesture).
void hmi::ui::ActionsView::open() {
  config_.screen_ensure(); // built on demand: see "Screens built on demand"
  clear();
  for (int i = 0; i < config_.count; i++) {
    const Tile &spec = config_.tiles[i];
    lv_obj_t *button = ui_SlotTile_create(ui_GenericActionsFlexPanel);
    buttons_[button_count_++] = button;
    lv_label_set_text(ui_comp_get_child(button, UI_COMP_SLOTTILE_SLOTBOX_SLOTTITLE), spec.title);
    lv_label_set_text(ui_comp_get_child(button, UI_COMP_SLOTTILE_SLOTBOX_SLOTSUBTITLE),
                      spec.subtitle);
    // The same cursor as every other button; greyed fades the whole tile.
    config_.nav->focus_ring(button);
    lv_obj_set_style_opa(button, LV_OPA_40, UNAVAILABLE_STYLE);
    if (spec.needs_mcb) {
      config_.bind_ready(button, config_.ready_observer);
    }
    lv_group_add_obj(group_, button);
    lv_obj_add_event_cb(button, key_cb, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(button, click_cb, LV_EVENT_CLICKED, this);
    // Only the button takes part in focus; see clear_click_focusable_recursive.
    clear_click_focusable_recursive(button);
  }
  // Tiles per row: however many share the first tile's row once the flex
  // panel has wrapped them. Measured rather than assumed, so a SquareLine
  // change to the tile or the panel width needs nothing here.
  cols_ = 1;
  if (button_count_ > 0) {
    lv_obj_update_layout(ui_GenericActionsFlexPanel);
    const int32_t first_y = lv_obj_get_y(buttons_[0]);
    cols_ = 0;
    for (int i = 0; i < button_count_ && lv_obj_get_y(buttons_[i]) == first_y; i++) {
      cols_++;
    }
  }
  _ui_screen_change(&ui_SkunkWorksScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_SkunkWorksScreen_screen_init);
}
