// split_main.py fragment of main.cpp (frag_nav: original lines 3230-3999); one TU, see main.cpp.
// p21 "HOME BUTTON": a dissolve from any screen back to Drive.
static constexpr uint32_t kHomeFadeMs = 120;
// How long an updated image runs before it keeps itself (github_ota_boot_confirm).
static constexpr uint32_t kOtaConfirmAfterMs = 30'000;

// The destinations, in the order MenuOverlay draws them, and the child id of
// each row. The menu has two levels: the top rows, and Settings' own rows on
// the SubMenu panel (after its "< Settings" row, which is the way back up).
// Adding a row in SquareLine means a line in each of these two and a case in
// nav_go -- the row order is the menu order, nothing else encodes it.
enum NavDest {
  NAV_DRIVE, // "Drive": the Drive screen, or Locked (where it is asked for)
  NAV_SEAT,  // "Seat Functions"
  // The rest alphabetically.
  NAV_BENCH,    // "Bench", behind the PIN gate
  NAV_DIAG,     // "Diagnostics"
  NAV_JOYSTICK, // "Joystick", the test screen with CALIBRATE on it
  NAV_LOG,      // "Log"
  NAV_SETTINGS, // "Settings": opens the level below rather than a screen
  NAV_SKUNK,    // "Skunk Works"
  NAV_TOP_COUNT,
  // Settings' level, in SubMenu order.
  NAV_SET_DISPLAY = NAV_TOP_COUNT, // "Display & sound": a Settings page
  NAV_SET_STICK,                   // "Joystick & driving": a Settings page
  NAV_INTERNET,                    // "Internet": Ethernet or WiFi, and which network
  NAV_UPDATE,                      // "Firmware update": a release from GitHub
  NAV_ABOUT,                       // "About": the firmware, and the board
  NAV_DEST_COUNT,
};

static const uint32_t kNavRowIds[NAV_DEST_COUNT] = {
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

// Each screen's key and overlay, filled in as nav_attach_chrome wires them, so
// "the burger key on the screen that is up" is one lookup. The screens built on
// demand come and go, so an entry is emptied when its key is deleted.
struct NavChrome {
  lv_obj_t *screen;
  lv_obj_t *key;
  lv_obj_t *overlay;
};
static NavChrome nav_chrome[16];

static NavChrome *nav_chrome_of(const lv_obj_t *screen) {
  NavChrome *const end = std::end(nav_chrome);
  NavChrome *const found = std::find_if(std::begin(nav_chrome), end, [screen](const NavChrome &c) {
    return c.screen != nullptr && c.screen == screen;
  });
  return found != end ? found : nullptr;
}

static void nav_chrome_forget_cb(lv_event_t *e) {
  const lv_obj_t *key = lv_event_get_target_obj(e);
  std::replace_if(
      std::begin(nav_chrome), std::end(nav_chrome),
      [key](const NavChrome &c) { return c.key == key; }, NavChrome{});
}

// The group the screen underneath uses, so closing the menu can hand it back.
static lv_group_t *nav_screen_group = nullptr;

// A row picked and holding negative, waiting out kRowPressMs; and the row whose
// press the destination's own overlay replays on arrival, sliding away.
static lv_timer_t *nav_press_timer = nullptr;
static int nav_drop_row = -1;
// Which level the open menu shows: false = the top rows, true = Settings'.
static bool nav_menu_sub = false;

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

static void nav_mirror_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_current_target_obj(e);
  nav_mirror_to(obj, static_cast<lv_state_t>(lv_obj_get_state(obj) & kMirroredStates));
}

