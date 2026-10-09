/// @file ui_app_build.cpp
/// @brief UiApp's build: app_main's wiring of the screens (moved from main/main.cpp's
///        settings_ui_init .. on_demand_parts_init), in the same order, on app_main's task,
///        before the UI task starts (CS-LAY-01).

#include "hmi_ui/ui_app.hpp"

#include "ui.h"

#include "hmi_ui/resident_chrome.hpp"
#include "settings.hpp"

namespace hmi::ui {

void UiApp::build() {
  init_settings();
  // The Joystick screen's axis bars, bound to the ADC task's percentages (JoystickView).
  joystick_view.init_bars();
  init_status_subjects();
  bind_chrome();
  bind_banners();
  init_joystick_screen();
}

void UiApp::build_input(lv_indev_t *keypad) {
  // The joystick's indev in nav, the lost-cursor backstop and the key repeat.
  nav_view_.start_input(keypad);
  init_lock_screen();
  init_screens();
  init_update_screen();
  init_screen_hooks();
  finish_build(hmi::ui::bind<&UiApp::strip_overdraw_logged>(this));
}

// UI build 1: the saved settings, the theme, the backlight and every Settings row's subject.
void UiApp::init_settings() {
  // Saved settings (settings.cpp). The theme goes on before anything is drawn (the boot
  // overdraw pass further down runs after it, as it must: a theme switch re-adds the fills that
  // pass removes). The backlight is applied as its observer is added.
  settings_load();
  if (const uint8_t theme = settings_theme();
      theme != ui_theme_idx && (theme == UI_THEME_DEFAULT || theme == UI_THEME_DAY)) {
    ui_theme_set(theme);
  }
  brightness_view_.init(settings_brightness());
  // Every other row's subject: its saved value, and its observer (SettingSubjects).
  setting_subjects.init(hmi::ui::bind<&UiApp::store_setting>(this));
  brightness_view_.start_save_timer(BRIGHTNESS_SAVE_DELAY_MS);
}

// UI build 2: the MCB, link and band subjects, before anything binds to them.
void UiApp::init_status_subjects() {
  // MCB status labels. The joystick is a slave: until the MCB says otherwise the chair is not
  // accepting drive commands, so INACTIVE/OK is the honest default. The export draws a green
  // "ACTIVE", so the initial observer run repaints it grey -- that is the point, not a flicker
  // to design away. Locked at boot, which is the screen ui_init leaves up, so the first
  // observer run is a no-op rather than a visible flicker. Before the chrome binds, because
  // lv_subject_init_int memzeroes the subject and would take any observer already on it with
  // it.
  lv_subject_init_int(&locked_, 1);
  lv_subject_init_int(&mib_state_, static_cast<int32_t>(MIB::MibSystemState::INITIALIZING));
  // Initialised before the panels bind, because their observers read it on the first run.
  // LINK_DOWN at boot is true and self-correcting: the poll timer has the real answer a
  // quarter second later.
  lv_subject_init_int(&rtps_link_, static_cast<int32_t>(LinkState::LINK_DOWN));
  lv_subject_init_int(&rtps_blink_subject, 1);
  // empty = no override, so the labels start on the enum names
  status_band_view_.init_texts();
  lv_subject_init_int(drive_band_view_.speed_subject(), 0);
  refusal_view_.init_error_texts();
}

// UI build 3: every resident screen's chrome (TopBar, band, burger key), the clock and the
// Drive band.
void UiApp::bind_chrome() {
  // Every screen ui_init built (resident_chrome.cpp). The three built on demand bind their own
  // chrome when they are built, and BenchMotorsScreen is destroyed right after ui_init, so
  // neither is here. One list rather than three, because a screen whose band, TopBar and
  // chrome do not all get bound shows a frozen readout.
  topbar_view_.init(config_.link->setting_text());
  for_each_resident_chrome(hmi::ui::bind<&UiApp::bind_resident_chrome>(this));
  // The menu stays reachable while locked: Log, Diagnostics, Settings and the bench tools are
  // all useful with the chair not driving -- and without an MCB at all. Locked still means
  // nothing moves: the band reads LOCKED on every screen and the stick only drives from Drive
  // (stick_drives).
  topbar_view_.start_clock();
  drive_band_view_.bind_speed(ui_SpeedValue);
  lv_subject_init_int(drive_band_view_.profile_subject(),
                      static_cast<int32_t>(MIB::DriveProfile::NORMAL));
  drive_band_view_.bind_profile_buttons(ui_ModeManual, ui_ModeAssist, ui_ModeAuto);
  drive_band_view_.bind_profile_mirror();
  drive_notice_view_.build(ui_DriveScreen);
}

void UiApp::bind_resident_chrome(const ScreenChrome &c) {
  bind_chrome_views(c.band, c.bar);
  nav_view_.attach_chrome(c.key, c.overlay, c.band_goes_home ? c.band : nullptr);
}

// UI build 4: the error banners, the diagnostics subjects and the 250 ms poll.
void UiApp::bind_banners() {
  // Before the panels that observe it. lv_subject_init_int memzeroes the subject, taking any
  // observer already on it with it, and the lost panels bound below watch this one so the
  // drive screen can show a refused exit.
  lv_subject_init_int(&refused_, 0);
  // Mirrored for other tasks (refused_any_task: the bench's STATE line).
  lv_subject_add_observer(&refused_, refused_mirror_observer, this);
  refusal_view_.start_timer(DRIVE_REFUSED_SHOW_MS);

  // The resident screens' error banners (RefusalView).
  refusal_view_.bind_resident_banners();
  // Diagnostics readings, and whether they are live: before the poll timer that keeps the
  // latter current, and before any RTPS sample can land.
  diag_view_.init_subjects();
  lv_timer_create(rtps_poll_cb, UiPoll::PERIOD_MS, &ui_poll_);
}

// UI build 5: the JoystickScreen's calibration and GPIO48 counter.
void UiApp::init_joystick_screen() {
  // Calibration on the JoystickScreen: holding Calibrate or the stick button starts a run
  // (calibrate_gesture_), so the button is not handed to joystick_cal as a tap-to-start --
  // only its label, which reads CANCEL during a run. Its prompts blink on the RTPS indicator's
  // phase, so this comes after rtps_blink_subject is initialised.
  NavView::focusable_button(ui_CalibrateButton);
  config_.screens->calibration_init();

  // GPIO48 test button: the count and its colour (JoystickView).
  joystick_view.init_button();
}

// UI build 6: the padlock's rest position and the three hold gestures.
void UiApp::init_lock_screen() {
  // Where the shackle sits at rest, so the 01b rise can be undone exactly, and the ring's
  // range (DriveUi).
  drive_ui_.init_lock();

  // The three push-and-hold gestures, polled by one shared timer -- only the gesture whose
  // applies() is true on the current screen can be filling at any moment. The subjects carry
  // the fill, which is how hold_poll tells a hold from a tap and what the ring and Calibrate's
  // meter are bound to.
  lv_subject_init_int(&unlock_gesture_.progress, 0);
  lv_subject_init_int(&drive_exit_gesture_.progress, 0);
  lv_subject_init_int(&calibrate_gesture_.progress, 0);
  // The ring round the padlock fills with the button hold.
  drive_ui_.bind_ring(&unlock_gesture_.progress);
  // Calibrate's meter shows the hold filling, and is out of sight while it is empty. Held by
  // touch or by the stick button, it is the same fill.
  lv_bar_set_range(ui_CalibrateFill, 0, HOLD_MAX);
  lv_bar_bind_value(ui_CalibrateFill, &calibrate_gesture_.progress);
  lv_obj_bind_flag_if_eq(ui_CalibrateFill, &calibrate_gesture_.progress, LV_OBJ_FLAG_HIDDEN, 0);
  lv_obj_remove_flag(ui_CalibrateFill, LV_OBJ_FLAG_CLICKABLE);
  lv_timer_create(hold_poll_cb, HOLD_POLL_MS, this);
}

// UI build 7: the Log, Seat, Internet and About screens.
void UiApp::init_screens() {
  // LogScreen: TextArea1 shows the serial output log_capture has kept. Its ErrorBanner5 is
  // left for menu refusals only: a link-lost banner would cover the log at exactly the moment
  // someone wants to read it. Down at the newest line leaves the log for the burger key.
  config_.screens->log_init();

  // SeatScreen: both pages, their grids and groups.
  seat_view_.init_pages();

  // InternetScreen: Ethernet or WiFi, the network list and the password page. Its two pages
  // cover the body, so their fill is what hides it.
  keep_overlay_fill(ui_NetPickPanel);
  keep_overlay_fill(ui_NetPwPanel);
  config_.screens->internet_init();
  config_.screens->about_init();
}

// UI build 8: the Update screen, and the OTA image's confirm.
void UiApp::init_update_screen() {
  // UpdateScreen: the GitHub releases, one release, and an install running. Its two pages
  // cover the body, so their fill is what hides it.
  keep_overlay_fill(ui_UpdatePickPanel);
  keep_overlay_fill(ui_UpdateRunPanel);
  config_.screens->update_init();
}

// UI build 9: the seat values and the screen-loaded hooks.
void UiApp::init_screen_hooks() {
  // The seat values, shared by this screen and the DEBUG ACTUATORS page, and the numbers bound
  // to them.
  seat_axis_count_ = static_cast<uint8_t>(rammp::kSeatAxisCount);
  seat_view_.init_values();

  // Hand the joystick between groups as the screen changes. Every screen ui_init builds, so
  // each route in and out is covered; the ones built on demand register it when built.
  for (lv_obj_t *screen :
       {ui_LockedScreen, ui_DriveScreen, ui_SeatScreen, ui_BenchGateScreen, ui_JoystickScreen,
        ui_LogScreen, ui_UpdateScreen, ui_InternetScreen, ui_AboutScreen}) {
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, &nav_view_);
  }
}

// UI build 10: what outlives the screens built on demand (BenchGate's PIN pad, Settings, Skunk
// Works, Diagnostics) and the perf overlay's font.
void UiApp::build_on_demand_parts() {
  // BenchGateScreen: the PIN pad, its four dots and the line above them.
  (void)bench_pin_view_.init();

  // SettingsScreen: what outlives the screen, which is built on demand
  // (OnDemandScreens::ensure_settings). The seat values it steps are initialised with the seat
  // screen that shares them.
  (void)settings_view_.init();

  // Initialised before the screen's warning panel ever binds to it.
  lv_subject_init_int(settings_view_.page(), SETTINGS_PAGE_DISPLAY);

  // Which page is up is chosen in the burger menu: Settings' level has a row per page
  // (NavView::go), so the screen needs no chooser of its own.

  // SkunkWorksScreen: what outlives the screen, which is built on demand.
  (void)actions_view_.init();

  // DiagnosticsScreen: what outlives the screen, which is built on demand. The menu row goes
  // through DiagnosticsView::open rather than a SquareLine screen-change action, which would
  // build the screen without any of that.
  (void)diag_view_.init();

  perf_overlay_font();
}

} // namespace hmi::ui
