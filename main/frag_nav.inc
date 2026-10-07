// split_main.py fragment of main.cpp (frag_nav: original lines 3230-3999); one TU, see main.cpp.
// How long an updated image runs before it keeps itself (github_ota_boot_confirm).
static constexpr uint32_t kOtaConfirmAfterMs = 30'000;

/////////////////////////////////////////////////////////////////////////////
// The burger menu, and moving around with the joystick (hmi::ui::NavView)
/////////////////////////////////////////////////////////////////////////////
#include "hmi_ui/actions_view.hpp"
#include "hmi_ui/nav_view.hpp"
#include "hmi_ui/widget_tree.hpp"

// The child id of each row, in NavDest order.
static constexpr std::array<uint32_t, hmi::ui::NAV_DEST_COUNT> kNavRowIds = {
    UI_COMP_MENUOVERLAY_ROW1,
    UI_COMP_MENUOVERLAY_ROW2,
    UI_COMP_MENUOVERLAY_ROW3,
    UI_COMP_MENUOVERLAY_ROW4,
    UI_COMP_MENUOVERLAY_ROW5,
    UI_COMP_MENUOVERLAY_ROW6,
    UI_COMP_MENUOVERLAY_ROW7,
    UI_COMP_MENUOVERLAY_ROW8,
    UI_COMP_MENUOVERLAY_SUBMENU_SUBROW2,
    UI_COMP_MENUOVERLAY_SUBMENU_SUBROW3,
    UI_COMP_MENUOVERLAY_SUBMENU_SUBROW4,
    UI_COMP_MENUOVERLAY_SUBMENU_SUBROW5,
    UI_COMP_MENUOVERLAY_SUBMENU_SUBROW6,
};
// The panel that holds Settings' level over the top rows, and its first row,
// "< Settings", which goes back up.
static constexpr uint32_t kNavSubMenuId = UI_COMP_MENUOVERLAY_SUBMENU;
static constexpr uint32_t kNavSubBackId = UI_COMP_MENUOVERLAY_SUBMENU_SUBROW1;

using hmi::ui::NavDest;
using hmi::ui::NavView;

hmi::ui::NavView::Chrome *hmi::ui::NavView::chrome_of(const lv_obj_t *screen) {
  auto *const found = std::find_if(chrome_.begin(), chrome_.end(), [screen](const Chrome &c) {
    return c.screen != nullptr && c.screen == screen;
  });
  return found != chrome_.end() ? found : nullptr;
}

const hmi::ui::NavView::Chrome *hmi::ui::NavView::chrome_of(const lv_obj_t *screen) const {
  const auto *const found =
      std::find_if(chrome_.begin(), chrome_.end(), [screen](const Chrome &c) {
        return c.screen != nullptr && c.screen == screen;
      });
  return found != chrome_.end() ? found : nullptr;
}

bool hmi::ui::NavView::has_chrome(const lv_obj_t *screen) const {
  return chrome_of(screen) != nullptr;
}

// Each screen's key and overlay, filled in as attach_chrome wires them, so "the
// burger key on the screen that is up" is one lookup. The screens built on
// demand come and go, so an entry is emptied when its key is deleted.
void hmi::ui::NavView::chrome_forget_cb(lv_event_t *e) {
  auto *view = static_cast<NavView *>(lv_event_get_user_data(e));
  const lv_obj_t *key = lv_event_get_target_obj(e);
  std::replace_if(
      view->chrome_.begin(), view->chrome_.end(), [key](const Chrome &c) { return c.key == key; },
      Chrome{});
}

// --- the look ------------------------------------------------------------------

// LVGL keeps a state on the object it was set on. The spec's negative (the
// styles negative() writes in the SquareLine build) is defined on every layer
// of a control -- ground, label, bars -- so the state has to reach all of them
// or the ground inverts under a label that does not, and the label vanishes.
static constexpr lv_state_t kMirroredStates = static_cast<lv_state_t>(
    static_cast<uint32_t>(LV_STATE_PRESSED) | static_cast<uint32_t>(LV_STATE_CHECKED) |
    static_cast<uint32_t>(LV_STATE_FOCUSED));

static void nav_mirror_to(lv_obj_t *obj, lv_state_t on) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    lv_obj_remove_state(child, static_cast<lv_state_t>(kMirroredStates & ~on));
    lv_obj_add_state(child, on);
    nav_mirror_to(child, on);
  }
}

void hmi::ui::NavView::mirror_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_current_target_obj(e);
  nav_mirror_to(obj, static_cast<lv_state_t>(lv_obj_get_state(obj) & kMirroredStates));
}

