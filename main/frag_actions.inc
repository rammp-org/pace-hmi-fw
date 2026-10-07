// split_main.py fragment of main.cpp (frag_actions: original lines 2782-2979); one TU, see main.cpp.
#include "hmi_ui/actions_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// SkunkWorksScreen: a grid of one-press actions
//
// The menu's Skunk Works row. One tile per entry in actions_spec.h: the spec
// gives each its title, subtitle and whether it needs the MCB; kActionRun below
// says what it does. The stick walks the tiles as a grid; the stick button (or
// a tap) runs the focused one.
//
// A button that needs the MCB greys out while mcb_ready() is false, so it says
// nothing would happen before anyone presses it - and the local actions stay
// reachable, which a full-screen banner would not allow.
//
// Like the SettingsScreen, the screen and its tiles exist only
// while it is up - see "Screens built on demand".
/////////////////////////////////////////////////////////////////////////////

// What each action does. LVGL task (a click).
static void action_haptic_test() {
  haptic_play(espp::Drv2605::Waveform::ALERT_1000MS, kHapticBuzzSlots);
}

static void action_self_test() { selftest_request(SelfTestTrigger::LOCAL, 0); }

// An RTPS command: the request a "+" press on the actuators page makes. The
// seat moves only if the MCB agrees, and a refusal flashes on that page.
static void action_seat_up() { seat_step(rammp::index_of(rammp::SeatAxis::ELEVATION), +1); }

static void action_restart_hmi() {
  static espp::Logger action_logger({.tag = "actions", .level = espp::Logger::Verbosity::INFO});
  action_logger.warn("restart requested from the actions screen");
  esp_restart();
}

// In actions_spec.h order.
static void (*const kActionRun[])() = {
    action_haptic_test, // ACTION_HAPTIC_TEST
    action_self_test,   // ACTION_SELF_TEST
    action_seat_up,     // ACTION_SEAT_UP
    fps_toggle,         // ACTION_FPS_COUNTER
    action_restart_hmi, // ACTION_RESTART_HMI
};
static_assert(std::size(kActionRun) == ACTION_COUNT,
              "every actions_spec.h entry needs its function here");

static constexpr hmi::ui::ActionsView::Tile kActionSpecs[] = {
#define ACTIONS_ROW(name_, title_, subtitle_, mcb_) {title_, subtitle_, (mcb_) != 0},
    ACTIONS_TABLE(ACTIONS_ROW)
#undef ACTIONS_ROW
};
static_assert(ACTION_COUNT <= hmi::ui::ActionsView::TILES_MAX, "more actions than tiles");

static lv_group_t *actions_group = nullptr;

// A tile the MCB could not act on right now (hmi::ui::ActionsView::UNAVAILABLE);
// frag_nav greys its gated menu rows the same way.
static constexpr lv_state_t kActionUnavailable = hmi::ui::ActionsView::UNAVAILABLE;
static constexpr lv_style_selector_t kActionUnavailableStyle =
    hmi::ui::ActionsView::UNAVAILABLE_STYLE;

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

// Greys an MCB action while the MCB could not act on it. Bound to every
// subject mcb_ready() reads, so it follows the link and the state both.
static void action_ready_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_set_state(lv_observer_get_target_obj(observer), kActionUnavailable, !mcb_ready());
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

// The one instance; the tiles' actions stay out here, and so does the group
// (frag_nav reads it).
static constinit hmi::ui::ActionsView actions_view{{
    .nav = &ui_nav_port,
    .tiles = kActionSpecs,
    .run = kActionRun,
    .count = ACTION_COUNT,
    .screen_ensure = actions_screen_ensure,
    .bind_ready = bind_to_drive_blocked_cause,
    .ready_observer = action_ready_observer,
    .refuse = refusal_feedback,
}};
static void actions_open() { actions_view.open(); }
static void actions_clear() { actions_view.clear(); }
static void action_focus(int index) { actions_view.focus(index); }