static void nav_mirror_states(lv_obj_t *obj) {
  lv_obj_add_event_cb(obj, nav_mirror_cb, LV_EVENT_STATE_CHANGED, nullptr);
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
static void nav_ring_style(lv_obj_t *obj, int32_t width, int32_t pad) {
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

static void nav_focus_ring(lv_obj_t *obj) { nav_ring_style(obj, 4, 6); }

// The burger key spans the screen edge to edge, so an outside ring would be
// off-screen and an inside one on the key itself would be painted over by its
// Ground. The ring goes on the Ground, inset -- the Ground sees the key's
// focus through nav_mirror_states -- and the key's own outline is switched off.
static void nav_key_focus_ring(lv_obj_t *key) {
  nav_ring_style(key, 0, 0);
  lv_obj_t *ground = ui_comp_get_child(key, UI_COMP_MENUKEY_GROUND);
  nav_ring_style(ground, 4, -14);
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
static void nav_use_group(lv_group_t *g, const lv_obj_t *screen) {
  lv_group_set_wrap(g, false);
  if (NavChrome *c = nav_chrome_of(screen)) {
    lv_group_add_obj(g, c->key); // removes it from wherever it was first
  }
  nav_screen_group = g;
  lv_indev_set_group(joystick_indev, g);
}

// Down past the end of a list, grid or log: on to the burger key.
static void nav_to_key() {
  if (NavChrome *c = nav_chrome_of(lv_screen_active())) {
    lv_group_focus_obj(c->key);
  }
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

// --- the menu ------------------------------------------------------------------

static void nav_menu_anim_cb(void *obj, int32_t y) {
  lv_obj_set_y(static_cast<lv_obj_t *>(obj), y);
}

static void nav_menu_hide_cb(lv_anim_t *a) {
  auto *overlay = static_cast<lv_obj_t *>(a->var);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  // A row replayed on arrival (nav_drop_row) goes back to resting once it is
  // out of sight, and the overlay to its top level, ready for the next time it
  // opens.
  for (uint32_t id : kNavRowIds) {
    lv_obj_remove_state(ui_comp_get_child(overlay, id), LV_STATE_CHECKED);
  }
  lv_obj_add_flag(ui_comp_get_child(overlay, kNavSubMenuId), LV_OBJ_FLAG_HIDDEN);
}

static void nav_menu_slide(lv_obj_t *overlay, int32_t from, int32_t to, bool hide_after) {
  lv_anim_delete(overlay, nav_menu_anim_cb);
  // Instant unless Settings turns the slide on: straight to where the
  // slide would have ended, hidden if that is where it was going.
  if (lv_subject_get_int(&menu_slide_subject) == 0) {
    lv_obj_set_y(overlay, to);
    if (hide_after) {
      lv_anim_t done;
      lv_anim_init(&done);
      lv_anim_set_var(&done, overlay);
      nav_menu_hide_cb(&done);
    }
    return;
  }
  lv_obj_set_y(overlay, from);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, overlay);
  lv_anim_set_exec_cb(&a, nav_menu_anim_cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, kMenuSlideMs);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  if (hide_after) {
    lv_anim_set_completed_cb(&a, nav_menu_hide_cb);
  }
  lv_anim_start(&a);
}

static void nav_key_open_look(const lv_obj_t *screen, bool open) {
  if (NavChrome *c = nav_chrome_of(screen)) {
    lv_obj_set_state(c->key, LV_STATE_CHECKED, open);
  }
}

// Closes the menu over the screen that is up and gives the stick back to it,
// on the key the menu was opened from.
static void nav_close_menu() {
  if (nav_menu_open == nullptr) {
    return;
  }
  lv_obj_t *overlay = nav_menu_open;
  nav_menu_open = nullptr;
  nav_menu_sub = false; // the overlay goes back to its top level once hidden
  if (nav_press_timer != nullptr) {
    lv_timer_delete(nav_press_timer);
    nav_press_timer = nullptr;
  }
  nav_key_open_look(lv_screen_active(), false);
  nav_menu_slide(overlay, lv_obj_get_y(overlay), kMenuHiddenY, true);
  lv_group_remove_all_objs(menu_group);
  if (nav_screen_group != nullptr) {
    nav_use_group(nav_screen_group, lv_screen_active());
    nav_to_key();
  }
  nav_update_stick_gate();
}

// Shows one level of the open menu -- the top rows, or Settings' over them --
// and gives the stick that level's rows, the cursor on `focus`.
static void nav_menu_level(lv_obj_t *overlay, bool sub, lv_obj_t *focus) {
  nav_menu_sub = sub;
  lv_obj_set_flag(ui_comp_get_child(overlay, kNavSubMenuId), LV_OBJ_FLAG_HIDDEN, !sub);

  // The rows belong to whichever overlay is up, so the group is rebuilt rather
  // than filled once: every screen has its own instance of all of them. The
  // key goes last, so down from the bottom row reaches it and a press closes.
  lv_group_remove_all_objs(menu_group);
  // The menu wraps: down from the burger key is the top row again, up from
  // the top row is the key. It is the one list short enough that going round
  // is quicker than going back.
  lv_group_set_wrap(menu_group, true);
  if (sub) {
    lv_group_add_obj(menu_group, ui_comp_get_child(overlay, kNavSubBackId));
  }
  for (int i = sub ? NAV_TOP_COUNT : 0; i < (sub ? NAV_DEST_COUNT : NAV_TOP_COUNT); i++) {
    lv_group_add_obj(menu_group, ui_comp_get_child(overlay, kNavRowIds[i]));
  }
  if (NavChrome *c = nav_chrome_of(lv_screen_active())) {
    lv_group_add_obj(menu_group, c->key);
  }
  lv_indev_set_group(joystick_indev, menu_group);
  lv_group_focus_obj(focus);
}

// Into Settings' level, the cursor on its first section; and back up, the
// cursor on Settings.
static void nav_menu_enter_settings(lv_obj_t *overlay) {
  nav_menu_level(overlay, true, ui_comp_get_child(overlay, kNavRowIds[NAV_TOP_COUNT]));
}

static void nav_menu_leave_settings(lv_obj_t *overlay) {
  nav_menu_level(overlay, false, ui_comp_get_child(overlay, kNavRowIds[NAV_SETTINGS]));
}

// Always opens at the top level, wherever it was last left.
static void nav_open_menu(lv_obj_t *overlay) {
  if (nav_menu_open != nullptr) {
    return;
  }
  nav_menu_open = overlay;
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  nav_menu_slide(overlay, kMenuHiddenY, kMenuShownY, false);
  nav_key_open_look(lv_screen_active(), true);
  nav_menu_level(overlay, false, ui_comp_get_child(overlay, kNavRowIds[0]));
  nav_update_stick_gate();
}

// Where DRIVE in the band goes: the Drive screen while the chair is driving,
// the Locked screen -- where holding the stick button asks for it -- while it is not. Either way
// it is where the chair's state is, which is what "home" has to mean.
static void nav_home() {
  nav_close_menu();
  if (lv_subject_get_int(&locked_subject) != 0) {
    _ui_screen_change(&ui_LockedScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, kHomeFadeMs, 0,
                      &ui_LockedScreen_screen_init);
  } else {
    _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, kHomeFadeMs, 0,
                      &ui_DriveScreen_screen_init);
  }
}

// Swaps the screen underneath. The overlay that was up belonged to the screen
// being left and goes with it; the destination's own overlay replays the drop
// (nav_drop_row, in screen_loaded_cb), which is the spec's "the screen
// underneath swaps to the destination, then the panel drops".
static void nav_go(NavDest dest) {
  const lv_obj_t *const before = lv_screen_active();
  if (nav_menu_open != nullptr) {
    lv_obj_add_flag(nav_menu_open, LV_OBJ_FLAG_HIDDEN);
    for (uint32_t id : kNavRowIds) {
      lv_obj_remove_state(ui_comp_get_child(nav_menu_open, id), LV_STATE_CHECKED);
    }
    nav_key_open_look(lv_screen_active(), false);
    lv_group_remove_all_objs(menu_group);
    nav_menu_open = nullptr;
    nav_menu_sub = false;
    nav_drop_row = dest;
  }
  switch (dest) {
  case NAV_DRIVE:
    // Where driving is: Drive while the chair drives, the Locked screen -- with
    // the hold that asks for it -- while it does not.
    if (lv_subject_get_int(&locked_subject) != 0) {
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
  case NAV_SKUNK:
    actions_open();
    break;
  case NAV_LOG:
    _ui_screen_change(&ui_LogScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_LogScreen_screen_init);
    break;
  case NAV_DIAG:
    diagnostics_open();
    break;
  case NAV_BENCH:
    // The PIN gate, not the motors: passing it is what opens the actuator page.
    _ui_screen_change(&ui_BenchGateScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                      &ui_BenchGateScreen_screen_init);
    break;
  case NAV_SETTINGS:
    break; // a level of the menu, not a screen: nav_row_cb opens it
  case NAV_SET_DISPLAY:
    setting_page_open(SETTINGS_PAGE_DISPLAY);
    break;
  case NAV_SET_STICK:
    setting_page_open(SETTINGS_PAGE_STICK);
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
  lv_obj_t *dest_screens[NAV_DEST_COUNT] = {};
  dest_screens[NAV_DRIVE] =
      lv_subject_get_int(&locked_subject) != 0 ? ui_LockedScreen : ui_DriveScreen;
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
  if (dest < NAV_DEST_COUNT && dest_screens[dest] == before && lv_screen_active() == before) {
    nav_arrive(before);
  }
  nav_update_stick_gate();
}

static void nav_press_done_cb(lv_timer_t *t) {
  nav_press_timer = nullptr; // repeat count 1: LVGL deletes it after this
  nav_go(static_cast<NavDest>(reinterpret_cast<intptr_t>(lv_timer_get_user_data(t))));
}

// A row picked, by touch or by the stick button: it turns negative and holds
// kRowPressMs, then the screen underneath changes.
static void nav_row_cb(lv_event_t *e) {
  if (nav_press_timer != nullptr) {
    return; // one pick at a time
  }
  // Seat Functions needs the MCB, as the old seat page did: refused on the
  // spot rather than opening a screen whose every button would be refused in
  // turn.
  const auto dest = static_cast<NavDest>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  // Settings changes the menu's level rather than the screen: at once, with no
  // row press to wait out, since nothing underneath moves.
  if (dest == NAV_SETTINGS) {
    if (nav_menu_open != nullptr) {
      nav_menu_enter_settings(nav_menu_open);
    }
    return;
  }
  if (dest == NAV_SEAT && !mcb_ready()) {
    // Refused where it was picked: the menu stays up, the row stays greyed,
    // and the reason is waiting on the banner underneath once it closes.
    refusal_feedback();
    entry_refused_show(kRefusedSeat, kDriveRefusedShowMs);
    return;
  }
  // Drive the same way, while there is a drive to ask for: driving already,
  // the row only goes back to the Drive screen.
  // The drive session refuses it (rows 40-41: locked, MCB not ready).
  if (dest == NAV_DRIVE && drive_session_input(hmi::drive_session::Input::MENU_ROW_DRIVE)) {
    return;
  }
  lv_obj_add_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
  nav_press_timer = lv_timer_create(nav_press_done_cb, kRowPressMs, lv_event_get_user_data(e));
  lv_timer_set_repeat_count(nav_press_timer, 1);
}

// "< Settings": back up to the top level.
static void nav_sub_back_cb(lv_event_t *) {
  if (nav_press_timer == nullptr && nav_menu_open != nullptr) {
    nav_menu_leave_settings(nav_menu_open);
  }
}

// The key toggles; user_data is the overlay on the same screen.
static void nav_key_cb(lv_event_t *e) {
  auto *overlay = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  if (nav_menu_open == overlay) {
    nav_close_menu();
    return;
  }
  // While driving, the menu is the way off the Drive screen, and whether
  // driving stops is the MIB's call. So the key asks it to stop, and the menu
  // opens over the Locked screen once it has (the drive session's relock). If
  // the MIB keeps driving, the Drive screen stays and its banner says so. While
  // a stop is already being waited for, the key does nothing (rows 25-28).
  if (lv_screen_active() == ui_DriveScreen && lv_subject_get_int(&locked_subject) == 0) {
    (void)drive_session_input(hmi::drive_session::Input::MENU_KEY_DRIVE);
    return;
  }
  nav_open_menu(overlay);
}

// The stick on a menu row. The keypad indev hands arrows to the focused object
// rather than moving the group, so the walk is done here. Right is "go", like
// the chevron says; left is "back": up a level from Settings' rows, and closed
// from the top, like the key.
static void nav_row_key_cb(lv_event_t *e) {
  lv_obj_t *row = lv_event_get_target_obj(e);
  const bool back_row =
      nav_menu_open != nullptr && row == ui_comp_get_child(nav_menu_open, kNavSubBackId);
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
    if (nav_menu_sub && nav_menu_open != nullptr) {
      nav_menu_leave_settings(nav_menu_open);
    } else {
      nav_close_menu();
    }
    return;
  default:
    return;
  }
}

// The stick on the burger key. It is always last in its group: up goes back
// to whatever is above; down goes on only in the open menu, which wraps round
// to its top row (a screen's own group does not wrap, so there it stays put).
static void nav_key_key_cb(lv_event_t *e) {
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
static void nav_button_key_cb(lv_event_t *e) {
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
static void nav_focusable_button(lv_obj_t *button) {
  nav_focus_ring(button);
  nav_mirror_states(button);
  lv_obj_add_event_cb(button, nav_button_key_cb, LV_EVENT_KEY, nullptr);
}

static void nav_band_cb(lv_event_t *) { nav_home(); }

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

static void nav_claim_clicks(lv_obj_t *obj) {
  clear_click_focusable_recursive(obj);
  nav_clear_clickable(obj);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

// One screen's chrome. `band` is the DriveBand whose DRIVE cell goes home.
static void nav_attach_chrome(lv_obj_t *key, lv_obj_t *overlay, lv_obj_t *band) {
  lv_obj_t *screen = lv_obj_get_screen(key);
  // This screen's slot if it had one (a screen rebuilt on demand), else the
  // first free one.
  NavChrome *const slot =
      std::find_if(std::begin(nav_chrome), std::end(nav_chrome), [screen](const NavChrome &c) {
        return c.screen == nullptr || c.screen == screen;
      });
  if (slot != std::end(nav_chrome)) {
    *slot = {screen, key, overlay};
  }
  lv_obj_add_event_cb(key, nav_chrome_forget_cb, LV_EVENT_DELETE, nullptr);

  keep_overlay_fill(overlay);
  nav_claim_clicks(key);
  nav_mirror_states(key);
  // Not on the Drive screen: there the key is the only thing that ever holds
  // focus, and a ring round it for as long as someone drives is noise.
  if (screen != ui_DriveScreen) {
    nav_key_focus_ring(key);
  } else {
    nav_ring_style(key, 0, 0); // not even the theme's
  }
  lv_obj_add_event_cb(key, nav_key_cb, LV_EVENT_CLICKED, overlay);
  lv_obj_add_event_cb(key, nav_key_key_cb, LV_EVENT_KEY, nullptr);

  for (int i = 0; i < NAV_DEST_COUNT; i++) {
    lv_obj_t *row = ui_comp_get_child(overlay, kNavRowIds[i]);
    nav_claim_clicks(row);
    // Exported in its focused preview state; the group is left as the only
    // writer of LV_STATE_FOCUSED, and the mirror carries it to the labels.
    set_focused_recursive(row, false);
    nav_mirror_states(row);
    nav_ring_style(row, 0, 0); // the row's negative is its cursor; no outline
    lv_obj_add_event_cb(row, nav_row_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(row, nav_row_key_cb, LV_EVENT_KEY, nullptr);
  }
  // Settings' level: opaque over the top rows, and clickable so a tap below its
  // last row does not reach the top-level row underneath.
  lv_obj_t *sub = ui_comp_get_child(overlay, kNavSubMenuId);
  keep_overlay_fill(sub);
  lv_obj_add_flag(sub, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *back = ui_comp_get_child(overlay, kNavSubBackId);
  nav_claim_clicks(back);
  set_focused_recursive(back, false);
  nav_mirror_states(back);
  nav_ring_style(back, 0, 0);
  lv_obj_add_event_cb(back, nav_sub_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(back, nav_row_key_cb, LV_EVENT_KEY, nullptr);
  // Drive and Seat Functions grey while the MCB could not act on them, the same
  // test and the same look as the MCB tiles on Skunk Works -- still walkable,
  // so the cursor never sticks on one, and refused by nav_row_cb if picked.
  for (NavDest gated : {NAV_DRIVE, NAV_SEAT}) {
    lv_obj_t *row = ui_comp_get_child(overlay, kNavRowIds[gated]);
    lv_obj_set_style_opa(row, LV_OPA_40, kActionUnavailableStyle);
    for (lv_subject_t *subject : {&rtps_link_subject, &mib_state_subject}) {
      lv_subject_add_observer_obj(subject, action_ready_observer, row, nullptr);
    }
  }

  if (band != nullptr) {
    nav_claim_clicks(ui_comp_get_child(band, UI_COMP_DRIVEBAND_DRIVECELL));
    lv_obj_add_event_cb(ui_comp_get_child(band, UI_COMP_DRIVEBAND_DRIVECELL), nav_band_cb,
                        LV_EVENT_CLICKED, nullptr);
  }
}

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

// Replays the row press on the destination's own overlay: shown where the old
// one was, the picked row negative, then slid away over the new screen.
static void nav_drop_menu(const lv_obj_t *screen) {
  const int row = nav_drop_row;
  nav_drop_row = -1;
  NavChrome *c = nav_chrome_of(screen);
  if (row < 0 || c == nullptr) {
    return;
  }
  lv_obj_remove_flag(c->overlay, LV_OBJ_FLAG_HIDDEN);
  // A pick from Settings' level is replayed on that level.
  lv_obj_set_flag(ui_comp_get_child(c->overlay, kNavSubMenuId), LV_OBJ_FLAG_HIDDEN,
                  row < NAV_TOP_COUNT);
  lv_obj_add_state(ui_comp_get_child(c->overlay, kNavRowIds[row]), LV_STATE_CHECKED);
  nav_menu_slide(c->overlay, kMenuShownY, kMenuHiddenY, true);
}

// The active screen, by name. Only the remote UI asks, but the table is worth
// having in one place: a screen missing from it reports "?" rather than lying.
static const char *active_screen_name() {
  const lv_obj_t *screen = lv_screen_active();
  struct Named {
    lv_obj_t *const *obj;
    const char *name;
  };
  static const Named kScreens[] = {
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
  };
  const Named *const found = std::find_if(std::begin(kScreens), std::end(kScreens),
                                          [screen](const Named &e) { return *e.obj == screen; });
  return found != std::end(kScreens) ? found->name : "?";
}

static void screen_loaded_cb(lv_event_t *e) { nav_arrive(lv_event_get_target_obj(e)); }

// Everything that happens when a screen comes up: on SCREEN_LOADED, or by
// hand from nav_go when the destination was already the screen up.
static void nav_arrive(const lv_obj_t *screen) {
  // The overlay that was up belonged to the screen being left. It was closed on
  // the way out, but a screen reached any other way (the unlock timer, a
  // completed hold) has to leave the menu behind too.
  nav_menu_open = nullptr;
  nav_enter_screen(screen);
  nav_drop_menu(screen);
  // The burger key on Drive asked the MIB to stop, and it has: the menu the
  // key was pressed for, over the Locked screen that came up.
  if (nav_menu_on_arrival) {
    nav_menu_on_arrival = false;
    if (NavChrome *c = nav_chrome_of(screen)) {
      nav_open_menu(c->overlay);
    }
  }
  nav_update_stick_gate();
  // The PIN is asked again on every visit rather than latching once per boot.
  if (screen == ui_BenchGateScreen) {
    rd_pin_reset();
  }
}