void hmi::ui::NavView::mirror_states(lv_obj_t *obj) {
  lv_obj_add_event_cb(obj, mirror_cb, LV_EVENT_STATE_CHANGED, nullptr);
  nav_mirror_to(obj, static_cast<lv_state_t>(lv_obj_get_state(obj) & kMirroredStates));
}

// The joystick's cursor on a button: a second outline in the theme's text
// colour, a gap outside the button's own stroke. Not the theme's blue "focused"
// colour, which all but vanishes on black -- and the spec's rule is that no
// control uses colour to say it is chosen. Themeable, so it follows a day/night
// switch.
//
// Set for FOCUS_KEY as well as FOCUSED: a focus the keypad caused carries both,
// and LVGL ranks FOCUS_KEY above FOCUSED, so the default theme's own FOCUS_KEY
// outline -- two dim blue pixels -- would otherwise win over this one.
void hmi::ui::NavView::ring_style(lv_obj_t *obj, int32_t width, int32_t pad) {
  for (lv_state_t state : {LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY}) {
    const lv_style_selector_t sel =
        static_cast<lv_style_selector_t>(LV_PART_MAIN) | static_cast<lv_style_selector_t>(state);
    if (width > 0) {
      ui_object_set_themeable_style_property(obj, sel, LV_STYLE_OUTLINE_COLOR,
                                             _ui_theme_color_text);
      ui_object_set_themeable_style_property(obj, sel, LV_STYLE_OUTLINE_OPA, _ui_theme_alpha_text);
    }
    lv_obj_set_style_outline_width(obj, width, sel);
    lv_obj_set_style_outline_pad(obj, pad, sel);
  }
}

void hmi::ui::NavView::focus_ring(lv_obj_t *obj) { ring_style(obj, 4, 6); }

// The burger key spans the screen edge to edge, so an outside ring would be
// off-screen and an inside one on the key itself would be painted over by its
// Ground. The ring goes on the Ground, inset -- the Ground sees the key's
// focus through mirror_states -- and the key's own outline is switched off.
void hmi::ui::NavView::key_focus_ring(lv_obj_t *key) {
  ring_style(key, 0, 0);
  lv_obj_t *ground = ui_comp_get_child(key, UI_COMP_MENUKEY_GROUND);
  ring_style(ground, 4, -14);
  // With the menu open the key is CHECKED, which fills its ground with the text
  // colour -- and a ring in the text colour on that is invisible. Focused AND
  // checked, the ring takes the background colour instead. LVGL ranks a style
  // by its state bits, and FOCUSED|CHECKED outranks FOCUSED alone.
  const lv_style_selector_t open_and_focused = static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                                               static_cast<lv_style_selector_t>(LV_STATE_FOCUSED) |
                                               static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
  ui_object_set_themeable_style_property(ground, open_and_focused, LV_STYLE_OUTLINE_COLOR,
                                         _ui_theme_color_background);
  ui_object_set_themeable_style_property(ground, open_and_focused, LV_STYLE_OUTLINE_OPA,
                                         _ui_theme_alpha_background);
  lv_obj_set_style_outline_width(ground, 4, open_and_focused);
  lv_obj_set_style_outline_pad(ground, -14, open_and_focused);
}

// --- groups --------------------------------------------------------------------

// Makes `g` the joystick's group, with `screen`'s burger key appended as its
// last member. Removed and re-added every time rather than once, because the
// list screens rebuild their rows on every visit and the key has to stay after
// them for "down past the last row" to reach it.
void hmi::ui::NavView::use_group(lv_group_t *g, const lv_obj_t *screen) {
  lv_group_set_wrap(g, false);
  if (Chrome *c = chrome_of(screen)) {
    lv_group_add_obj(g, c->key); // removes it from wherever it was first
  }
  screen_group_ = g;
  lv_indev_set_group(*config_.indev, g);
}

void hmi::ui::NavView::to_key() const {
  if (const Chrome *c = chrome_of(lv_screen_active())) {
    lv_group_focus_obj(c->key);
  }
}

// --- the menu ------------------------------------------------------------------

void hmi::ui::NavView::menu_anim_cb(void *obj, int32_t y) {
  lv_obj_set_y(static_cast<lv_obj_t *>(obj), y);
}

void hmi::ui::NavView::menu_hide_cb(lv_anim_t *a) {
  auto *overlay = static_cast<lv_obj_t *>(a->var);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  // A row replayed on arrival (drop_row_) goes back to resting once it is
  // out of sight, and the overlay to its top level, ready for the next time it
  // opens.
  for (uint32_t id : kNavRowIds) {
    lv_obj_remove_state(ui_comp_get_child(overlay, id), LV_STATE_CHECKED);
  }
  lv_obj_add_flag(ui_comp_get_child(overlay, kNavSubMenuId), LV_OBJ_FLAG_HIDDEN);
}

