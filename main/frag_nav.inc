// split_main.py fragment of main.cpp (frag_nav: original lines 3230-3999); one TU, see main.cpp.
// How long an updated image runs before it keeps itself (github_ota_boot_confirm).
static constexpr uint32_t kOtaConfirmAfterMs = 30'000;

/////////////////////////////////////////////////////////////////////////////
// The burger menu, and moving around with the joystick (hmi::ui::NavView)
/////////////////////////////////////////////////////////////////////////////
#include "hmi_ui/nav_view.hpp"

using hmi::ui::NavDest;
using hmi::ui::NavView;

// Only the stick may drive, and only on the Drive screen with the menu shut:
// everywhere else the same stick is walking focus around, and a push meant for
// the next row must not also move the chair. The ADC task reads this and sends
// the MCB a centred stick whenever it is false.
// The rule is the drive session's pure stick_drives (drive_session_table.hpp).
static void nav_update_stick_gate() {
  stick_drives.store(hmi::drive_session::stick_drives(lv_subject_get_int(&locked_subject) != 0,
                                                      hmi::ui::drive_screen_of(lv_screen_active()),
                                                      nav_menu_open != nullptr));
}

static void nav_use_group(lv_group_t *g, const lv_obj_t *screen);
static lv_group_t *nav_fallback_group(); // the view's, defined after it

// Hands the joystick to the screen being shown and puts the cursor at its top.
// Called on load; closing the menu restores the group without this reset.
static void nav_enter_screen(const lv_obj_t *screen) {
  if (screen == ui_SeatScreen) {
    seat_buttons_grid.row = 0;
    seat_buttons_grid.col = 0;
    seat_show_buttons_page();
  } else if (screen == ui_BenchGateScreen) {
    nav_use_group(bench_pin_view.group(), screen);
    rd_focus(0);
  } else if (screen == ui_SettingsScreen) {
    nav_use_group(settings_view.group(), screen);
    setting_focus(0);
  } else if (screen == ui_SkunkWorksScreen) {
    nav_use_group(actions_view.group(), screen);
    action_focus(0);
  } else if (screen == ui_DiagnosticsScreen) {
    nav_use_group(diag_view.group(), screen);
    diag_focus(0);
  } else if (screen == ui_InternetScreen) {
    internet_ui_on_load(); // its own groups, one per page
  } else if (screen == ui_UpdateScreen) {
    update_ui_on_load(); // its own groups, one per page
  } else if (screen == ui_AboutScreen) {
    about_ui_on_load();
    lv_group_remove_all_objs(nav_fallback_group()); // nothing to pick: only the key
    nav_use_group(nav_fallback_group(), screen);
  } else if (screen == ui_LogScreen && log_view_group() != nullptr) {
    nav_use_group(log_view_group(), screen);
    log_view_on_load();
  } else {
    // The screens with at most a button or two of their own share one group,
    // refilled for whichever is up.
    lv_group_remove_all_objs(nav_fallback_group());
    if (screen == ui_JoystickScreen) {
      lv_group_add_obj(nav_fallback_group(), ui_CalibrateButton);
    }
    nav_use_group(nav_fallback_group(), screen);
  }
}

// The one instance (hmi::ui::NavView): every screen's key and menu, and arriving.
static void nav_cursor_lost(const lv_obj_t *screen); // logs on logger_nav, defined after it
static constinit hmi::ui::NavView nav_view{{
    .shared = &ui_shared_subjects,
    .menu_slide = setting_subjects.value(SETTINGS_PARAM_MENU_SLIDE),
    .menu_open = &nav_menu_open,
    .menu_on_arrival = &nav_menu_on_arrival,
    .gate_update = nav_update_stick_gate,
    .keep_overlay_fill = keep_overlay_fill,
    .ready_observer = action_ready_observer,
    .mcb_ready = mcb_ready,
    // Refused where it was picked: the feedback, and the banner underneath.
    .refuse_seat =
        [] {
          refusal_feedback();
          drive_port.show_refused(kRefusedSeat, kDriveRefusedShowMs);
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
    .cursor_lost = nav_cursor_lost,
}};

// The calls the rest of the unit makes (and NavPort's table).
static void nav_use_group(lv_group_t *g, const lv_obj_t *screen) { nav_view.use_group(g, screen); }
static void nav_to_key() { nav_view.to_key(); }
static lv_group_t *nav_fallback_group() { return nav_view.fallback_group(); }
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
