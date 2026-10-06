// split_main.py fragment of main.cpp (frag_actions: original lines 2782-2979); one TU, see main.cpp.
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

struct ActionSpec {
  const char *title;
  const char *subtitle;
  bool needs_mcb;
};

static constexpr ActionSpec kActionSpecs[] = {
#define ACTIONS_ROW(name_, title_, subtitle_, mcb_) {title_, subtitle_, (mcb_) != 0},
    ACTIONS_TABLE(ACTIONS_ROW)
#undef ACTIONS_ROW
};

static lv_obj_t *action_buttons[ACTION_COUNT];
static int action_button_count; // buttons on screen; 0 while it is not up
static int action_cursor;       // which button the joystick is on
static int action_cols = 1;     // tiles per row, measured once they are laid out
static lv_group_t *actions_group = nullptr;

// A tile the MCB could not act on right now. Not LV_STATE_DISABLED: LVGL drops
// every input event aimed at a disabled object, LV_EVENT_KEY included, so the
// joystick's cursor landing on a greyed tile could never leave it again. A
// user state greys it the same way and leaves it walkable; action_click_cb is
// what refuses to run it.
static constexpr lv_state_t kActionUnavailable = LV_STATE_USER_2;
static constexpr lv_style_selector_t kActionUnavailableStyle =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(kActionUnavailable);

// Clamped rather than wrapped, for the reason in grid_key_cb.
static void action_focus(int index) {
  if (action_button_count == 0) {
    return;
  }
  action_cursor = std::clamp(index, 0, action_button_count - 1);
  lv_group_focus_obj(action_buttons[action_cursor]);
}

// Greys an MCB action while the MCB could not act on it. Bound to every
// subject mcb_ready() reads, so it follows the link and the state both.
static void action_ready_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_set_state(lv_observer_get_target_obj(observer), kActionUnavailable, !mcb_ready());
}

// A tap, or the stick button on the focused one (the keypad indev turns ENTER
// into LV_EVENT_CLICKED). A greyed tile takes the press and does nothing.
static void action_click_cb(lv_event_t *e) {
  lv_obj_t *button = lv_event_get_target_obj(e);
  if (lv_obj_has_state(button, kActionUnavailable)) {
    refusal_feedback(); // felt, like a refusal
    return;
  }
  const auto index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  action_focus(index);
  kActionRun[index]();
}

// The tiles are a grid (the flex panel wraps them action_cols to a row), so
// the stick walks them as one: up and down a row, left and right along it.
// The keypad indev hands arrow keys to the focused object rather than moving
// the group itself.
static void action_key_cb(lv_event_t *e) {
  // The cursor is the tile that holds focus: coming back up from the burger
  // key, the group moved focus without telling action_cursor.
  const lv_obj_t *here = lv_event_get_target_obj(e);
  for (int i = 0; i < action_button_count; i++) {
    if (action_buttons[i] == here) {
      action_cursor = i;
    }
  }
  const int col = action_cursor % action_cols;
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    if (action_cursor >= action_cols) {
      action_focus(action_cursor - action_cols);
    }
    return;
  case LV_KEY_DOWN:
    if (action_cursor + action_cols >= action_button_count) {
      // No tile below. On the last row that means the burger key; on a
      // short last row seen from above, the last tile.
      if (action_cursor / action_cols == (action_button_count - 1) / action_cols) {
        nav_to_key();
      } else {
        action_focus(action_button_count - 1);
      }
      return;
    }
    action_focus(action_cursor + action_cols);
    return;
  case LV_KEY_LEFT:
    if (col > 0) {
      action_focus(action_cursor - 1);
    }
    return;
  case LV_KEY_RIGHT:
    if (col + 1 < action_cols && action_cursor + 1 < action_button_count) {
      action_focus(action_cursor + 1);
    }
    return;
  default:
    return;
  }
}

// Deleting a button takes its observers and events with it and drops it from
// actions_group; the theme manager forgets it too.
static void actions_clear() {
  for (int i = 0; i < action_button_count; i++) {
    lv_obj_delete(action_buttons[i]);
  }
  action_button_count = 0;
  action_cursor = 0;
}

// Builds the buttons and shows the screen. LVGL task (a hold gesture).
static void actions_open() {
  actions_screen_ensure(); // built on demand: see "Screens built on demand"
  actions_clear();
  for (int i = 0; i < ACTION_COUNT; i++) {
    const ActionSpec &spec = kActionSpecs[i];
    lv_obj_t *button = ui_SlotTile_create(ui_GenericActionsFlexPanel);
    action_buttons[action_button_count++] = button;
    lv_label_set_text(ui_comp_get_child(button, UI_COMP_SLOTTILE_SLOTBOX_SLOTTITLE), spec.title);
    lv_label_set_text(ui_comp_get_child(button, UI_COMP_SLOTTILE_SLOTBOX_SLOTSUBTITLE),
                      spec.subtitle);
    // The same cursor as every other button; greyed fades the whole tile.
    nav_focus_ring(button);
    lv_obj_set_style_opa(button, LV_OPA_40, kActionUnavailableStyle);
    if (spec.needs_mcb) {
      bind_to_drive_blocked_cause(button, action_ready_observer);
    }
    lv_group_add_obj(actions_group, button);
    lv_obj_add_event_cb(button, action_key_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(button, action_click_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    // Only the button takes part in focus; see clear_click_focusable_recursive.
    clear_click_focusable_recursive(button);
  }
  // Tiles per row: however many share the first tile's row once the flex
  // panel has wrapped them. Measured rather than assumed, so a SquareLine
  // change to the tile or the panel width needs nothing here.
  action_cols = 1;
  if (action_button_count > 0) {
    lv_obj_update_layout(ui_GenericActionsFlexPanel);
    const int32_t first_y = lv_obj_get_y(action_buttons[0]);
    action_cols = 0;
    for (int i = 0; i < action_button_count && lv_obj_get_y(action_buttons[i]) == first_y; i++) {
      action_cols++;
    }
  }
  _ui_screen_change(&ui_SkunkWorksScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_SkunkWorksScreen_screen_init);
}