void hmi::ui::NavView::menu_slide(lv_obj_t *overlay, int32_t from, int32_t to,
                                  bool hide_after) const {
  lv_anim_delete(overlay, menu_anim_cb);
  // Instant unless Settings turns the slide on: straight to where the
  // slide would have ended, hidden if that is where it was going.
  if (lv_subject_get_int(config_.menu_slide) == 0) {
    lv_obj_set_y(overlay, to);
    if (hide_after) {
      lv_anim_t done;
      lv_anim_init(&done);
      lv_anim_set_var(&done, overlay);
      menu_hide_cb(&done);
    }
    return;
  }
  lv_obj_set_y(overlay, from);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, overlay);
  lv_anim_set_exec_cb(&a, menu_anim_cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, MENU_SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  if (hide_after) {
    lv_anim_set_completed_cb(&a, menu_hide_cb);
  }
  lv_anim_start(&a);
}

void hmi::ui::NavView::key_open_look(const lv_obj_t *screen, bool open) {
  if (Chrome *c = chrome_of(screen)) {
    lv_obj_set_state(c->key, LV_STATE_CHECKED, open);
  }
}

// Closes the menu over the screen that is up and gives the stick back to it,
// on the key the menu was opened from.
void hmi::ui::NavView::close_menu() {
  if (*config_.menu_open == nullptr) {
    return;
  }
  lv_obj_t *overlay = *config_.menu_open;
  *config_.menu_open = nullptr;
  menu_sub_ = false; // the overlay goes back to its top level once hidden
  if (press_timer_ != nullptr) {
    lv_timer_delete(press_timer_);
    press_timer_ = nullptr;
  }
  key_open_look(lv_screen_active(), false);
  menu_slide(overlay, lv_obj_get_y(overlay), MENU_HIDDEN_Y, true);
  lv_group_remove_all_objs(*config_.menu_group);
  if (screen_group_ != nullptr) {
    use_group(screen_group_, lv_screen_active());
    to_key();
  }
  config_.gate_update();
}

// Shows one level of the open menu -- the top rows, or Settings' over them --
// and gives the stick that level's rows, the cursor on `focus`.
void hmi::ui::NavView::menu_level(lv_obj_t *overlay, bool sub, lv_obj_t *focus) {
  menu_sub_ = sub;
  lv_obj_set_flag(ui_comp_get_child(overlay, kNavSubMenuId), LV_OBJ_FLAG_HIDDEN, !sub);

  // The rows belong to whichever overlay is up, so the group is rebuilt rather
  // than filled once: every screen has its own instance of all of them. The
  // key goes last, so down from the bottom row reaches it and a press closes.
  lv_group_t *menu_group = *config_.menu_group;
  lv_group_remove_all_objs(menu_group);
  // The menu wraps: down from the burger key is the top row again, up from
  // the top row is the key. It is the one list short enough that going round
  // is quicker than going back.
  lv_group_set_wrap(menu_group, true);
  if (sub) {
    lv_group_add_obj(menu_group, ui_comp_get_child(overlay, kNavSubBackId));
  }
  for (int i = sub ? NAV_TOP_COUNT : 0; i < (sub ? NAV_DEST_COUNT : NAV_TOP_COUNT); i++) {
    lv_group_add_obj(menu_group, ui_comp_get_child(overlay, kNavRowIds[static_cast<size_t>(i)]));
  }
  if (Chrome *c = chrome_of(lv_screen_active())) {
    lv_group_add_obj(menu_group, c->key);
  }
  lv_indev_set_group(*config_.indev, menu_group);
  lv_group_focus_obj(focus);
}

// Always opens at the top level, wherever it was last left.
void hmi::ui::NavView::open_menu(lv_obj_t *overlay) {
  if (*config_.menu_open != nullptr) {
    return;
  }
  *config_.menu_open = overlay;
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  menu_slide(overlay, MENU_HIDDEN_Y, MENU_SHOWN_Y, false);
  key_open_look(lv_screen_active(), true);
  menu_level(overlay, false, ui_comp_get_child(overlay, kNavRowIds[0]));
  config_.gate_update();
}

// Where DRIVE in the band goes: the Drive screen while the chair is driving,
// the Locked screen -- where holding the stick button asks for it -- while it is not. Either way
// it is where the chair's state is, which is what "home" has to mean.
void hmi::ui::NavView::home() {
  close_menu();
  if (lv_subject_get_int(config_.shared->locked) != 0) {
    _ui_screen_change(&ui_LockedScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, HOME_FADE_MS, 0,
                      &ui_LockedScreen_screen_init);
  } else {
    _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, HOME_FADE_MS, 0,
                      &ui_DriveScreen_screen_init);
  }
}

