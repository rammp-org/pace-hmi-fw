/// @file ui_app.cpp
/// @brief UiApp's glue between the views (moved from main/main.cpp): readiness, the holds, the
///        seat command path, the Settings and Skunk Works observers, arrivals and the menu's
///        destinations, the screens built on demand and the overdraw pass. The build steps are
///        in ui_app_build.cpp.

#include "hmi_ui/ui_app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>

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
// The blocking check's own words are the POST's TopBar indicator's (hazard-c3-spec.md §2.8,
// §4.5); NOT_RUN reads C1's "Start-up check not run". UI task: the words are kept in a member.
const char *UiApp::post_reason() {
  post_reason_words_ = post_stage_.text_now();
  const char *words = post_reason_words_.text.data();
  return post_reason_text(post_gate(), words[0] != '\0' ? words : nullptr);
}

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

// The three gestures, polled by one shared timer; user data is the app. Then the Drive notice
// takes the stick's hold reason as it is now, so the screen follows it within one hold poll
// (REQ-UI-17), not one 250 ms tick.
void UiApp::hold_poll_cb(lv_timer_t *timer) {
  UiApp *self = static_cast<UiApp *>(lv_timer_get_user_data(timer));
  self->hold_engine_.poll_all(self->hold_gestures_);
  self->config_.drive->refresh_notice(self->hold_reason());
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
    key_last_ = 0;
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
  // A new press of a direction (REQ-UI-31): what tells an actuator row a held key's repeat
  // from the next press (SettingsView::seat_key_repeat).
  if (key != 0 && key != key_last_) {
    ++key_presses_;
  }
  key_last_ = key;
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
// units, the screens raw integers, so each field is converted here, and only through
// seat_reading (REQ-UI-30, H8): a NaN, an infinity or a value outside the axis range (one step
// of tolerance) reads unknown, "--". A MibStatus arriving is a CONNECTED link: rtps_comms stamps
// its arrival before this handler runs. RTPS task, lvgl_mutex held.
void UiApp::seat_apply_state(const MIB::seatState &seat) {
  for (uint8_t i = 0; i < seat_axis_count_; i++) {
    const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[i];
    seat_store(i, hmi::seat::seat_reading(spec, rammp::seat_field(seat, spec.id)));
  }
}

// Stale -> unknown (REQ-UI-30, hazard-seat-spec.md §2.3): every UI poll tick that finds the link
// not CONNECTED sets every axis unknown, so "--" shows within 2.25 s of the last MibStatus and a
// value is never stepped from after the MCB stopped saying it. It stays unknown until a
// MibStatus arrives (seat_apply_state). UI task.
void UiApp::seat_link_polled(LinkState state) {
  if (state == LinkState::CONNECTED) {
    return;
  }
  for (uint8_t i = 0; i < seat_axis_count_; i++) {
    seat_store(i, hmi::format::VALUE_UNKNOWN);
  }
}

// One axis' value into its subject (LVGL notifies only a change) and the bench's mirror
// (REQ-RUI-09).
void UiApp::seat_store(size_t row, int32_t raw) {
  lv_subject_set_int(&seat_axis_value_[row], raw);
  seat_value_pub_[row].store(raw, std::memory_order_release);
}

// A press' outcome for the bench (REQ-RUI-09): sent, refused before a publish, or not sent.
void UiApp::seat_outcome(hmi::seat::SeatReason reason) {
  using hmi::seat::SeatReason;
  if (reason == SeatReason::ALLOW) {
    seat_sent_.fetch_add(1, std::memory_order_relaxed);
  } else if (reason == SeatReason::NOT_SENT) {
    seat_publish_failures_.fetch_add(1, std::memory_order_relaxed);
  } else {
    seat_refused_.fetch_add(1, std::memory_order_relaxed);
  }
  seat_reason_.store(static_cast<uint8_t>(reason), std::memory_order_release);
}

UiApp::SeatTelemetry UiApp::seat_telemetry() const {
  SeatTelemetry t;
  for (size_t i = 0; i < t.values.size(); ++i) {
    t.values[i] = seat_value_pub_[i].load(std::memory_order_acquire);
  }
  constexpr auto relaxed = std::memory_order_relaxed;
  t.sent = seat_sent_.load(relaxed);
  t.refused = seat_refused_.load(relaxed);
  t.publish_failures = seat_publish_failures_.load(relaxed);
  const uint8_t reason = seat_reason_.load(std::memory_order_acquire);
  t.pressed = reason != kSeatNoPress;
  t.reason = t.pressed ? static_cast<hmi::seat::SeatReason>(reason) : hmi::seat::SeatReason::ALLOW;
  return t;
}

// The seat gate (REQ-UI-28; hazard-seat-spec.md §2.2-§2.5). Every SeatCommand the HMI sends
// passes here: the Seat screen's "-"/"+" and presets, Bench -> Actuators, Skunk Works "Seat up".
// It is the only caller of publish_seat (SEAT-011). The inputs are read now: the link live
// (LinkPort::state, not the 250 ms subject), the MCB state, the POST gate and stick health
// (acquire), the axis value. A refused press publishes nothing and is felt and named (§2.8). An
// allowed one is an absolute target within the axis range, published once: the result is
// checked and counted, and the HMI never re-sends it (REQ-UI-29, Q7: a lost press means no
// motion). C3's POST condition (REQ-UI-20) is the gate's condition 3. UI task.
void UiApp::seat_request(rammp::SeatAxis axis, hmi::seat::SeatAsk ask) {
  using hmi::seat::SeatReason;
  const size_t row = rammp::index_of(axis);
  if (row >= seat_axis_count_) {
    return; // not one of the table's axes
  }
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[row];
  const hmi::seat::SeatDecision decision = hmi::seat::decide(spec, seat_inputs(row), ask);
  if (decision.reason != SeatReason::ALLOW) {
    seat_refuse(decision.reason, row);
    return;
  }
  if (!config_.link->publish_seat(axis, rammp::seat_units(spec, decision.target))) {
    seat_refuse(SeatReason::NOT_SENT, row);
    return;
  }
  seat_outcome(SeatReason::ALLOW);
}

// A preset: its absolute target, read off its label (SeatView), through the gate.
void UiApp::seat_preset(rammp::SeatAxis axis, int32_t target) {
  seat_request(axis, {.press = hmi::seat::SeatPress::PRESET, .amount = target});
}

// One step up or down from where the MCB last said the axis is, through the gate: an unknown
// value is refused (no step from the axis minimum), and so is a step that would not move the
// axis in the press' direction (AT_LIMIT, Q9).
void UiApp::seat_step(size_t row, int direction) {
  if (row >= seat_axis_count_) {
    return;
  }
  seat_request(rammp::kSeatAxes[row].id,
               {.press = hmi::seat::SeatPress::STEP, .amount = direction});
}

// The gate's inputs at the press (§2.2's "Read from").
hmi::seat::SeatInputs UiApp::seat_inputs(size_t row) {
  return {.link_connected = config_.link->state() == LinkState::CONNECTED,
          .mcb_state = static_cast<uint8_t>(lv_subject_get_int(&mib_state_)),
          .post = post_gate(),
          .health = permit_hooks_.stick_health.read(),
          .value = lv_subject_get_int(&seat_axis_value_[row])};
}

// A refused press (hazard-seat-spec.md §2.8): counted, felt, and named on the reason's banner.
// AT_LIMIT is feedback only, as the Actuators page always did at a limit.
void UiApp::seat_refuse(hmi::seat::SeatReason reason, size_t row) {
  seat_outcome(reason);
  config_.cues->refused();
  seat_reason_banner(reason, row);
}

// The banner of each refusal reason (hazard-seat-spec.md §2.8).
void UiApp::seat_reason_banner(hmi::seat::SeatReason reason, size_t row) {
  using hmi::seat::SeatReason;
  switch (reason) {
  case SeatReason::LINK_DOWN:
  case SeatReason::MCB_NOT_IDLE:
    seat_banner(REFUSED_SEAT);
    return;
  case SeatReason::POST_NOT_PASSED:
    seat_banner(REFUSED_POST);
    return;
  case SeatReason::STICK_FAULT:
    seat_banner(REFUSED_STICK);
    return;
  case SeatReason::POSITION_UNKNOWN:
    seat_unknown_row_ = static_cast<uint8_t>(row);
    seat_banner(REFUSED_SEAT_UNKNOWN);
    return;
  case SeatReason::NOT_SENT:
    seat_log_not_sent(row);
    seat_banner(REFUSED_SEAT_NOT_SENT);
    return;
  case SeatReason::AT_LIMIT:
  case SeatReason::ALLOW:
    return;
  }
}

// A seat refusal's banner, marked as the seat's (refused_mirror_observer reads the mark): the
// shared REFUSED_POST and REFUSED_STICK then read "Seat not ready". The same value raised again
// is re-notified, so the mark and the banner follow this press.
void UiApp::seat_banner(int32_t which) {
  seat_raising_ = true;
  const bool same = lv_subject_get_int(&refused_) == which;
  DrivePort<DriveUi>{&drive_ui_}.show_refused(which, DRIVE_REFUSED_SHOW_MS);
  if (same) {
    lv_subject_notify(&refused_);
  }
  seat_raising_ = false;
}

// REQ-UI-29: a failed publish is logged at most once a second, with the count since start.
void UiApp::seat_log_not_sent(size_t row) {
  const int64_t now = now_us();
  if (seat_log_ == nullptr ||
      (seat_fail_logged_us_ != 0 && now - seat_fail_logged_us_ < kSeatPublishFailLogMs * 1000)) {
    return;
  }
  seat_fail_logged_us_ = now;
  seat_log_->warn("SeatCommand {} not published ({} failed since start)",
                  rammp::to_string(rammp::kSeatAxes[row].id),
                  seat_publish_failures_.load(std::memory_order_relaxed));
}

// REFUSED_SEAT_UNKNOWN clears early once its axis is known again (REQ-UI-33).
bool UiApp::seat_unknown_known() {
  return hmi::seat::known(lv_subject_get_int(&seat_axis_value_[seat_unknown_row_]));
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
// on the drive and seat screens: while the link is down or the MCB's state is not OK; or, for its
// window, when the seat gate refused a press there (hazard-seat-spec.md §2.8; C3's POST
// refusal is one of them). User data is the app.
void UiApp::setting_warning_observer(lv_observer_t *observer, lv_subject_t *) {
  UiApp *self = static_cast<UiApp *>(lv_observer_get_user_data(observer));
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  if (lv_subject_get_int(self->settings_view_.page()) != ACTUATORS_PAGE) {
    self->refusal_view_.show(panel, false);
    return;
  }
  const int32_t refused = lv_subject_get_int(self->drive_ui_.entry_refused_subject());
  if (self->refusal_view_.fill_seat(panel, refused)) { // the seat gate's (hazard-seat-spec §2.8)
    self->refusal_view_.show(panel, true);
    return;
  }
  if (refused == REFUSED_POST) {
    self->refusal_view_.fill_post_refused(panel, rammp::kHmiRefusedPostSeatTitle);
    self->refusal_view_.show(panel, true);
    return;
  }
  if (self->seat_ready()) {
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

// The refusal banner's value, mirrored for other tasks (refused_any_task), and whether a seat
// press raised it (seat_banner). The first observer of `refused`, so the banners read the mark
// as of this change. User data is the app.
void UiApp::refused_mirror_observer(lv_observer_t *observer, lv_subject_t *subject) {
  auto *self = static_cast<UiApp *>(lv_observer_get_user_data(observer));
  self->seat_origin_ = self->seat_raising_;
  self->refused_pub_.store(lv_subject_get_int(subject), std::memory_order_release);
}

// DiagnosticsFreqLabel: the rate, or "No data" while stale. User data is the app.
void UiApp::diag_freq_observer(lv_observer_t *observer, lv_subject_t *) {
  static_cast<UiApp *>(lv_observer_get_user_data(observer))
      ->diag_view_.paint_freq(lv_observer_get_target_obj(observer));
}

// --- Navigation and the screens built on demand -----------------------------------------------

// The menu's Seat Functions row (REQ-UI-32, Q10): refused where it was picked when the seat
// gate's conditions 1-4 fail (link, MCB state, POST, stick; not the value, not the limit), with
// the refusal feedback and that reason's banner underneath; the menu stays. Not a seat press:
// the bench's seat counters do not count it.
bool UiApp::seat_row_refused() {
  const hmi::seat::SeatReason reason = hmi::seat::ready(seat_inputs(0));
  if (reason == hmi::seat::SeatReason::ALLOW) {
    return false;
  }
  config_.cues->refused();
  seat_reason_banner(reason, 0);
  return true;
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

// Every screen load is logged (the bench times the MCB-initiated entry from it: B5''-18b). The
// PIN is asked again on every visit rather than latching once per boot.
void UiApp::arrived(const lv_obj_t *screen) {
  config_.nav_log->info("screen -> {}", NavView::screen_name(screen));
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
  lv_subject_add_observer_obj(drive_ui_.entry_refused_subject(), setting_warning_observer,
                              ui_ErrorBanner6, this);
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

bool UiApp::stick_fault_indicated() const {
  return post_stage_.shown().kind == hmi::post::IndicatorKind::NONE &&
         permit_hooks_.stick_health.read() == hmi::stick::StickHealth::FAULT;
}

namespace {
// The Joystick screen's words for a latched stick fault (REQ-UI-26): the axis and what it does.
const char *stick_why(hmi::stick::SampleFault kind) {
  using hmi::stick::SampleFault;
  switch (kind) {
  case SampleFault::MISSING:
    return rammp::kHmiStickWhyMissing;
  case SampleFault::NAN_VALUE:
    return rammp::kHmiStickWhyNan;
  case SampleFault::NEGATIVE:
    return rammp::kHmiStickWhyNegative;
  case SampleFault::HIGH:
    return rammp::kHmiStickWhyHigh;
  case SampleFault::STALE:
    return rammp::kHmiStickWhyStale;
  case SampleFault::TWIST_PARTIAL:
    return rammp::kHmiStickWhyTwistPartial;
  case SampleFault::NO_SAMPLES:
    return rammp::kHmiStickWhyNoSamples;
  case SampleFault::INTERNAL:
  case SampleFault::NONE:
    break;
  }
  return rammp::kHmiStickWhyInternal;
}
const char *stick_axis_words(hmi::stick::StickAxis axis) {
  using hmi::stick::StickAxis;
  switch (axis) {
  case StickAxis::HORIZONTAL:
    return rammp::kHmiStickAxisHorizontal;
  case StickAxis::VERTICAL:
    return rammp::kHmiStickAxisVertical;
  case StickAxis::TWIST:
    return rammp::kHmiStickAxisTwist;
  case StickAxis::NONE:
    break;
  }
  return "";
}
} // namespace

bool UiApp::stick_fault() const {
  return permit_hooks_.stick_health.read() == hmi::stick::StickHealth::FAULT;
}

// The latched fault, as the monitor's telemetry names it, in the Joystick screen's words.
const char *UiApp::stick_fault_words() {
  constexpr auto relaxed = std::memory_order_relaxed;
  const auto kind = static_cast<hmi::stick::SampleFault>(stick_telemetry_.fault_kind.load(relaxed));
  const auto axis = static_cast<hmi::stick::StickAxis>(stick_telemetry_.fault_axis.load(relaxed));
  std::snprintf(stick_fault_words_.text.data(), stick_fault_words_.text.size(), "%s%s%s",
                rammp::kHmiStickFaultLead, stick_axis_words(axis), stick_why(kind));
  return stick_fault_words_.text.data();
}

// C2's stick monitor as the UI task sees it (REQ-UI-26, REQ-UI-27): each change of its state is
// logged once, with the reasons and the counters (the ADC task logs nothing); in FAULT the
// Joystick screen names the reason in words.
void UiApp::stick_monitor_changed() {
  using hmi::stick::MonitorState;
  using hmi::stick::SampleFault;
  using hmi::stick::StickAxis;
  constexpr auto relaxed = std::memory_order_relaxed;
  const hmi::stick::MonitorTelemetry &t = stick_telemetry_;
  const uint8_t state = t.state.load(relaxed);
  if (stick_state_seen_ && state == stick_state_) {
    return;
  }
  stick_state_seen_ = true;
  stick_state_ = state;
  const auto last_kind = static_cast<SampleFault>(t.last_kind.load(relaxed));
  const auto last_axis = static_cast<StickAxis>(t.last_axis.load(relaxed));
  const auto fault_kind = static_cast<SampleFault>(t.fault_kind.load(relaxed));
  const auto fault_axis = static_cast<StickAxis>(t.fault_axis.load(relaxed));
  const uint32_t bad =
      std::accumulate(t.implausible.begin(), t.implausible.end(), uint32_t{0},
                      [](uint32_t sum, const auto &n) { return sum + n.load(relaxed); });
  if (stick_log_ != nullptr) {
    stick_log_->info("stick monitor {}: last {} {}, fault {} {}; suspect onsets {}, faults {}, "
                     "implausible samples {}, X/Y age max {} ms",
                     hmi::stick::to_string(static_cast<MonitorState>(state)),
                     hmi::stick::to_string(last_kind), hmi::stick::to_string(last_axis),
                     hmi::stick::to_string(fault_kind), hmi::stick::to_string(fault_axis),
                     t.suspect_onsets.load(relaxed), t.faults.load(relaxed), bad,
                     t.xy_age_max_ms.load(relaxed));
  }
  if (config_.screens->calibration_note == nullptr) {
    return;
  }
  if (static_cast<MonitorState>(state) != MonitorState::FAULT) {
    config_.screens->calibration_note(nullptr);
    return;
  }
  std::snprintf(stick_words_.text.data(), stick_words_.text.size(), "%s%s%s%s",
                rammp::kHmiStickFaultLead, stick_axis_words(fault_axis), stick_why(fault_kind),
                rammp::kHmiStickFaultTail);
  config_.screens->calibration_note(stick_words_.text.data());
}

// The POST's persistent indicator on every TopBar (hazard-c3-spec.md §2.8, REQ-UI-19): the
// indicator's state after this tick's runner and drive tick, in its words and colour. Below
// every POST state, C2's latched stick fault (REQ-UI-26).
void UiApp::post_indicator() {
  using hmi::post::IndicatorKind;
  stick_monitor_changed();
  if (stick_fault_indicated()) {
    topbar_view_.set_post(TopBarView::PostColour::RED, rammp::kHmiNoticeStickFault);
    return;
  }
  const PostStage::Shown shown = post_stage_.shown();
  TopBarView::PostColour colour = TopBarView::PostColour::RED;
  switch (shown.kind) {
  case IndicatorKind::NONE:
    colour = TopBarView::PostColour::NONE;
    break;
  case IndicatorKind::CHECKING:
    colour = TopBarView::PostColour::GREY;
    break;
  case IndicatorKind::WAITING:
    colour = TopBarView::PostColour::AMBER;
    break;
  case IndicatorKind::FAILED:
  case IndicatorKind::NOT_RUN:
    break;
  }
  topbar_view_.set_post(colour, post_stage_.text_now().text.data());
}

// The hmi_ui chrome views of one screen, in the order they have always been bound: the
// DriveBand's status cells, then the TopBar's RTPS label, then its clock and link.
void UiApp::bind_chrome_views(lv_obj_t *band, lv_obj_t *bar) {
  status_band_view_.bind(band);
  rtps_label_view_.bind(bar);
  topbar_view_.bind(bar);
}

} // namespace hmi::ui
