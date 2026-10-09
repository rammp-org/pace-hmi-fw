/// @file ui_app.cpp
/// @brief UiApp's glue between the views (moved from main/main.cpp): readiness, the holds, the
///        seat command path, the Settings and Skunk Works observers, arrivals and the menu's
///        destinations, the screens built on demand and the overdraw pass. The build steps are
///        in ui_app_build.cpp.

#include "hmi_ui/ui_app.hpp"

#include <algorithm>
#include <chrono>

#include "esp_timer.h"
#include "ui.h"

#include "drive_session_table.hpp"
#include "drive_ui/drive_port.hpp"
#include "hmi_format/speed.hpp"
#include "hmi_format/stepper.hpp"
#include "hmi_ui/widget_tree.hpp"
#include "settings.hpp"
#include "stick/button_edges.hpp"

namespace hmi::ui {

// The poll is the drive session's tick (drive_session_table.hpp kTickPeriod).
static_assert(std::chrono::milliseconds{UiPoll::PERIOD_MS} == hmi::drive_session::kTickPeriod);
// The gestures' timings are the drive table's (drive_session_table.hpp).
static_assert(std::chrono::milliseconds{HOLD_MS} == hmi::drive_session::kHoldFill);
static_assert(std::chrono::milliseconds{HOLD_GRACE_MS} == hmi::drive_session::kBarGrace);
static_assert(std::chrono::milliseconds{HOLD_POLL_MS} == hmi::drive_session::kHoldPollPeriod);
static_assert(hmi::stick::SELECT_MAX_US == int64_t{HOLD_GRACE_MS} * 1000,
              "a press short enough to select never starts a hold filling");
// The MIB decides whether the chair drives: the joystick asks with a DriveCommand and
// shows the DriveScreen only once MibStatus says ENABLED. Two windows, because saying
// "that was refused" and giving up on the request are different jobs:
//
//  - kDriveAnswer is when to speak. One MibStatus period plus a margin, so a sample
//    already in flight when the request went out cannot be mistaken for a refusal.
//    This is as early as a refusal can honestly be known, and waiting longer just
//    reads as the HMI ignoring the user.
//  - kDriveWait is when to stop asking, so a silent MIB is not left with a live
//    request. The warning is long since up by then.
// Both are the drive table's (drive_session_table.hpp), the drive adapter's default
// windows; here they are held to the RTPS spec they come from.
static_assert(hmi::drive_session::kDriveAnswer ==
              rammp::kMibStatusPeriod + std::chrono::milliseconds{250});
static_assert(hmi::drive_session::kDriveWait == rammp::kMibStatusTimeout);
static_assert(StatusBandView::TEXT_SIZE == rammp::kMcbTextLen,
              "the band's text buffers are the shared spec's");
// MibStatus.speed is metres per second; the label shows mph to one decimal (hmi_format).
static_assert(hmi::format::MPH_PER_MPS == rammp::kMphPerMps,
              "hmi_format's mph per m/s must be the shared spec's");
static_assert(hmi::format::SPEED_MAX_TENTHS == rammp::kSpeedMaxTenths,
              "hmi_format's speed clamp must be the shared spec's");
static_assert(rammp::kDiagFields == 3, "the DiagnosticComponent has exactly three readings");
static_assert(GRID_MAX_ROWS == 4 && GRID_MAX_COLS == 3,
              "the PIN pad's bottom row is the fourth, three keys wide");
static_assert(ACTION_COUNT == 5, "every actions_spec.h entry needs its function in action_run_");

// --- Readiness, holds and drive inputs --------------------------------------------------------

// Is the MCB fit to drive, or to move the seat (DriveUi's readiness checks).
bool UiApp::mcb_ready() { return hmi::ui::mcb_ready(shared_); }
bool UiApp::seat_ready() { return hmi::ui::seat_ready(shared_); }
hmi::stick::HoldReason UiApp::hold_reason() const { return permit_hooks_.hold_reason.read(); }
hmi::stick::PostGate UiApp::post_gate() const { return permit_hooks_.post_gate.read(); }
bool UiApp::post_passed() const { return hmi::stick::post_passed(post_gate()); }
// The blocking check's own words come from the POST's TopBar indicator (hazard-c3-spec.md §2.8)
// once that is wired; until then the gate's own words.
const char *UiApp::post_reason() const { return post_reason_text(post_gate(), nullptr); }

// PUBLISH_DRIVE: the drive request as it stands, with the new profile.
void UiApp::profile_clicked() {
  (void)config_.drive->input(hmi::drive_session::Input::PROFILE_CLICK);
}

bool UiApp::entry_push() { return config_.drive->input(hmi::drive_session::Input::ENTRY_PUSH); }

// A hold completed. Both calls are non-blocking - one short I2C burst, and one
// xStreamBufferSend with a zero timeout - so neither holds the LVGL task for the duration of
// the effect it starts.
void UiApp::hold_confirm() {
  config_.cues->haptic_click();
  config_.cues->click();
}

// The drive table's UNLOCK_APPLIES: C3 adds POST passed (the hold does not fill before it; a
// push says why, row 53).
bool UiApp::unlock_applies() {
  return lv_subject_get_int(&locked_) != 0 && !drive_ui_.lock_waiting() &&
         lv_screen_active() == ui_LockedScreen && nav_menu_open == nullptr && mcb_ready() &&
         post_passed();
}

bool UiApp::drive_exit_applies() {
  return lv_screen_active() == ui_DriveScreen && nav_menu_open == nullptr;
}

// Only while locked (C1 §2.8, REQ-UI-16): with the drive table's entry rows needing no
// calibration running, a calibration then only ever runs locked, so no screen change the
// session makes can cut one short (G5).
bool UiApp::calibrate_applies() {
  return lv_subject_get_int(&locked_) != 0 && lv_screen_active() == ui_JoystickScreen &&
         nav_menu_open == nullptr;
}

// The stick's own button. Reads the level the button callback mirrors out, not the edge-latched
// select_key: a hold gesture needs to know the button is still down, and select_key is consumed
// by the first indev read after the press.
bool UiApp::joy_button_held() { return joy_button_pressed.load(); }

// Holding the Calibrate button, or the stick button anywhere on the joystick screen. The touch
// half is polled ("is a pointer down, and on Calibrate?" at the gesture's own cadence) rather
// than tracked from the button's press events, so there is no event to miss.
bool UiApp::calibrate_held() {
  return joy_button_held() || touch_held_on(ui_JoystickScreen, ui_CalibrateButton);
}

bool UiApp::menu_open() { return nav_menu_open != nullptr; }

int64_t UiApp::now_us() { return esp_timer_get_time(); }

// The ADC task cannot take the LVGL lock, so the profile reaches it through an atomic.
void UiApp::store_profile(int32_t profile) {
  drive_profile_published.store(static_cast<MIB::DriveProfile>(profile));
}

// The three gestures, polled by one shared timer; user data is the app.
void UiApp::hold_poll_cb(lv_timer_t *timer) {
  UiApp *self = static_cast<UiApp *>(lv_timer_get_user_data(timer));
  self->hold_engine_.poll_all(self->hold_gestures_);
}

// The 250 ms poll; user data is the UiPoll.
void UiApp::rtps_poll_cb(lv_timer_t *timer) {
  static_cast<UiPoll *>(lv_timer_get_user_data(timer))->poll();
}

void UiApp::keypad_read(bool *up, bool *down, bool *left, bool *right, bool *enter, bool *escape) {
  // While the self-test overlay is up it owns the stick: nothing reaches
  // the screens behind it, and the stick button closes it once the run
  // has finished. This read runs on the LVGL task, under its lock.
  if (config_.selftest->overlay_visible()) {
    *left = *right = *up = *down = *enter = *escape = false;
    if (select_key.exchange(false)) {
      config_.selftest->dismiss();
    }
    return;
  }
  uint32_t key = joy_key.load(); // held, so LVGL can repeat it
  const uint32_t flick = joy_flick.exchange(0);
  if (key == 0) {
    key = flick; // pressed for this one read, released on the next
  }
  *left = key == LV_KEY_LEFT;
  *right = key == LV_KEY_RIGHT;
  *up = key == LV_KEY_UP;
  *down = key == LV_KEY_DOWN;
  *enter = select_key.exchange(false); // one-shot
  *escape = false;
  if (*enter) {
    // The stick button's select, heard like a finger landing on the
    // screen (the touch callback clicks on the press).
    config_.cues->click();
  }
}

// --- Seat, bench gate, Settings, Skunk Works, Diagnostics --------------------------------------

// Seat values come from the MIB and nowhere else: a press publishes a request, and the number
// on screen moves only when the next MibStatus says the seat moved. The wire carries whole
// units, the screens raw integers, so each field is converted here.
void UiApp::seat_apply_state(const MIB::seatState &seat) {
  for (uint8_t i = 0; i < seat_axis_count_; i++) {
    const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[i];
    lv_subject_set_int(&seat_axis_value_[i],
                       rammp::seat_raw(spec, rammp::seat_field(seat, spec.id)));
  }
}

// One seat request: an absolute target, clamped to the axis' range, so a lost or repeated
// message cannot drift the seat.
void UiApp::seat_request(rammp::SeatAxis axis, int32_t target) {
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[rammp::index_of(axis)];
  (void)config_.link->publish_seat(
      axis, rammp::seat_units(spec, std::clamp(target, spec.min_value, spec.max_value)));
}

// One step up or down from where the MCB last said the axis is. What a value reads before it
// is known (VALUE_UNKNOWN) steps from the axis minimum.
void UiApp::seat_step(size_t row, int direction) {
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[row];
  const int32_t now = lv_subject_get_int(&seat_axis_value_[row]);
  const int32_t from = now == hmi::format::VALUE_UNKNOWN ? spec.min_value : now;
  seat_request(spec.id, from + direction * spec.step);
}

// The right PIN opens the actuators page.
void UiApp::bench_accepted() { settings_view_.open_page(ACTUATORS_PAGE); }

// The focus ring and pressed look of each PIN pad key.
void UiApp::style_pin_key(lv_obj_t *key) {
  NavView::focus_ring(key);
  NavView::mirror_states(key);
  clear_click_focusable_recursive(key);
}

// Applies and saves one of the Settings rows that has no observer of its own (brightness and
// theme do).
void UiApp::store_setting(int param, int32_t value) {
  settings_set(param, value);
  if (param == SETTINGS_PARAM_FLIP) {
    display_flip_.set_flipped(value != 0);
    return;
  }
  // The stick's and the sounds' atomics; MENU_SLIDE is read where it is used, nothing to
  // apply.
  (void)applied_settings.apply(param, value);
}

// ErrorBanner6. Raised only on a page that needs the MCB - the actuators - and then exactly as
// on the drive and seat screens: while the link is down or the MCB's state is not OK. User data
// is the app.
void UiApp::setting_warning_observer(lv_observer_t *observer, lv_subject_t *) {
  UiApp *self = static_cast<UiApp *>(lv_observer_get_user_data(observer));
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  if (lv_subject_get_int(self->settings_view_.page()) != ACTUATORS_PAGE || self->seat_ready()) {
    self->refusal_view_.show(panel, false);
    return;
  }
  self->refusal_view_.fill_drive_blocked(panel, rammp::kHmiLinkLostTitle, rammp::kHmiMcbFaultTitle);
  self->refusal_view_.show(panel, true);
}

// A tile that needs the MCB follows everything mcb_ready() reads.
void UiApp::bind_ready(lv_obj_t *tile, lv_observer_cb_t observer) {
  refusal_view_.bind_to_cause(tile, observer, this);
}

// Greys an MCB action (or menu row) while the MCB could not act on it. Bound to every subject
// mcb_ready() reads, so it follows the link and the state both. User data is the app.
void UiApp::action_ready_observer(lv_observer_t *observer, lv_subject_t *) {
  UiApp *self = static_cast<UiApp *>(lv_observer_get_user_data(observer));
  lv_obj_set_state(lv_observer_get_target_obj(observer), ActionsView::UNAVAILABLE,
                   !self->mcb_ready());
}

// An RTPS command: the request a "+" press on the actuators page makes. The seat moves only if
// the MCB agrees, and a refusal flashes on that page.
void UiApp::action_seat_up() { seat_step(rammp::index_of(rammp::SeatAxis::ELEVATION), +1); }

// DiagnosticsFreqLabel: the rate, or "No data" while stale. User data is the app.
void UiApp::diag_freq_observer(lv_observer_t *observer, lv_subject_t *) {
  static_cast<UiApp *>(lv_observer_get_user_data(observer))
      ->diag_view_.paint_freq(lv_observer_get_target_obj(observer));
}

// --- Navigation and the screens built on demand -----------------------------------------------

// Seat Functions refused where it was picked: the feedback, and the banner underneath.
void UiApp::refuse_seat() {
  config_.cues->refused();
  DrivePort<DriveUi>{&drive_ui_}.show_refused(REFUSED_SEAT, DRIVE_REFUSED_SHOW_MS);
}

bool UiApp::nav_drive_row() {
  return config_.drive->input(hmi::drive_session::Input::MENU_ROW_DRIVE);
}

void UiApp::nav_drive_key() {
  (void)config_.drive->input(hmi::drive_session::Input::MENU_KEY_DRIVE);
}

// The destinations the views open themselves (built on demand).
void UiApp::open_dest(NavDest dest) {
  switch (dest) {
  case NAV_SKUNK:
    actions_view_.open();
    break;
  case NAV_DIAG:
    diag_view_.open();
    break;
  case NAV_SET_DISPLAY:
    settings_view_.open_page(SETTINGS_PAGE_DISPLAY);
    break;
  case NAV_SET_STICK:
    settings_view_.open_page(SETTINGS_PAGE_STICK);
    break;
  default:
    break;
  }
}

// Hands the joystick to the screen being shown and puts the cursor at its top. Called on load;
// closing the menu restores the group without this reset.
void UiApp::enter_screen(const lv_obj_t *screen) {
  if (screen == ui_SeatScreen) {
    seat_view_.buttons_grid().row = 0;
    seat_view_.buttons_grid().col = 0;
    seat_view_.show_buttons_page();
  } else if (screen == ui_BenchGateScreen) {
    nav_view_.use_group(bench_pin_view_.group(), screen);
    bench_pin_view_.focus(0);
  } else if (screen == ui_SettingsScreen) {
    nav_view_.use_group(settings_view_.group(), screen);
    settings_view_.focus(0);
  } else if (screen == ui_SkunkWorksScreen) {
    nav_view_.use_group(actions_view_.group(), screen);
    actions_view_.focus(0);
  } else if (screen == ui_DiagnosticsScreen) {
    nav_view_.use_group(diag_view_.group(), screen);
    diag_view_.focus(0);
  } else if (screen == ui_InternetScreen) {
    config_.screens->internet_on_load(); // its own groups, one per page
  } else if (screen == ui_UpdateScreen) {
    config_.screens->update_on_load(); // its own groups, one per page
  } else if (screen == ui_AboutScreen) {
    config_.screens->about_on_load();
    lv_group_remove_all_objs(nav_view_.fallback_group()); // nothing to pick: only the key
    nav_view_.use_group(nav_view_.fallback_group(), screen);
  } else if (screen == ui_LogScreen && config_.screens->log_group() != nullptr) {
    nav_view_.use_group(config_.screens->log_group(), screen);
    config_.screens->log_on_load();
  } else {
    // The screens with at most a button or two of their own share one group, refilled for
    // whichever is up.
    lv_group_remove_all_objs(nav_view_.fallback_group());
    if (screen == ui_JoystickScreen) {
      lv_group_add_obj(nav_view_.fallback_group(), ui_CalibrateButton);
    }
    nav_view_.use_group(nav_view_.fallback_group(), screen);
  }
}

// The PIN is asked again on every visit rather than latching once per boot.
void UiApp::arrived(const lv_obj_t *screen) {
  if (screen == ui_BenchGateScreen) {
    bench_pin_view_.reset();
  }
}

// The lost-cursor backstop re-enters `screen` (NavView::start_input).
void UiApp::cursor_lost(const lv_obj_t *screen) {
  config_.nav_log->warn("the stick had nothing focused on {}; re-entering it",
                        NavView::screen_name(screen));
}

// Hand the joystick between groups as the screen changes. User data is the NavView.
void UiApp::screen_loaded_cb(lv_event_t *e) {
  static_cast<NavView *>(lv_event_get_user_data(e))->arrive(lv_event_get_target_obj(e));
}

// The chrome of a screen built on demand: the band's cells, the TopBar's RTPS label, clock and
// link, then the burger key and its overlay.
void UiApp::bind_on_demand_chrome(lv_obj_t *band, lv_obj_t *bar, lv_obj_t *key, lv_obj_t *overlay) {
  bind_chrome_views(band, bar);
  nav_view_.attach_chrome(key, overlay, band);
}

void UiApp::settings_bound() {
  refusal_view_.bind_to_cause(ui_ErrorBanner6, setting_warning_observer, this);
  lv_subject_add_observer_obj(settings_view_.page(), setting_warning_observer, ui_ErrorBanner6,
                              this);
}

void UiApp::diagnostics_bound() {
  for (lv_subject_t *subject : {diag_view_.rate(), diag_view_.stale(), &rtps_blink_subject}) {
    lv_subject_add_observer_obj(subject, diag_freq_observer, ui_DiagnosticsFreqLabel, this);
  }
}

// Overdraw
//
// What a full-screen redraw costs is mostly content, not the frame buffer: the
// render benchmark (kFpsInstrument) measured 86 ms for a busy screen against
// 19.5 ms for an empty one. The biggest share of that content was fills
// nobody could see, which is what strip_screen_overdraw takes out.
//
// One screen's redundant background fills (a screen built on demand).
void UiApp::strip_screen(const lv_obj_t *screen) {
  (void)strip_screen_overdraw(screen, OVERLAY_FLAG);
}

// Every screen's, logged: the boot pass, and again after each theme switch.
void UiApp::strip_overdraw_logged() {
  config_.overdraw_log->info("cleared {} redundant background fills",
                             strip_all_overdraw(OVERLAY_FLAG));
}

// The theme switch restored the redundant background fills, so take them out again. The switch
// itself is the Settings Theme row (or a CALL FUNCTION event reaching ui_events.cpp's
// theme_toggle, or the remote UI); this is where firmware first sees the result, so it is saved
// from here.
void UiApp::theme_switched(uint8_t theme) {
  strip_overdraw_logged();
  settings_set_theme(theme);
}

// The hmi_ui chrome views of one screen, in the order they have always been bound: the
// DriveBand's status cells, then the TopBar's RTPS label, then its clock and link.
void UiApp::bind_chrome_views(lv_obj_t *band, lv_obj_t *bar) {
  status_band_view_.bind(band);
  rtps_label_view_.bind(bar);
  topbar_view_.bind(bar);
}

} // namespace hmi::ui