// Swaps the screen underneath. The overlay that was up belonged to the screen
// being left and goes with it; the destination's own overlay replays the drop
// (drop_row_, in arrive), which is the spec's "the screen underneath swaps to
// the destination, then the panel drops".
void hmi::ui::NavView::go(NavDest dest) {
  const lv_obj_t *const before = lv_screen_active();
  lv_obj_t *const open = *config_.menu_open;
  if (open != nullptr) {
    lv_obj_add_flag(open, LV_OBJ_FLAG_HIDDEN);
    for (uint32_t id : kNavRowIds) {
      lv_obj_remove_state(ui_comp_get_child(open, id), LV_STATE_CHECKED);
    }
    key_open_look(lv_screen_active(), false);
    lv_group_remove_all_objs(*config_.menu_group);
    *config_.menu_open = nullptr;
    menu_sub_ = false;
    drop_row_ = dest;
  }
  switch (dest) {
  case NAV_DRIVE:
    // Where driving is: Drive while the chair drives, the Locked screen -- with
    // the hold that asks for it -- while it does not.
    if (lv_subject_get_int(config_.shared->locked) != 0) {
      _ui_screen_change(&ui_LockedScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                        &ui_LockedScreen_screen_init);
    } else {
      _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                        &ui_DriveScreen_screen_init);
    }
    break;
  case NAV_SEAT:
    _ui_screen_change(&ui_SeatScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_SeatScreen_screen_init);
    break;
  case NAV_LOG:
    _ui_screen_change(&ui_LogScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_LogScreen_screen_init);
    break;
  case NAV_BENCH:
    // The PIN gate, not the motors: passing it is what opens the actuator page.
    _ui_screen_change(&ui_BenchGateScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                      &ui_BenchGateScreen_screen_init);
    break;
  case NAV_SETTINGS:
    break; // a level of the menu, not a screen: row_cb opens it
  case NAV_SKUNK:
  case NAV_DIAG:
  case NAV_SET_DISPLAY:
  case NAV_SET_STICK:
    config_.open_dest(dest); // built on demand by their views
    break;
  case NAV_INTERNET:
    _ui_screen_change(&ui_InternetScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                      &ui_InternetScreen_screen_init);
    break;
  case NAV_UPDATE:
    _ui_screen_change(&ui_UpdateScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                      &ui_UpdateScreen_screen_init);
    break;
  case NAV_ABOUT:
    _ui_screen_change(&ui_AboutScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_AboutScreen_screen_init);
    break;
  case NAV_JOYSTICK:
    _ui_screen_change(&ui_JoystickScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                      &ui_JoystickScreen_screen_init);
    break;
  case NAV_DEST_COUNT:
    break;
  }
  // A row picked on its own screen (one Settings page from another): LVGL skips
  // a load of the screen already up, so SCREEN_LOADED never comes, and the
  // stick would be left on the menu's group -- emptied above -- with nothing
  // to focus until another screen loaded. Arrive by hand instead.
  // Filled by name, not by position, so reordering the menu cannot shift it.
  std::array<lv_obj_t *, NAV_DEST_COUNT> dest_screens{};
  dest_screens[NAV_DRIVE] =
      lv_subject_get_int(config_.shared->locked) != 0 ? ui_LockedScreen : ui_DriveScreen;
  dest_screens[NAV_SEAT] = ui_SeatScreen;
  dest_screens[NAV_BENCH] = ui_BenchGateScreen;
  dest_screens[NAV_DIAG] = ui_DiagnosticsScreen;
  dest_screens[NAV_JOYSTICK] = ui_JoystickScreen;
  dest_screens[NAV_LOG] = ui_LogScreen;
  dest_screens[NAV_SKUNK] = ui_SkunkWorksScreen;
  dest_screens[NAV_SET_DISPLAY] = ui_SettingsScreen;
  dest_screens[NAV_SET_STICK] = ui_SettingsScreen;
  dest_screens[NAV_INTERNET] = ui_InternetScreen;
  dest_screens[NAV_UPDATE] = ui_UpdateScreen;
  dest_screens[NAV_ABOUT] = ui_AboutScreen;
  if (dest < NAV_DEST_COUNT && dest_screens[static_cast<size_t>(dest)] == before &&
      lv_screen_active() == before) {
    arrive(before);
  }
  config_.gate_update();
}

void hmi::ui::NavView::press_done_cb(lv_timer_t *t) {
  auto *row = static_cast<Row *>(lv_timer_get_user_data(t));
  row->view->press_timer_ = nullptr; // repeat count 1: LVGL deletes it after this
  row->view->go(row->dest);
}

// A row picked, by touch or by the stick button: it turns negative and holds
// ROW_PRESS_MS, then the screen underneath changes.
void hmi::ui::NavView::row_cb(lv_event_t *e) {
  auto *row = static_cast<Row *>(lv_event_get_user_data(e));
  row->view->row_picked(e, row->dest);
}

void hmi::ui::NavView::row_picked(lv_event_t *e, NavDest dest) {
  if (press_timer_ != nullptr) {
    return; // one pick at a time
  }
  // Settings changes the menu's level rather than the screen: at once, with no
  // row press to wait out, since nothing underneath moves.
  if (dest == NAV_SETTINGS) {
    if (*config_.menu_open != nullptr) {
      menu_level(*config_.menu_open, true,
                 ui_comp_get_child(*config_.menu_open, kNavRowIds[NAV_TOP_COUNT]));
    }
    return;
  }
  // Seat Functions needs the MCB, as the old seat page did: refused on the
  // spot rather than opening a screen whose every button would be refused in
  // turn. The menu stays up, the row stays greyed, and the reason is waiting on
  // the banner underneath once it closes.
  if (dest == NAV_SEAT && !config_.mcb_ready()) {
    config_.refuse_seat();
    return;
  }
  // Drive the same way, while there is a drive to ask for: driving already,
  // the row only goes back to the Drive screen.
  // The drive session refuses it (rows 40-41: locked, MCB not ready).
  if (dest == NAV_DRIVE && config_.drive_row()) {
    return;
  }
  lv_obj_add_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
  press_timer_ = lv_timer_create(press_done_cb, ROW_PRESS_MS, lv_event_get_user_data(e));
  lv_timer_set_repeat_count(press_timer_, 1);
}

// "< Settings": back up to the top level, the cursor on Settings.
void hmi::ui::NavView::sub_back_cb(lv_event_t *e) {
  auto *view = static_cast<NavView *>(lv_event_get_user_data(e));
  lv_obj_t *const open = *view->config_.menu_open;
  if (view->press_timer_ == nullptr && open != nullptr) {
    view->menu_level(open, false, ui_comp_get_child(open, kNavRowIds[NAV_SETTINGS]));
  }
}

// The key toggles.
void hmi::ui::NavView::key_cb(lv_event_t *e) {
  auto *chrome = static_cast<Chrome *>(lv_event_get_user_data(e));
  if (chrome == nullptr || chrome->view == nullptr) {
    return; // a key whose screen found no free chrome slot (16 for 13 screens)
  }
  NavView *view = chrome->view;
  lv_obj_t *overlay = chrome->overlay;
  if (*view->config_.menu_open == overlay) {
    view->close_menu();
    return;
  }
  // While driving, the menu is the way off the Drive screen, and whether
  // driving stops is the MIB's call. So the key asks it to stop, and the menu
  // opens over the Locked screen once it has (the drive session's relock). If
  // the MIB keeps driving, the Drive screen stays and its banner says so. While
  // a stop is already being waited for, the key does nothing (rows 25-28).
  if (lv_screen_active() == ui_DriveScreen &&
      lv_subject_get_int(view->config_.shared->locked) == 0) {
    view->config_.drive_key();
    return;
  }
  view->open_menu(overlay);
}

// The stick on a menu row. The keypad indev hands arrows to the focused object
// rather than moving the group, so the walk is done here. Right is "go", like
// the chevron says; left is "back": up a level from Settings' rows, and closed
// from the top, like the key.
void hmi::ui::NavView::row_key_cb(lv_event_t *e) {
  auto *view = static_cast<NavView *>(lv_event_get_user_data(e));
  lv_obj_t *row = lv_event_get_target_obj(e);
  lv_obj_t *const open = *view->config_.menu_open;
  const bool back_row = open != nullptr && row == ui_comp_get_child(open, kNavSubBackId);
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    lv_group_focus_prev(lv_obj_get_group(row));
    return;
  case LV_KEY_DOWN:
    lv_group_focus_next(lv_obj_get_group(row));
    return;
  case LV_KEY_RIGHT:
    if (!back_row) { // its chevron points the other way
      lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    }
    return;
  case LV_KEY_LEFT:
  case LV_KEY_ESC:
    if (view->menu_sub_ && open != nullptr) {
      view->menu_level(open, false, ui_comp_get_child(open, kNavRowIds[NAV_SETTINGS]));
    } else {
      view->close_menu();
    }
    return;
  default:
    return;
  }
}

// The stick on the burger key. It is always last in its group: up goes back
// to whatever is above; down goes on only in the open menu, which wraps round
// to its top row (a screen's own group does not wrap, so there it stays put).
void hmi::ui::NavView::key_key_cb(lv_event_t *e) {
  const uint32_t key = lv_event_get_key(e);
  lv_group_t *g = lv_obj_get_group(lv_event_get_target_obj(e));
  if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
    lv_group_focus_prev(g);
  } else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
    lv_group_focus_next(g);
  }
}

// A plain button in a screen's group (Calibrate): down to the
// next member -- in practice, the key.
void hmi::ui::NavView::button_key_cb(lv_event_t *e) {
  const uint32_t key = lv_event_get_key(e);
  lv_group_t *g = lv_obj_get_group(lv_event_get_target_obj(e));
  if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
    lv_group_focus_next(g);
  } else if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
    lv_group_focus_prev(g);
  }
}

// A button that sits in a screen's focus group: ringed when the cursor is on
// it, inverted when pressed, and walkable with the stick.
void hmi::ui::NavView::focusable_button(lv_obj_t *button) {
  focus_ring(button);
  mirror_states(button);
  lv_obj_add_event_cb(button, button_key_cb, LV_EVENT_KEY, nullptr);
}

void hmi::ui::NavView::band_cb(lv_event_t *e) {
  static_cast<NavView *>(lv_event_get_user_data(e))->home();
}

// Only the outermost object of a composite stays clickable, so the whole of it
// is the hit target rather than whichever child happens to be under the finger.
//
// All the way down, not one level: LVGL hit-tests the DEEPEST clickable object
// under the point, and lv_obj_create() makes every container clickable. The
// burger key is a Ground holding three Bars, and the three bars are what a
// finger actually lands on -- clearing only Ground left the key dead in the
// middle, which is the whole of it anyone aims at.
static void nav_clear_clickable(lv_obj_t *obj) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    lv_obj_remove_flag(child, LV_OBJ_FLAG_CLICKABLE);
    nav_clear_clickable(child);
  }
}

void hmi::ui::NavView::claim_clicks(lv_obj_t *obj) {
  clear_click_focusable_recursive(obj);
  nav_clear_clickable(obj);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

// One screen's chrome. `band` is the DriveBand whose DRIVE cell goes home.
void hmi::ui::NavView::attach_chrome(lv_obj_t *key, lv_obj_t *overlay, lv_obj_t *band) {
  lv_obj_t *screen = lv_obj_get_screen(key);
  // This screen's slot if it had one (a screen rebuilt on demand), else the
  // first free one.
  Chrome *const slot = std::find_if(chrome_.begin(), chrome_.end(), [screen](const Chrome &c) {
    return c.screen == nullptr || c.screen == screen;
  });
  if (slot != chrome_.end()) {
    *slot = {screen, key, overlay, this};
  }
  lv_obj_add_event_cb(key, chrome_forget_cb, LV_EVENT_DELETE, this);

  config_.keep_overlay_fill(overlay);
  claim_clicks(key);
  mirror_states(key);
  // Not on the Drive screen: there the key is the only thing that ever holds
  // focus, and a ring round it for as long as someone drives is noise.
  if (screen != ui_DriveScreen) {
    key_focus_ring(key);
  } else {
    ring_style(key, 0, 0); // not even the theme's
  }
  lv_obj_add_event_cb(key, key_cb, LV_EVENT_CLICKED, slot != chrome_.end() ? slot : nullptr);
  lv_obj_add_event_cb(key, key_key_cb, LV_EVENT_KEY, nullptr);

  for (int i = 0; i < NAV_DEST_COUNT; i++) {
    const auto index = static_cast<size_t>(i);
    rows_[index] = Row{.view = this, .dest = static_cast<NavDest>(i)};
    lv_obj_t *row = ui_comp_get_child(overlay, kNavRowIds[index]);
    claim_clicks(row);
    // Exported in its focused preview state; the group is left as the only
    // writer of LV_STATE_FOCUSED, and the mirror carries it to the labels.
    set_focused_recursive(row, false);
    mirror_states(row);
    ring_style(row, 0, 0); // the row's negative is its cursor; no outline
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, &rows_[index]);
    lv_obj_add_event_cb(row, row_key_cb, LV_EVENT_KEY, this);
  }
  // Settings' level: opaque over the top rows, and clickable so a tap below its
  // last row does not reach the top-level row underneath.
  lv_obj_t *sub = ui_comp_get_child(overlay, kNavSubMenuId);
  config_.keep_overlay_fill(sub);
  lv_obj_add_flag(sub, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *back = ui_comp_get_child(overlay, kNavSubBackId);
  claim_clicks(back);
  set_focused_recursive(back, false);
  mirror_states(back);
  ring_style(back, 0, 0);
  lv_obj_add_event_cb(back, sub_back_cb, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(back, row_key_cb, LV_EVENT_KEY, this);
  // Drive and Seat Functions grey while the MCB could not act on them, the same
  // test and the same look as the MCB tiles on Skunk Works -- still walkable,
  // so the cursor never sticks on one, and refused by row_cb if picked.
  for (NavDest gated : {NAV_DRIVE, NAV_SEAT}) {
    lv_obj_t *row = ui_comp_get_child(overlay, kNavRowIds[static_cast<size_t>(gated)]);
    lv_obj_set_style_opa(row, LV_OPA_40, ActionsView::UNAVAILABLE_STYLE);
    for (lv_subject_t *subject : {config_.shared->rtps_link, config_.shared->mib_state}) {
      lv_subject_add_observer_obj(subject, config_.ready_observer, row, nullptr);
    }
  }

  if (band != nullptr) {
    claim_clicks(ui_comp_get_child(band, UI_COMP_DRIVEBAND_DRIVECELL));
    lv_obj_add_event_cb(ui_comp_get_child(band, UI_COMP_DRIVEBAND_DRIVECELL), band_cb,
                        LV_EVENT_CLICKED, this);
  }
}

// Replays the row press on the destination's own overlay: shown where the old
// one was, the picked row negative, then slid away over the new screen.
void hmi::ui::NavView::drop_menu(const lv_obj_t *screen) {
  const int row = drop_row_;
  drop_row_ = -1;
  const Chrome *c = chrome_of(screen);
  if (row < 0 || c == nullptr) {
    return;
  }
  lv_obj_remove_flag(c->overlay, LV_OBJ_FLAG_HIDDEN);
  // A pick from Settings' level is replayed on that level.
  lv_obj_set_flag(ui_comp_get_child(c->overlay, kNavSubMenuId), LV_OBJ_FLAG_HIDDEN,
                  row < NAV_TOP_COUNT);
  lv_obj_add_state(ui_comp_get_child(c->overlay, kNavRowIds[static_cast<size_t>(row)]),
                   LV_STATE_CHECKED);
  menu_slide(c->overlay, MENU_SHOWN_Y, MENU_HIDDEN_Y, true);
}

// The active screen, by name. Only the remote UI asks, but the table is worth
// having in one place: a screen missing from it reports "?" rather than lying.
const char *hmi::ui::NavView::screen_name(const lv_obj_t *screen) {
  struct Named {
    lv_obj_t *const *obj;
    const char *name;
  };
  static constexpr std::array<Named, 14> kScreens{{
      {&ui_BootScreen, "BootScreen"},
      {&ui_LockedScreen, "LockedScreen"},
      {&ui_DriveScreen, "DriveScreen"},
      {&ui_SeatScreen, "SeatScreen"},
      {&ui_JoystickScreen, "JoystickScreen"},
      {&ui_BenchGateScreen, "BenchGateScreen"},
      {&ui_BenchMotorsScreen, "BenchMotorsScreen"},
      {&ui_LogScreen, "LogScreen"},
      {&ui_InternetScreen, "InternetScreen"},
      {&ui_AboutScreen, "AboutScreen"},
      {&ui_UpdateScreen, "UpdateScreen"},
      {&ui_SettingsScreen, "SettingsScreen"},
      {&ui_SkunkWorksScreen, "SkunkWorksScreen"},
      {&ui_DiagnosticsScreen, "DiagnosticsScreen"},
  }};
  const auto *const found = std::find_if(kScreens.begin(), kScreens.end(),
                                         [screen](const Named &e) { return *e.obj == screen; });
  return found != kScreens.end() ? found->name : "?";
}

// Everything that happens when a screen comes up: on SCREEN_LOADED, or by
// hand from go when the destination was already the screen up.
void hmi::ui::NavView::arrive(const lv_obj_t *screen) {
  // The overlay that was up belonged to the screen being left. It was closed on
  // the way out, but a screen reached any other way (the unlock timer, a
  // completed hold) has to leave the menu behind too.
  *config_.menu_open = nullptr;
  config_.enter_screen(screen);
  drop_menu(screen);
  // The burger key on Drive asked the MIB to stop, and it has: the menu the
  // key was pressed for, over the Locked screen that came up.
  if (*config_.menu_on_arrival) {
    *config_.menu_on_arrival = false;
    if (Chrome *c = chrome_of(screen)) {
      open_menu(c->overlay);
    }
  }
  config_.gate_update();
  config_.arrived(screen);
}

// Only the stick may drive, and only on the Drive screen with the menu shut:
// everywhere else the same stick is walking focus around, and a push meant for
// the next row must not also move the chair. The ADC task reads this and sends
// the MCB a centred stick whenever it is false.
// The rule is the drive session's pure stick_drives (drive_session_table.hpp).
static void nav_update_stick_gate() {
  stick_drives.store(hmi::drive_session::stick_drives(lv_subject_get_int(&locked_subject) != 0,
                                                      drive_screen_of(lv_screen_active()),
                                                      nav_menu_open != nullptr));
}

static void nav_use_group(lv_group_t *g, const lv_obj_t *screen);

// Hands the joystick to the screen being shown and puts the cursor at its top.
// Called on load; closing the menu restores the group without this reset.
static void nav_enter_screen(const lv_obj_t *screen) {
  if (screen == ui_SeatScreen) {
    seat_buttons_grid.row = 0;
    seat_buttons_grid.col = 0;
    seat_show_buttons_page();
  } else if (screen == ui_BenchGateScreen) {
    nav_use_group(rd_group, screen);
    rd_focus(0);
  } else if (screen == ui_SettingsScreen) {
    nav_use_group(setting_group, screen);
    setting_focus(0);
  } else if (screen == ui_SkunkWorksScreen) {
    nav_use_group(actions_group, screen);
    action_focus(0);
  } else if (screen == ui_DiagnosticsScreen) {
    nav_use_group(diag_group, screen);
    diag_focus(0);
  } else if (screen == ui_InternetScreen) {
    internet_ui_on_load(); // its own groups, one per page
  } else if (screen == ui_UpdateScreen) {
    update_ui_on_load(); // its own groups, one per page
  } else if (screen == ui_AboutScreen) {
    about_ui_on_load();
    lv_group_remove_all_objs(joystick_group); // nothing to pick: only the key
    nav_use_group(joystick_group, screen);
  } else if (screen == ui_LogScreen && log_view_group() != nullptr) {
    nav_use_group(log_view_group(), screen);
    log_view_on_load();
  } else {
    // The screens with at most a button or two of their own share one group,
    // refilled for whichever is up.
    lv_group_remove_all_objs(joystick_group);
    if (screen == ui_JoystickScreen) {
      lv_group_add_obj(joystick_group, ui_CalibrateButton);
    }
    nav_use_group(joystick_group, screen);
  }
}

// The one instance (hmi::ui::NavView): every screen's key and menu, and arriving.
static constinit hmi::ui::NavView nav_view{{
    .shared = &ui_shared_subjects,
    .menu_slide = &menu_slide_subject,
    .menu_open = &nav_menu_open,
    .menu_on_arrival = &nav_menu_on_arrival,
    .menu_group = &menu_group,
    .indev = &joystick_indev,
    .gate_update = nav_update_stick_gate,
    .keep_overlay_fill = keep_overlay_fill,
    .ready_observer = action_ready_observer,
    .mcb_ready = mcb_ready,
    // Refused where it was picked: the feedback, and the banner underneath.
    .refuse_seat =
        [] {
          refusal_feedback();
          entry_refused_show(kRefusedSeat, kDriveRefusedShowMs);
        },
    .drive_row = [] { return drive_session_input(hmi::drive_session::Input::MENU_ROW_DRIVE); },
    .drive_key = [] { (void)drive_session_input(hmi::drive_session::Input::MENU_KEY_DRIVE); },
    .open_dest =
        [](NavDest dest) {
          switch (dest) {
          case hmi::ui::NAV_SKUNK:
            actions_open();
            break;
          case hmi::ui::NAV_DIAG:
            diagnostics_open();
            break;
          case hmi::ui::NAV_SET_DISPLAY:
            setting_page_open(SETTINGS_PAGE_DISPLAY);
            break;
          case hmi::ui::NAV_SET_STICK:
            setting_page_open(SETTINGS_PAGE_STICK);
            break;
          default:
            break;
          }
        },
    .enter_screen = nav_enter_screen,
    // The PIN is asked again on every visit rather than latching once per boot.
    .arrived =
        [](const lv_obj_t *screen) {
          if (screen == ui_BenchGateScreen) {
            rd_pin_reset();
          }
        },
}};

// The calls the rest of the unit makes (and NavPort's table).
static void nav_use_group(lv_group_t *g, const lv_obj_t *screen) { nav_view.use_group(g, screen); }
static void nav_to_key() { nav_view.to_key(); }
static void nav_mirror_states(lv_obj_t *obj) { NavView::mirror_states(obj); }
static void nav_focus_ring(lv_obj_t *obj) { NavView::focus_ring(obj); }
static void nav_focusable_button(lv_obj_t *button) { NavView::focusable_button(button); }
static void nav_claim_clicks(lv_obj_t *obj) { NavView::claim_clicks(obj); }
static void nav_home() { nav_view.home(); }
static void nav_arrive(const lv_obj_t *screen) { nav_view.arrive(screen); }
static void nav_attach_chrome(lv_obj_t *key, lv_obj_t *overlay, lv_obj_t *band) {
  nav_view.attach_chrome(key, overlay, band);
}
static void screen_loaded_cb(lv_event_t *e) { nav_arrive(lv_event_get_target_obj(e)); }
static const char *active_screen_name() { return NavView::screen_name(lv_screen_active()); }
