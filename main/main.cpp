/**
 * @file m5stack_tab5_example.cpp
 * @brief M5Stack Tab5 BSP Example
 *
 * This example demonstrates the comprehensive functionality of the M5Stack Tab5
 * development board including display, touch, audio, camera, IMU, power management,
 * and communication interfaces.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <numeric>
#include <optional>
#include <stdlib.h>
#include <sys/time.h>
#include <utility>
#include <vector>

#include "m5stack-tab5.hpp"

#include "drv2605.hpp"
#include "feedback/da7280_bench.hpp"
#include "feedback/feedback.hpp"
#include "housekeeping/housekeeping.hpp"
#include "housekeeping/system_clock.hpp"

#include "simple_lowpass_filter.hpp"

#include "ui.h"
// The chrome (DriveBand, ErrorBanner, TopBar, MenuKey, MenuOverlay) is made of
// SquareLine *components*, so their children are reached by index
// through ui_comp_get_child() rather than by a ui_* global.
#include "components/ui_comp_driveband.h"
#include "components/ui_comp_errorbanner.h"
#include "components/ui_comp_topbar.h"

#include "button.hpp"
#include "joystick.hpp"
#include "keypad_input.hpp"

#include "about_ui.hpp"
#include "actions_spec.h"
#include "boot_logo.h"
#include "drive_adapter.hpp"
#include "drive_session.hpp"
#include "fw_info.hpp"
#include "github_ota.hpp"
#include "internet_ui.hpp"
#include "joystick_cal.hpp"
#include "log_capture.hpp"
#include "log_view.hpp"
#include "remote_ui.hpp"
#include "rtps_comms.hpp"
#include "selftest.hpp"
#include "selftest_platform.hpp"
#include "settings.hpp"
#include "settings_applied.hpp"
#include "storage.hpp"
#include "update_ui.hpp"

#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_system.h"
#include "esp_timer.h"

using namespace std::chrono_literals;

static std::recursive_mutex lvgl_mutex;

#include "frag_fps.inc" // split_main.py
// --
// What is left here, and why it stays in main for now: mib_state, rtps_link and locked, which
// several views read (SharedSubjects) and the drive port reads by name, pinned by its frozen
// goldens (tests/host/drive_golden), with the tables over them. The rest of the shared state
// is components/hmi_ui's app_state (S7 moves each piece on to its owner).
#include "hmi_ui/app_state.hpp"
// The MIB's state, as reported over RTPS. The joystick is a slave here: this holds
// whatever the MIB last said, and every DriveBand on every screen follows it. One
// subject, because MibSystemState answers both of a panel's labels - where the message
// it replaced needed a drive status and a fault. Values are the MIB::MibSystemState
// enum from messages/mib_message.hpp.
static lv_subject_t mib_state_subject;

// Link health behind those two, polled from rtps_comms (RtpsLinkState). Drives
// the TopBar's RTPS indicator, and greys the status labels when it is not
// CONNECTED - a value we can no longer vouch for must not keep showing.
static lv_subject_t rtps_link_subject;

// Lock state. Declared up here with the other subjects because the unlock
// gesture and the nav layer both read it; the rest of the lock/unlock machinery
// lives in its own section further down, after the haptic and audio helpers it
// needs.
//
// 1 = locked, 0 = unlocked. The single source of truth for both padlock images
// and both labels, so a future "lock on request" is one set_locked(true) call
// and every bound widget follows.
static lv_subject_t locked_subject;
// An indev can own exactly one group, so the joystick's group follows the
// active screen. The swap happens on LV_EVENT_SCREEN_LOADED, so it covers
// every route between the screens.
// "This object covers what is behind it" (components/hmi_ui overdraw.hpp).
#include "hmi_ui/overdraw.hpp"
static constexpr lv_obj_flag_t kOverlayFlag = hmi::ui::OVERLAY_FLAG;
using hmi::ui::keep_overlay_fill;

// Defined with the burger menu (see "The burger menu, and moving around with
// the joystick"); declared here for the screens and gestures above it.
static void nav_use_group(lv_group_t *g, const lv_obj_t *screen);
static void nav_arrive(const lv_obj_t *screen);
static void nav_mirror_states(lv_obj_t *obj);
static void nav_to_key();
static void nav_home();
static void nav_focus_ring(lv_obj_t *obj);
static void nav_update_stick_gate();

// The subjects above that the hmi_ui views read (components/hmi_ui/shared_subjects.hpp).
#include "hmi_ui/link_state.hpp"
#include "hmi_ui/nav_port.hpp"
#include "hmi_ui/nav_view.hpp"
#include "hmi_ui/shared_subjects.hpp"
static constexpr hmi::ui::SharedSubjects ui_shared_subjects{
    .locked = &locked_subject,
    .mib_state = &mib_state_subject,
    .rtps_link = &rtps_link_subject,
    .rtps_blink = &rtps_blink_subject,
};
// rtps_link_subject carries RtpsLinkState; the views read it as hmi::ui::LinkState.
static constexpr bool same_link_state(RtpsLinkState a, hmi::ui::LinkState b) {
  return static_cast<int32_t>(a) == static_cast<int32_t>(b);
}
static_assert(same_link_state(RtpsLinkState::NET_FAILED, hmi::ui::LinkState::NET_FAILED));
static_assert(same_link_state(RtpsLinkState::LINK_DOWN, hmi::ui::LinkState::LINK_DOWN));
static_assert(same_link_state(RtpsLinkState::NO_IP, hmi::ui::LinkState::NO_IP));
static_assert(same_link_state(RtpsLinkState::NO_PEER, hmi::ui::LinkState::NO_PEER));
static_assert(same_link_state(RtpsLinkState::CONNECTED, hmi::ui::LinkState::CONNECTED));
// Navigation for the hmi_ui views (components/hmi_ui/nav_port.hpp): NavView's API. The look
// is NavView's own (static); the two calls that need the one instance go through it.
static constexpr hmi::ui::NavPort ui_nav_port{
    .to_key = nav_to_key,
    .use_group = nav_use_group,
    .focus_ring = hmi::ui::NavView::focus_ring,
    .mirror_states = hmi::ui::NavView::mirror_states,
};
// The user's haptic and sound cues (hmi::feedback::Feedback). app_main builds
// them (app-main-shrink V6) and points this at them before any task starts; the
// rest of the unit cues through haptic_play below, and through play_click,
// play_refusal and refusal_feedback (after app_main, with load_audio).
static hmi::feedback::Feedback *feedback = nullptr;
using hmi::feedback::kHapticBuzzSlots;
// The self test reports whether the DA7280 answered the boot scan.
using hmi::feedback::kDa7280Address;

// HAPTIC TEST settings row -> kHapticBuzzDuration of vibration; the unlock and
// hold clicks; a refusal's double click. Returns false (having logged) if there
// is no motor or the I2C burst fails, so callers can leave their own UI alone.
static bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots) {
  return feedback != nullptr && feedback->haptics().play(w, slots);
}

// How the click sound reaches the speaker and the LVGL task, for app_main's Feedback.
static hmi::feedback::ClickSound::Config click_sound_config() {
  return {
      .sounds_on = applied_settings.sounds_on(),
      .play =
          [](std::span<const uint8_t> samples) { espp::M5StackTab5::get().play_audio(samples); },
      .now_ms = [] { return lv_tick_get(); },
      // The second click of a refusal: a one-shot LVGL timer, so it runs on the
      // LVGL task like the first.
      .click_later =
          [](uint32_t delay_ms, hmi::feedback::ClickSound &sound) {
            lv_timer_t *second = lv_timer_create(
                [](lv_timer_t *timer) {
                  static_cast<hmi::feedback::ClickSound *>(lv_timer_get_user_data(timer))->click();
                },
                delay_ms, &sound);
            lv_timer_set_repeat_count(second, 1);
          },
  };
}
#include "frag_status_band.inc" // split_main.py
// --
// AXIS WIRING: the gimbal pots are cross-wired relative to the channel names.
// ADC1_CH1 (GPIO17) is the HORIZONTAL axis and reads higher to the right, so
// it feeds the joystick's X with no inversion. ADC1_CH0 (GPIO16) is the
// VERTICAL axis and reads *lower* moving up, so it feeds Y with invert_output
// — after which +Y is up, as the rest of the code assumes. Twist reads higher
// clockwise. Fixed in hmi::stick (components/stick: horizontal_config,
// vertical_config, twist_config) so every consumer (UI bars, the keypad, RTPS)
// sees correct axes without compensating itself; joystick_cal.cpp's step
// prompts rely on the same directions.
//
// The stick math itself (mapping, mount, keys, gate, speed scale) is
// hmi::stick::StickPipeline. The ADC task runs one cycle of it per sample and
// AdcStickIo below is everything that cycle reads and writes here, in the order
// the code ran inline before it was extracted. stick_inject.hpp includes it, and
// adds StickSlot: the pipeline, or with CONFIG_HMI_BENCH_STICK_INJECT the bench
// stick injection in front of it.
#include "control/stick_island.hpp"
#include "stick_inject.hpp"

static_assert(hmi::stick::SENSITIVITY_MIN == SETTINGS_STICK_SENSITIVITY_MIN &&
                  hmi::stick::SENSITIVITY_MAX == SETTINGS_STICK_SENSITIVITY_MAX,
              "hmi::stick clamps the stick sensitivity to the Settings range");
static_assert(hmi::stick::DRIVE_SPEED_MIN == SETTINGS_DRIVE_SPEED_MIN &&
                  hmi::stick::DRIVE_SPEED_MAX == SETTINGS_DRIVE_SPEED_MAX,
              "hmi::stick clamps the drive speed to the Settings range");

static hmi::stick::CalibrationMv stick_cal_mv(const JoystickCal &cal) {
  const auto axis = [](const JoystickAxisCal &a) {
    return hmi::stick::AxisCalMv{.min_mv = a.min_mv, .center_mv = a.center_mv, .max_mv = a.max_mv};
  };
  return {.horizontal = axis(cal[JOY_HORIZONTAL]),
          .vertical = axis(cal[JOY_VERTICAL]),
          .twist = axis(cal[JOY_TWIST])};
}

// How far each axis travels is measured per unit (joystick_cal.cpp, the CALIBRATE button on
// the JoystickTest screen); the dead zones are the stick's (hmi::stick::pipeline_config).
static hmi::stick::StickPipeline::Config stick_pipeline_config(const JoystickCal &cal) {
  return hmi::stick::pipeline_config(
      stick_cal_mv(cal),
      {.up = LV_KEY_UP, .down = LV_KEY_DOWN, .right = LV_KEY_RIGHT, .left = LV_KEY_LEFT});
}

// The ADC task's side of StickPipeline::cycle. Runs on the ADC task only; every
// member is what the stick block of adc_task_fn did inline before.
struct AdcStickIo {
  espp::SimpleLowpassFilter &twist_lowpass;

  // A calibration run just finished: the pipeline switches to it between two
  // samples, on the task that owns the stick.
  std::optional<hmi::stick::CalibrationMv> take_new_calibration() {
    if (auto cal = joystick_cal_take_new()) {
      return stick_cal_mv(*cal);
    }
    return std::nullopt;
  }
  float smooth_twist_mv(float twist_mv) { return twist_lowpass(twist_mv); }
  void note_raw_mv(float horizontal_mv, float vertical_mv, float twist_mv) {
    joystick_cal_note_raw(horizontal_mv, vertical_mv, twist_mv);
  }
  // While a calibration run owns the stick nothing downstream may act on it:
  // the user is being told to push it to every end in turn.
  bool calibrating() { return joystick_cal_running(); }
  bool swap() { return applied_settings.stick_swap(); }
  bool invert_x() { return applied_settings.stick_invert_x(); }
  bool invert_y() { return applied_settings.stick_invert_y(); }
  int sensitivity() { return applied_settings.stick_sensitivity(); }
  uint32_t joy_key() { return ::joy_key.load(); }
  uint32_t remote_key() { return ::remote_key.load(); }
  void set_joy_key(uint32_t key) { ::joy_key.store(key); }
  void set_joy_flick(uint32_t key) { joy_flick.store(key); }
  void show(const hmi::stick::Position &mounted) {
    // lv_subject_set_int runs the bar's observer callback synchronously,
    // which touches the widget, so it needs the LVGL lock. Tried, not
    // waited for: the UI holds it for a whole frame (a full redraw is
    // ~100 ms, more with Flip screen), and the stick's path to the MCB
    // must not queue behind a render. A busy UI just gets the bars
    // one cycle later.
    std::unique_lock<std::recursive_mutex> lock(lvgl_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
      lv_subject_set_int(joystick_view.x(), static_cast<int32_t>(mounted.x * 100.0f));
      lv_subject_set_int(joystick_view.y(), static_cast<int32_t>(mounted.y * 100.0f));
      lv_subject_set_int(joystick_view.twist(), static_cast<int32_t>(mounted.twist * 100.0f));
    }
  }
  int drive_speed() { return applied_settings.drive_speed(); }
  // The gate. The MCB gets a centred stick whenever the stick is doing something
  // else: while a calibration run sweeps it (the pipeline does not read this
  // then), and whenever it is walking the UI rather than driving -- any screen
  // but Drive, or Drive with the menu open. Drive stays ACTIVE across the menu
  // and the other screens, as the spec draws it, so this is what keeps a push
  // meant for the next row from moving the chair. The bars keep moving.
  bool stick_drives() { return ::stick_drives.load(); }
  bool button_pressed() { return joy_button_pressed.load(); }
  // The MCB gets the same calibrated -1..+1 values the bars show (+Y forward,
  // deadzones applied), scaled by Settings "Speed sensitivity" and the gate, so
  // it needs no calibration of its own. Quiet no-op until RTPS is up and a
  // subscriber is discovered.
  bool publish(const hmi::stick::Command &command, bool button) {
    return rtps_comms_publish_adc(command.x, command.y, command.twist,
                                  button ? rammp::Buttons::JOYSTICK : rammp::Buttons::NONE);
  }
  // After every cycle, valid or not: the self test measures the loop's cadence and
  // how often a read fails, as well as the values. A no-op unless a run is
  // capturing.
  void note_cycle(bool valid, float horizontal_mv, float vertical_mv, float twist_mv,
                  bool published) {
    selftest_note_adc(valid, horizontal_mv, vertical_mv, twist_mv, published,
                      joy_button_pressed.load());
  }
};
#include "frag_rtps_label.inc" // split_main.py
// --
#include "frag_drive_band.inc" // split_main.py
// --
#include "frag_rtps_poll.inc" // split_main.py
// --
#include "hmi_ui/brightness_view.hpp"

// The one instance. Its subject is the Settings row's (SettingSubjects): the row steps it too.
static constinit hmi::ui::BrightnessView brightness_view{{
    .subject = setting_subjects.value(SETTINGS_PARAM_BRIGHTNESS),
    .min_percent = kBrightnessMinPercent,
    .max_percent = kBrightnessMaxPercent,
    .backlight = [](float percent) { espp::M5StackTab5::get().brightness(percent); },
    .save = settings_set_brightness,
}};

// Any task. Clamped, so nothing can turn the screen fully off.
static void brightness_set(int percent) {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  brightness_view.set(percent);
}

// The Tab5's side button: the next of 25/50/75/100 % above the current level.
static void brightness_step() {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  brightness_view.step();
}
#include "hmi_format/topbar.hpp"
#include "hmi_ui/topbar_view.hpp"

/////////////////////////////////////////////////////////////////////////////
// TopBar clock
//
// The MIB sends Unix time plus its UTC offset in every MibStatus. It sets the
// system clock and the RTC, so the time keeps running through a lost link, and
// across a reboot without the MIB. Clock1 on every TopBar shows the system
// clock. No TZ is set, so the system clock simply holds the local wall time the
// MIB reported and nothing converts again.
/////////////////////////////////////////////////////////////////////////////

// The system clock's validity: set by hmi::housekeeping::SystemClock (app_main's
// system_clock), read by the TopBar. The time sync itself is the housekeeping
// component's.
static std::atomic<bool> clock_valid{false};

// An RTC that lost power reads 2000; anything before 2025 is not a real time
// (components/hmi_format, REQ-FMT-06).
using hmi::format::clock_plausible;

// The TopBar's link label: what RTPS runs over. The design's "BT · WI-FI" is only
// a placeholder; it is set from the setting before rtps_comms_start, then from the
// link it brought up (WiFi with no network known runs Ethernet).
// NetLink -> hmi_format's Link (REQ-FMT-07): anything but WiFi reads Ethernet.
static const char *link_text(NetLink link) {
  return hmi::format::link_text(link == NetLink::WIFI ? hmi::format::Link::WIFI
                                                      : hmi::format::Link::ETHERNET);
}

// The one instance, for every TopBar; bound from app_main and the screens built on demand.
static constinit hmi::ui::TopBarView topbar_view{{.clock_valid = &clock_valid}};
static void bind_topbar_labels(lv_obj_t *bar) { topbar_view.bind(bar); }

// The hmi_ui chrome views of one screen, in the order app_main has always bound them: the
// DriveBand's status cells, then the TopBar's RTPS label, then its clock and link. The screens
// built on demand bind the same three in their *_ensure().
static void bind_chrome_views(lv_obj_t *band, lv_obj_t *bar) {
  bind_status_panel(band);
  bind_rtps_label(bar);
  bind_topbar_labels(bar);
}

// One edge of the stick button, from the GPIO or from the remote UI channel --
// one path, so what a script presses is the real handling. Touches subjects,
// never widgets directly. Any task.
//
// "Select" -- the keypad's ENTER, which clicks whatever the joystick has
// focused -- fires on RELEASE, and only for a press shorter than
// hmi::stick::SELECT_MAX_US. The same button is held to stop driving and to start a
// calibration, and a select on the press edge would have clicked the focused
// control (on the Drive screen: opened the menu) at the start of every hold.
// SELECT_MAX_US is a hold's grace period, so a press short enough to select
// never starts a hold filling, and one long enough to fill never selects.
#include "frag_stick_button.inc" // split_main.py
// --
#include "frag_hold.inc" // split_main.py
// --
#include "frag_lock.inc" // split_main.py
// --
#include "frag_refusal.inc" // split_main.py
// --
#include "frag_drive.inc" // split_main.py
// --
#include "frag_hold_poll.inc" // split_main.py
// --
#include "frag_seat.inc" // split_main.py
// --
#include "frag_bench_pin.inc" // split_main.py
// --
#include "hmi_format/stepper.hpp"
#include "hmi_ui/settings_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// SettingsScreen: pages of -/+ rows
//
// One screen for every setting that is a few numbers. A page is a title, a
// line of instructions and some rows; each row is the export's
// SettingRow component (Parameter1 is only the template, deleted at boot): short
// label, label, value, and - / + buttons that grey out at the ends of the
// range. Up/down move between rows, left/right step the focused one (holding
// the stick repeats), and touch works on the buttons.
//
// Two kinds of page, which differ only in what a step does:
//   settings   settings_spec.hpp. The step sets the row's subject, clamped to
//              the range; whatever owns that subject applies and saves it.
//   actuators  RAMMP_SEAT_AXIS_TABLE, reached through the PIN (DEBUG
//              ACTUATORS). The HMI owns none of these values: a step publishes a
//              SeatCommand, and a row's number changes only when MibStatus says
//              so. That keeps a limit or an interlock one decision made in one
//              place, instead of two boards disagreeing about where the seat is.
//
// Memory: the screen and its rows exist only while it is up - see "Screens
// built on demand".
/////////////////////////////////////////////////////////////////////////////

// A row's fixed description, from either table, and how it is drawn: the
// formatters live in components/hmi_format (REQ-FMT-02..05).
using hmi::format::seat_format;
using hmi::format::stepper_format;
using hmi::format::StepperSpec;

// Raw value per actuator, in the table's units: seat_axis_value, defined with
// the seat view (frag_seat). The rows' observers point at them, and the MCB
// keeps them current with no page up.
static uint8_t seat_axis_count; // actuators in the table

// What a value reads before it is known - only ever an actuator the MCB has
// not reported yet. Not zero: zero is a position an actuator can genuinely be
// in, and drawing it would be claiming knowledge we do not have. Both buttons
// stay greyed while a row reads this, because commanding an actuator whose
// position is unknown is exactly what the request/response arrangement avoids.
static constexpr int32_t kValueUnknown = hmi::format::VALUE_UNKNOWN;

static void set_display_flipped(bool on); // screen flip, beside direct_flush_cb

// Applies and saves one of the Settings rows that has no observer of its
// own (brightness and theme do). user_data is its SETTINGS_PARAM_*.
static void setting_store_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const int param =
      static_cast<int>(reinterpret_cast<intptr_t>(lv_observer_get_user_data(observer)));
  const int value = lv_subject_get_int(subject);
  settings_set(param, value);
  if (param == SETTINGS_PARAM_FLIP) {
    set_display_flipped(value != 0);
    return;
  }
  // The stick's and the sounds' atomics; MENU_SLIDE is read where it is used, nothing to
  // apply.
  (void)applied_settings.apply(param, value);
}

// Seat values come from the MIB and nowhere else: a press publishes a request, and the
// number on screen moves only when the next MibStatus says the seat moved. The wire
// carries whole units, the screens raw integers, so each field is converted here.
static void seat_apply_state(const MIB::seatState &seat) {
  for (uint8_t i = 0; i < seat_axis_count; i++) {
    const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[i];
    lv_subject_set_int(&seat_axis_value[i],
                       rammp::seat_raw(spec, rammp::seat_field(seat, spec.id)));
  }
}

// One seat request: an absolute target, clamped to the axis' range, so a lost or repeated
// message cannot drift the seat.
static void seat_request(rammp::SeatAxis axis, int32_t target) {
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[rammp::index_of(axis)];
  rtps_comms_publish_seat(
      axis, rammp::seat_units(spec, std::clamp(target, spec.min_value, spec.max_value)));
}

// One step up or down from where the MCB last said the axis is.
static void seat_step(size_t row, int direction) {
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[row];
  const int32_t now = lv_subject_get_int(&seat_axis_value[row]);
  const int32_t from = now == kValueUnknown ? spec.min_value : now;
  seat_request(spec.id, from + direction * spec.step);
}

// The one instance. The pages' contents (the settings tables, the actuator
// rows, setting_page_open) and what a setting does when it changes
// (setting_store_observer) stay in main.
static constinit hmi::ui::SettingsView settings_view{{
    .nav = &ui_nav_port,
    .locked = &locked_subject,
    .subjects = &setting_subjects,
    .seat_values = seat_axis_value,
    .screen_ensure = settings_screen_ensure,
    .actuators_page = kActuatorsPage,
    .seat_step = seat_step,
    .refuse = refusal_feedback,
}};
static void setting_focus(int index) { settings_view.focus(index); }
static constexpr lv_event_cb_t setting_focus_cb = hmi::ui::SettingsView::focus_cb;
static void setting_rows_clear() { settings_view.rows_clear(); }

// ErrorBanner6. Raised only on a page that needs the MCB - the
// actuators - and then exactly as on the drive and seat screens: while the
// link is down or the MCB's state is not OK.
static void setting_warning_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  if (lv_subject_get_int(settings_view.page()) != kActuatorsPage || seat_ready()) {
    banner_show(panel, false);
    return;
  }
  fill_drive_blocked_panel(panel, rammp::kHmiLinkLostTitle, rammp::kHmiMcbFaultTitle);
  banner_show(panel, true);
}

// Fills the screen for `page` and shows it (SettingsView). LVGL task (a click handler).
static void setting_page_open(int32_t page) { settings_view.open_page(page); }
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

// A tile the MCB could not act on right now (hmi::ui::ActionsView::UNAVAILABLE);
// frag_nav greys its gated menu rows the same way.
static constexpr lv_state_t kActionUnavailable = hmi::ui::ActionsView::UNAVAILABLE;
static constexpr lv_style_selector_t kActionUnavailableStyle =
    hmi::ui::ActionsView::UNAVAILABLE_STYLE;

// Greys an MCB action while the MCB could not act on it. Bound to every
// subject mcb_ready() reads, so it follows the link and the state both.
static void action_ready_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_set_state(lv_observer_get_target_obj(observer), kActionUnavailable, !mcb_ready());
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
#include "hmi_format/diag.hpp"
#include "hmi_ui/diagnostics_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// DiagnosticsScreen: live readings from the MCB
//
// Opened from the DIAGNOSTICS settings row, left by pulling and holding. One
// row per entry in RAMMP_DIAG_TABLE (messages/joystick_message.hpp): short label, label,
// and up to three readings, each under its unit. The MCB publishes them all on
// rammp::kMcbDiagnostics every rammp::kDiagPeriod; they land in
// diag_value (subjects, set under the LVGL lock by the RTPS handler in
// app_main) and the rows observe them.
//
// DiagnosticsFreqLabel shows how fast they are arriving ("2.0 Hz - Live").
// Once nothing has arrived for rammp::kDiagTimeout - or nothing ever has -
// every row's text and the label turn red and blink: readings the MCB stopped
// sending must not sit there looking current.
//
// The red comes through LV_STATE_USER_1. The labels' text colours are themed
// for DEFAULT and FOCUSED, and a style on a higher state outranks both without
// touching anything the theme manager re-applies on a theme change.
//
// Built on demand like the settings and actions screens (see "Screens built
// on demand").
/////////////////////////////////////////////////////////////////////////////

static_assert(rammp::kDiagFields == 3, "the DiagnosticComponent has exactly three readings");

// The one instance. It owns the readings (the RTPS handler writes them through
// RtpsUiBridge), the stale and rate subjects and the rows' group; main's 250 ms poll
// (rtps_poll_cb) keeps the stale and rate subjects current through diag_poll.
static constinit hmi::ui::DiagnosticsView diag_view{{
    .nav = &ui_nav_port,
    .stats =
        [](int64_t *last_us, int32_t *rate_tenths_hz) {
          const RtpsDiagStats stats = rtps_comms_diag_stats();
          *last_us = stats.last_us;
          *rate_tenths_hz = stats.rate_tenths_hz;
        },
    .timeout_us =
        std::chrono::duration_cast<std::chrono::microseconds>(rammp::kDiagTimeout).count(),
    .blink = &rtps_blink_subject,
    .screen_ensure = diagnostics_screen_ensure,
    .row_focus_cb = setting_focus_cb,
}};
static void diag_freq_observer(lv_observer_t *observer, lv_subject_t *) {
  diag_view.paint_freq(lv_observer_get_target_obj(observer));
}
static void diag_poll() { diag_view.poll(); }
static void diag_focus(int index) { diag_view.focus(index); }
static void diag_rows_clear() { diag_view.rows_clear(); }
static void diagnostics_open() { diag_view.open(); }

/////////////////////////////////////////////////////////////////////////////
// The burger menu, and moving around with the joystick
//
// Spec V2 gives every screen but BootScreen the same chrome: a TopBar across
// the top, a DriveBand under it, a MenuKey in the bottom 162 px, and a
// MenuOverlay that exactly covers the 720x921 body between them and starts
// hidden. Tapping the key slides the overlay up over the body; the TopBar, the
// band and the key itself do not move, which is what the spec means by
// "sticky".
//
// The joystick reaches all of it. Every screen's focus group ends with that
// screen's burger key: push down past the last row (or key, or log line) and
// the key is focused; push up from it and you are back where you were. The
// stick button presses whatever is focused -- a row, a button, the key. In the
// open menu, up and down walk the rows, and left (or the key) closes it.
//
// The Drive screen is the exception, because there the stick drives: nothing
// on it takes focus but its key, which has no focus ring, so a short press of
// the stick button opens the menu and a hold still stops the chair
// (drive_exit_gesture). The button selects on RELEASE, and only for a press
// shorter than a hold's grace, so a hold never also clicks.
//
// What the cursor looks like: rows (menu, settings, diagnostics) go negative,
// the spec's own "selected"; buttons get a ring in the theme's focus colour,
// because on a button the negative already means pressed -- a drive mode
// filled is the one selected, and the key filled white is "menu open".
//
// Every screen carries its own instance of all four pieces of chrome, so
// nav_attach_chrome is called once per screen -- from app_main for the screens
// ui_init builds, and from the *_screen_ensure() functions for the ones built
// on demand.
//
// This lives in main.cpp rather than a ui_nav.cpp: every destination is a
// file-static here (the *_open functions, the focus groups, the PIN gate), and
// a header exporting all of them to one caller would be more coupling than the
// split removes.
/////////////////////////////////////////////////////////////////////////////
#include "frag_nav.inc" // split_main.py
// --
#include "frag_overdraw.inc" // split_main.py
// --
#include "frag_screens_on_demand.inc" // split_main.py
// --
#include "frag_display_flip.inc" // split_main.py
// --
// One LVGL cycle, for the UI island: lv_task_handler under the LVGL lock (CS-UI: only the
// UI task touches LVGL; RTPS handlers and the side button take the same lock).
static void lvgl_cycle() {
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  if constexpr (kFpsStress) {
    lv_obj_invalidate(lv_screen_active());
  }
  lv_task_handler();
}

// What the RTPS receive task's handlers change in the UI (RtpsUiBridge); the handlers in
// app_main take lvgl_mutex around it.
static constinit const hmi::ui::RtpsUiBridge rtps_ui_bridge({
    .mib_state = &mib_state_subject,
    .drive_band = &drive_band_view,
    .status_band = &status_band_view,
    .refusal = &refusal_view,
    .seat_apply_state = seat_apply_state,
    .diag_values = diag_view.values(),
});

// ---------------------------------------------------------------------------
// Adapters: the callbacks the board's own tasks run (app-main-shrink §3). Each
// stays on the task that calls it today; app_main builds and registers them.
// ---------------------------------------------------------------------------

// The touch adapter, on the BSP's touch task: a click on each press (a release
// and a new touch before it clicks again), and every change logged at debug level.
// NOTE: since we're directly using the touchpad data, and not using the
// TouchpadInput + LVGL, we'll need to ensure the touchpad data is
// converted into proper screen coordinates instead of simply using the
// raw values.
class TouchClick {
public:
  TouchClick(espp::M5StackTab5 &tab5, espp::Logger &logger)
      : tab5_(tab5)
      , logger_(logger) {}

  void operator()(const espp::TouchpadData &touch) {
    // The first touch only sets the reference, as the function-local static did.
    if (!previous_touchpad_data_) {
      previous_touchpad_data_ = tab5_.touchpad_convert(touch);
    }
    auto touchpad_data = tab5_.touchpad_convert(touch);
    if (touchpad_data != *previous_touchpad_data_) {
      logger_.debug("Touch: {}", touchpad_data);
      previous_touchpad_data_ = touchpad_data;

      // play a click sound only on the press transition (release + re-touch
      // required before it plays again)
      bool is_pressed = touchpad_data.num_touch_points > 0;
      if (is_pressed && !was_pressed_) {
        play_click(tab5_);
      }
      was_pressed_ = is_pressed;
    }
  }

private:
  espp::M5StackTab5 &tab5_;
  espp::Logger &logger_;
  std::optional<espp::TouchpadData> previous_touchpad_data_;
  bool was_pressed_ = false;
};

// The side button's adapter, on the BSP's button task: brightness control.
struct SideButton {
  espp::Logger &logger;

  void operator()(const espp::Interrupt::Event &state) const {
    logger.info("Button state: {}", state.active);
    if (state.active) {
      brightness_step();
    }
  }
};

// The joystick's LVGL keypad read, on the LVGL task: moves the cursor through each
// screen's focus group. It drains the latch the ADC task fills, so one flick of
// the stick = one PRESSED cycle = one LV_EVENT_KEY.
static void joystick_keypad_read(bool *up, bool *down, bool *left, bool *right, bool *enter,
                                 bool *escape) {
  // While the self-test overlay is up it owns the stick: nothing reaches
  // the screens behind it, and the stick button closes it once the run
  // has finished. This read runs on the LVGL task, under its lock.
  if (selftest_ui_visible()) {
    *left = *right = *up = *down = *enter = *escape = false;
    if (select_key.exchange(false)) {
      selftest_ui_dismiss();
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
    play_click(espp::M5StackTab5::get());
  }
}

// ---------------------------------------------------------------------------
// UI build: app_main's wiring of the screens, split by concern and called from
// app_main in this order, on its task, before the UI task starts (CS-LAY-01).
// ---------------------------------------------------------------------------

// app_main, UI build 1: the saved settings, the theme, the backlight and every
// Settings row's subject.
static void settings_ui_init() {
  // Saved settings (settings.cpp). The theme goes on before anything is drawn
  // (the boot overdraw pass further down runs after it, as it must: a theme
  // switch re-adds the fills that pass removes). The backlight is applied as
  // its observer is added.
  settings_load();
  if (const uint8_t theme = settings_theme();
      theme != ui_theme_idx && (theme == UI_THEME_DEFAULT || theme == UI_THEME_DAY)) {
    ui_theme_set(theme);
  }
  brightness_view.init(settings_brightness());
  // Every other row's subject: its saved value, and its observer (SettingSubjects).
  setting_subjects.init(setting_store_observer);
  brightness_view.start_save_timer(kBrightnessSaveDelayMs);
}

// app_main, UI build 2: the MCB, link and band subjects, before anything binds to them.
static void status_subjects_init() {
  // MCB status labels. The joystick is a slave: until the MCB says otherwise
  // the chair is not accepting drive commands, so INACTIVE/OK is the honest
  // default. The export draws a green "ACTIVE", so the initial observer run
  // repaints it grey — that is the point, not a flicker to design away.
  // Locked at boot, which is the screen ui_init leaves up, so the first
  // observer run is a no-op rather than a visible flicker. Before the chrome
  // binds, because lv_subject_init_int memzeroes the subject and would take any
  // observer already on it with it.
  lv_subject_init_int(&locked_subject, 1);
  lv_subject_init_int(&mib_state_subject, static_cast<int32_t>(MIB::MibSystemState::INITIALIZING));
  // Initialised before the panels bind, because their observers read it on the
  // first run. LINK_DOWN at boot is true and self-correcting: the poll timer
  // has the real answer a quarter second later.
  lv_subject_init_int(&rtps_link_subject, static_cast<int32_t>(RtpsLinkState::LINK_DOWN));
  lv_subject_init_int(&rtps_blink_subject, 1);
  // empty = no override, so the labels start on the enum names
  status_band_view.init_texts();
  lv_subject_init_int(drive_band_view.speed_subject(), 0);
  refusal_view.init_error_texts();
}

// app_main, UI build 3: every resident screen's chrome (TopBar, band, burger key), the
// clock and the Drive band.
static void chrome_bind() {
  // Every screen ui_init built, in the screen order of the header list above.
  // The three built on demand bind their own chrome in *_screen_ensure(), and
  // BenchMotorsScreen is destroyed a few lines after ui_init, so neither is
  // here. One list rather than three, because a screen whose band, TopBar and
  // chrome do not all get bound shows a frozen readout, and lining the three
  // calls up separately is how one gets forgotten.
  topbar_view.init(link_text(static_cast<NetLink>(settings_get(SETTINGS_PARAM_NETWORK))));
  hmi::ui::for_each_resident_chrome([](const hmi::ui::ScreenChrome &c) {
    bind_chrome_views(c.band, c.bar);
    nav_attach_chrome(c.key, c.overlay, c.band_goes_home ? c.band : nullptr);
  });
  // The menu stays reachable while locked: Log, Diagnostics, Settings and
  // the bench tools are all useful with the chair not driving -- and without an
  // MCB at all. Locked still means nothing moves: the band reads LOCKED on every
  // screen and the stick only drives from Drive (stick_drives).
  topbar_view.start_clock();
  drive_band_view.bind_speed(ui_SpeedValue);
  lv_subject_init_int(drive_band_view.profile_subject(),
                      static_cast<int32_t>(MIB::DriveProfile::NORMAL));
  drive_band_view.bind_profile_buttons(ui_ModeManual, ui_ModeAssist, ui_ModeAuto);
  drive_band_view.bind_profile_mirror();
}

// app_main, UI build 4: the error banners, the diagnostics subjects and the 250 ms poll.
static void banners_bind() {
  // Before the panels that observe it. lv_subject_init_int memzeroes the subject,
  // taking any observer already on it with it, and the lost panels bound below watch
  // this one so the drive screen can show a refused exit.
  lv_subject_init_int(&entry_refused_subject, 0);
  refusal_view.start_timer(kDriveRefusedShowMs);

  // The resident screens' error banners (RefusalView).
  refusal_view.bind_resident_banners();
  // Diagnostics readings, and whether they are live: before the poll timer
  // that keeps the latter current, and before any RTPS sample can land.
  diag_view.init_subjects();
  lv_timer_create(rtps_poll_cb, kRtpsPollMs, nullptr);
}

// app_main, UI build 5: the JoystickScreen's calibration and GPIO48 counter.
static void joystick_screen_init() {
  // Calibration on the JoystickScreen: holding Calibrate or the stick button
  // starts a run (calibrate_gesture), so the button is not handed to
  // joystick_cal as a tap-to-start -- only its label, which reads CANCEL during
  // a run. Its prompts blink on the RTPS indicator's phase, so this comes after
  // rtps_blink_subject is initialised.
  nav_focusable_button(ui_CalibrateButton);
  joystick_cal_init_ui({.screen = ui_JoystickScreen,
                        .button_label = ui_CalibrateButtonLabel,
                        .instructions = ui_JoystickHint,
                        .blink = &rtps_blink_subject,
                        .feedback = [] {
                          haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
                          play_click(espp::M5StackTab5::get());
                        }});

  // GPIO48 test button: the count and its colour (JoystickView).
  joystick_view.init_button();
}

// The lost-cursor backstop re-enters `screen` (NavView::start_input).
static void nav_cursor_lost(const lv_obj_t *screen) {
  logger_nav.warn("the stick had nothing focused on {}; re-entering it",
                  hmi::ui::NavView::screen_name(screen));
}

// app_main, UI build 6: the padlock's rest position and the three hold gestures.
static void lock_screen_init() {
  // Where the shackle sits at rest, so the 01b rise can be undone exactly.
  lv_obj_update_layout(ui_Shackle);
  shackle_rest_y = lv_obj_get_y(ui_Shackle);
  shackle_rest_h = lv_obj_get_height(ui_Shackle);
  lv_arc_set_range(ui_LockRing, 0, kHoldMax);
  lv_obj_remove_flag(ui_LockRing, LV_OBJ_FLAG_CLICKABLE);

  // The three push-and-hold gestures, polled by one shared timer — only the
  // gesture whose applies() is true on the current screen can be filling at any
  // moment. The subjects carry the fill, which is how hold_poll tells a hold
  // from a tap and what the ring and Calibrate's meter are bound to.
  lv_subject_init_int(&unlock_gesture.progress, 0);
  lv_subject_init_int(&drive_exit_gesture.progress, 0);
  lv_subject_init_int(&calibrate_gesture.progress, 0);
  // The ring round the padlock fills with the button hold.
  lv_subject_add_observer_obj(&unlock_gesture.progress, lock_ring_hold_observer, ui_LockRing,
                              nullptr);
  // Calibrate's meter shows the hold filling, and is out of sight while it is
  // empty. Held by touch or by the stick button, it is the same fill.
  lv_bar_set_range(ui_CalibrateFill, 0, kHoldMax);
  lv_bar_bind_value(ui_CalibrateFill, &calibrate_gesture.progress);
  lv_obj_bind_flag_if_eq(ui_CalibrateFill, &calibrate_gesture.progress, LV_OBJ_FLAG_HIDDEN, 0);
  lv_obj_remove_flag(ui_CalibrateFill, LV_OBJ_FLAG_CLICKABLE);
  lv_timer_create(hold_poll_cb, kHoldPollMs, nullptr);
}

// app_main, UI build 7: the Log, Seat, Internet and About screens.
static void screens_init() {
  // LogScreen: TextArea1 shows the serial output log_capture has kept. Its
  // ErrorBanner5 is left for menu refusals only: a link-lost
  // banner would cover the log at exactly the moment someone wants to read it.
  log_view_init();
  // Down at the newest line leaves the log for the burger key.
  log_view_set_escape(nav_to_key);

  // SeatScreen: both pages, their grids and groups.
  seat_view.init_pages();

  // InternetScreen: Ethernet or WiFi, the network list and the password page.
  // Its two pages cover the body, so their fill is what hides it.
  keep_overlay_fill(ui_NetPickPanel);
  keep_overlay_fill(ui_NetPwPanel);
  internet_ui_init({
      .lvgl_mutex = &lvgl_mutex,
      .connection = setting_subjects.value(SETTINGS_PARAM_NETWORK),
      .use_group = [](lv_group_t *group) { nav_use_group(group, ui_InternetScreen); },
      .focus_ring = nav_focus_ring,
      .mirror_states = nav_mirror_states,
      .claim_clicks = nav_claim_clicks,
      .refuse = refusal_feedback,
  });
  about_ui_init();
}

// app_main, UI build 8: the Update screen, and the OTA image's confirm.
static void update_screen_init() {
  // UpdateScreen: the GitHub releases, one release, and an install running.
  // Its two pages cover the body, so their fill is what hides it.
  keep_overlay_fill(ui_UpdatePickPanel);
  keep_overlay_fill(ui_UpdateRunPanel);
  update_ui_init({
      .lvgl_mutex = &lvgl_mutex,
      .use_group = [](lv_group_t *group) { nav_use_group(group, ui_UpdateScreen); },
      .focus_ring = nav_focus_ring,
      .mirror_states = nav_mirror_states,
      .claim_clicks = nav_claim_clicks,
      .refuse = refusal_feedback,
      // Never under a driving chair: the restart waits for the MIB to stop.
      .may_restart =
          [] {
            return static_cast<MIB::MibSystemState>(lv_subject_get_int(&mib_state_subject)) !=
                   MIB::MibSystemState::ENABLED;
          },
  });
  // An updated image boots unconfirmed, and the bootloader rolls back to the
  // one before if it resets first. Confirmed once the UI has run this long:
  // the LVGL task is up and nothing has crashed it (github_ota.hpp).
  lv_timer_set_repeat_count(
      lv_timer_create([](lv_timer_t *) { github_ota_boot_confirm(); }, kOtaConfirmAfterMs, nullptr),
      1);
}

// app_main, UI build 9: the seat values and the screen-loaded hooks.
static void screen_hooks_init() {
  // The seat values, shared by this screen and the DEBUG ACTUATORS page, and the
  // numbers bound to them.
  seat_axis_count = static_cast<uint8_t>(rammp::kSeatAxisCount);
  seat_view.init_values();

  // Hand the joystick between groups as the screen changes. Every screen
  // ui_init builds, so each route in and out is covered; the ones built on
  // demand register it in their own *_screen_ensure().
  for (lv_obj_t *screen :
       {ui_LockedScreen, ui_DriveScreen, ui_SeatScreen, ui_BenchGateScreen, ui_JoystickScreen,
        ui_LogScreen, ui_UpdateScreen, ui_InternetScreen, ui_AboutScreen}) {
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);
  }
}

// app_main, UI build 10: what outlives the screens built on demand (BenchGate's PIN pad,
// Settings, Skunk Works, Diagnostics) and the perf overlay's font.
static void on_demand_parts_init() {
  // BenchGateScreen: the PIN pad, its four dots and the line above them.
  (void)bench_pin_view.init();

  // SettingsScreen: what outlives the screen, which is built on demand
  // (settings_screen_ensure). The seat values it steps are initialised further
  // up, with the seat screen that shares them.

  (void)settings_view.init();

  // Initialised before the screen's warning panel ever binds to it.
  lv_subject_init_int(settings_view.page(), SETTINGS_PAGE_DISPLAY);

  // Which page is up is chosen in the burger menu: Settings' level has a row
  // per page (nav_go), so the screen needs no chooser of its own.

  // SkunkWorksScreen: what outlives the screen, which is built on demand
  // (actions_screen_ensure).
  (void)actions_view.init();

  // DiagnosticsScreen: what outlives the screen, which is built on demand
  // (diagnostics_screen_ensure). The menu row goes through diagnostics_open
  // rather than a SquareLine screen-change action, which would build the
  // screen without any of that.
  (void)diag_view.init();

  hmi::ui::perf_overlay_font();
}

// ---------------------------------------------------------------------------
// Board and service wiring: app_main's Configs and bring-up steps, by concern.
// ---------------------------------------------------------------------------

// app_main: the remote UI's hooks into the same latches the ADC task and the GPIO48 callback
// write (remote_ui.cpp).
static RemoteUiConfig remote_ui_config() {
  return {
      .lvgl_mutex = &lvgl_mutex,
      .set_key =
          [](uint32_t key) {
            remote_key.store(key);
            joy_key.store(key);
          },
      .press_select = [] { select_key.store(true); },
      .set_button = [](bool down) { stick_button_edge(down); },
      .screen_name = [] { return active_screen_name(); },
  };
}

// app_main: LVGL in DIRECT render mode over the DSI panel's two frame buffers (see the
// comment at the call). @return whether it came up; if not, the BSP's flush stays.
static bool start_direct_render(espp::M5StackTab5 &tab5, espp::Logger &logger) {
  void *fb0 = nullptr;
  void *fb1 = nullptr;
  esp_err_t fb_err = esp_lcd_dpi_panel_get_frame_buffer(tab5.lcd_panel_handle(), 2, &fb0, &fb1);
  const size_t fb_bytes = tab5.display_width() * tab5.display_height() * sizeof(uint16_t);
  // DIRECT mode renders into screen-sized frame buffers and direct_flush_cb
  // assumes the panel's native orientation, so it is incompatible with LVGL
  // software rotation. Assert here rather than depend on the
  // lv_display_set_rotation(ROTATION_0) call much further down.
  assert(lv_display_get_rotation(lv_display_get_default()) == LV_DISPLAY_ROTATION_0 &&
         "DIRECT render mode requires rotation 0");
  if (fb_err == ESP_OK && fb0 && fb1) {
    display_flip.use_panel_buffers(lv_display_get_default(), fb0, fb1, fb_bytes,
                                   tab5.display_width(), tab5.display_height());
    logger.info("LVGL rendering directly into the DSI frame buffers (DIRECT mode)");
    return true;
  }
  logger.error("Could not get DPI frame buffers ({}); leaving BSP flush in place",
               esp_err_to_name(fb_err));
  return false;
}

extern "C" void app_main(void) {
  // First, so the LogScreen has everything printed from here on - including
  // what the tasks started below print.
  log_capture_start();
  espp::Logger logger({.tag = "M5Stack Tab5 Example", .level = espp::Logger::Verbosity::INFO});
  logger.info("Starting example!");
  // Before anything reads /storage: the first boot of the two-slot layout brings the
  // calibration and settings over from where the old partition table kept them.
  storage_migrate_legacy();
  github_ota_boot_report();

  //! [m5stack tab5 example]
  espp::M5StackTab5 &tab5 = espp::M5StackTab5::get();
  logger.info("Running on M5Stack Tab5");

  // first let's get the internal i2c bus and probe for all devices on the bus
  logger.info("Probing internal I2C bus...");
  auto &i2c = tab5.internal_i2c();
  std::vector<uint8_t> found_addresses;
  // 0x08..0x77 only: the rest are reserved, and a glitched ACK there once put
  // 0x01 in this list, which the self test then reported as a lost device.
  for (uint8_t address = 0x08; address <= 0x77; address++) {
    if (i2c.probe_device(address)) {
      found_addresses.push_back(address);
    }
  }
  logger.info("Found devices at addresses: {::#02x}", found_addresses);

  // The haptic and sound cues: the DRV2605 comes up here (the HAPTIC TEST slot,
  // the unlock and hold clicks, a refusal); the click's samples load after the
  // LVGL task starts. Lives as long as app_main, which never returns once the UI
  // runs; the unit reaches it through `feedback`.
  hmi::feedback::Feedback cues({.i2c = i2c, .boot_log = logger, .sound = click_sound_config()});
  feedback = &cues;

  // DA7280 bring-up test (raw register read) and driver functional test (Da7280
  // driver class, DRO mode): bench only, CONFIG_HMI_BENCH_DA7280_TEST.
  hmi::feedback::run_da7280_bench(logger, i2c, found_addresses);

  // Initialize the IO expanders
  logger.info("Initializing IO expanders...");
  if (!tab5.initialize_io_expanders()) {
    logger.error("Failed to initialize IO expanders!");
    return;
  }

  // EXT5V_EN (0x43 P2) is asserted by the expander's default output mask; read it
  // back to confirm the M5-Bus / 2.54-10P / HY2.0-4P 5V rail is live
  auto ext_5v = tab5.get_io_expander_output(0x43, 2);
  logger.info("EXT_5V_EN: {}", ext_5v ? (*ext_5v ? "enabled" : "DISABLED") : "read failed");

  logger.info("Initializing lcd...");
  // initialize the LCD
  if (!tab5.initialize_lcd()) {
    logger.error("Failed to initialize LCD!");
    return;
  }

  // Query LCD controller
  const char *controller_name = tab5.get_display_controller_name();
  logger.info(controller_name);

  // initialize the display with full-screen draw buffers (the vendored BSP in
  // components/m5stack-tab5 allocates them in PSRAM)
  logger.info("Initializing display...");
  auto pixel_buffer_size = tab5.display_width() * tab5.display_height();
  if (!tab5.initialize_display(pixel_buffer_size)) {
    logger.error("Failed to initialize display!");
    return;
  }

  // Switch LVGL to DIRECT render mode over the panel's own frame buffers. The
  // BSP already handed those buffers to lv_display_set_buffers, but espp's
  // Display hardcodes RENDER_MODE_PARTIAL; DIRECT is what lets LVGL treat them
  // as real frame buffers (tracking dirty areas across both) so a flush is a
  // vsync-gated flip (see direct_flush_cb) rather than a copy. DIRECT requires
  // rotation 0, which is what this panel runs at. The self test reports whether it
  // came up.
  const bool direct_render = start_direct_render(tab5, logger);

  // run the LVGL refresh timer at 60 fps — the espp lv_conf.h compiles in a
  // 33 ms (30 fps) default period; the lv_task loop below already calls
  // lv_task_handler every 16 ms so it can keep up
  lv_display_t *const display = lv_display_get_default();
  lv_timer_set_period(lv_display_get_refr_timer(display), 16);

  if constexpr (kFpsInstrument) {
    fps_meter.attach(display);
    logger.info("FPS instrumentation enabled (stress={})", kFpsStress);
  }

  TouchClick touch_click(tab5, logger);

  // The housekeeping island (IMU, battery, RTC). Built here because the IMU takes its
  // orientation filter; its task starts after the click sound is loaded, below.
  hmi::housekeeping::Housekeeping housekeeping({
      .task =
          {
              .name = "Data Display Task",
              .stack_size_bytes = 6 * 1024,
              .priority = 10,
              .core_id = 1,
          },
      .period = 20ms,
      .angle_noise = 0.001f,
      .rate_noise = 0.1f,
  });

  logger.info("Initializing IMU...");
  // initialize the IMU
  if (!tab5.initialize_imu(housekeeping.orientation_filter())) {
    logger.error("Failed to initialize IMU!");
    return;
  }

  // initialize the uSD card
  using SdCardConfig = espp::M5StackTab5::SdCardConfig;
  SdCardConfig sdcard_config{};
  if (!tab5.initialize_sdcard(sdcard_config)) {
    logger.warn("Failed to initialize uSD card, there may not be a uSD card inserted!");
  } else {
    uint32_t size_mb = 0;
    uint32_t free_mb = 0;
    if (tab5.get_sd_card_info(&size_mb, &free_mb)) {
      logger.info("uSD card size: {} MB, free space: {} MB", size_mb, free_mb);
    } else {
      logger.warn("Failed to get uSD card info");
    }
  }

  // The system clock and the RTC, kept to the MCB's time (housekeeping). Lives as long
  // as app_main, which never returns once RTPS runs.
  hmi::housekeeping::SystemClock system_clock({.valid = &clock_valid, .max_drift_s = 2});

  logger.info("Initializing RTC...");
  // initialize the RTC
  if (!tab5.initialize_rtc()) {
    logger.error("Failed to initialize RTC!");
    return;
  }

  auto current_time = std::tm{};
  if (!tab5.get_rtc_time(current_time)) {
    logger.error("Failed to get RTC time");
    return;
  }

  // The RTC holds the MCB's local time (see "TopBar clock"). One that lost
  // power reads a date long gone; the clock then shows --:-- until the MCB
  // sends the time.
  if (clock_plausible(current_time)) {
    system_clock.set(current_time);
    logger.info("RTC time {:%Y-%m-%d %H:%M:%S}", current_time);
  } else {
    logger.warn("RTC not set ({:%Y-%m-%d}); the clock waits for the MCB", current_time);
  }

  logger.info("Initializing battery management...");
  // initialize battery monitoring
  if (!tab5.initialize_battery_monitoring()) {
    logger.error("Failed to initialize battery monitoring!");
    return;
  }

  // enable charging
  tab5.set_charging_enabled(true);

  logger.info("Initializing sound...");
  // initialize the sound
  if (!tab5.initialize_audio()) {
    logger.error("Failed to initialize sound!");
    return;
  }

  // Brightness control with button
  logger.info("Initializing button...");
  if (!tab5.initialize_button(SideButton{.logger = logger})) {
    logger.warn("Failed to initialize button");
  }

  logger.info("Setting up LVGL UI...");
  // Load the SquareLine Studio UI. This creates every screen and makes
  // ui_BootScreen the active one; the default screen LVGL started on stays
  // behind it, empty.
  logger.info("Loading SquareLine UI...");
  hmi::ui::build_screens(&boot_logo, kFpsInstrument);

  settings_ui_init();

  // The Joystick screen's axis bars, bound to the ADC task's percentages (JoystickView).
  joystick_view.init_bars();

  status_subjects_init();
  chrome_bind();
  banners_bind();

  joystick_screen_init();

  // GPIO48, pulled up and shorted to ground on press (board-wide convention),
  // so active LOW. Constructed after the subjects are initialized, because the
  // interrupt task starts here and its callback writes them. The internal
  // pull-up is redundant against the external one but harmless.
  logger.info("Initializing GPIO48 test button...");
  static espp::Button gpio48_button({
      .name = "GPIO48 Button",
      .interrupt_config =
          {
              .gpio_num = 48,
              .callback = gpio48_button_callback,
              .active_level = espp::Button::ActiveLevel::LOW,
              .interrupt_type = espp::Button::InterruptType::ANY_EDGE,
              .pullup_enabled = true,
          },
      .task_config = {.name = "Button", .stack_size_bytes = 4 * 1024, .priority = 5},
  });

  // Joystick as an LVGL keypad input device, moving the cursor through each
  // screen's focus group. The read function runs on the LVGL task and drains the
  // latch the ADC task fills, so one flick of the stick = one PRESSED cycle =
  // one LV_EVENT_KEY. Touch keeps working; indevs coexist.
  logger.info("Adding joystick keypad input device...");
  static espp::KeypadInput joystick_keypad({.read = joystick_keypad_read});
  // The joystick's indev in nav, the lost-cursor backstop and the key repeat.
  nav_view.start_input(joystick_keypad.get_input_device());

  lock_screen_init();

  screens_init();

  update_screen_init();

  screen_hooks_init();

  hmi::ui::finish_build(strip_all_overdraw);

  // The self test's checks and their limits are in selftest_spec.hpp;
  // selftest.cpp measures them. Started from the "Self test" Skunk Works slot,
  // or by a PC over RTPS (scripts/rtps_selftest.py) — which is why this comes
  // before rtps_comms_start: selftest_init registers the self-test RTPS
  // handlers.
  SelfTestPlatform selftest_board = selftest_platform(feedback, found_addresses, direct_render);
  selftest_board.lvgl_mutex = &lvgl_mutex;
  selftest_init(selftest_board);

  on_demand_parts_init();

  logger.info("Initializing touch...");
  if (!tab5.initialize_touch(
          [&touch_click](const espp::TouchpadData &touch) { touch_click(touch); })) {
    logger.error("Failed to initialize touch!");
    return;
  }
  if (auto touchpad = tab5.touchpad_input()) {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    display_flip.wrap_touch(touchpad->get_touchpad_input_device());
  }

  // The UI island: lv_task_handler every 8ms -- the refresh timer runs at 16ms
  // (60 fps), polling at twice that rate keeps its firing jitter well under a frame.
  logger.info("Starting LVGL task...");
  hmi::ui::UiIsland ui_island({
      .task =
          {
              .name = "lv_task",
              // Measured peak ~6 KB (self test mem.stk_lvgl: 26964 B of 32 KB
              // never used). The stack is internal DMA-capable RAM, which RTPS
              // start-up runs dry on: at 32 KB the W5500 driver's bounce buffer
              // failed to allocate and the board boot-looped. mem.stk_lvgl
              // guards the headroom.
              .stack_size_bytes = 16 * 1024,
              .priority = 20,
              .core_id = 1,
          },
      .cycle = lvgl_cycle,
      .period = 8ms,
      .fps_meter = kFpsInstrument ? &fps_meter : nullptr,
  });
  if (!ui_island.start()) {
    logger.error("Failed to start LVGL task!");
    return;
  }

  // load the audio file (wav file bundled in memory)
  size_t wav_size = 0;
  size_t wav_sample_rate = 0;
  if (!load_audio(wav_size, wav_sample_rate)) {
    logger.error("Failed to load audio file!");
    return;
  }
  logger.info("Loaded {} bytes of audio", wav_size);

  logger.info("Setting audio sample rate to {} Hz", wav_sample_rate);
  tab5.audio_sample_rate(wav_sample_rate);

  // unmute the audio and set the volume to 60%
  tab5.mute(false);
  tab5.volume(60.0f);

  // (brightness is the saved setting, applied when brightness_view.init adds its observer)

  logger.info("Starting data display task...");
  housekeeping.start();

  // guards the joystick range-mapping math (center/range deadbands, circular
  // clamp, and that twist stays independent of the X/Y gimbal). Asserts, so it
  // aborts loudly on a regression; compiles to nothing under NDEBUG.
  espp::joystick_selftest();

  logger.info("Starting continuous adc...");

  // X/Y: ADC1_CH0/CH1 = GPIO16/GPIO17 on the M5-Bus header.
  // Twist: ADC2_CH3 = GPIO52 on the M5-Bus (the W5500 INT moved to GPIO4 to
  // free it — analog inputs can't be re-routed through the GPIO matrix).
  // The twist channel is sampled oneshot rather than through the continuous
  // driver: mixing both units via ADC_CONV_BOTH_UNIT produced a stream of
  // invalid DMA frames on the P4 (log spam that starved LVGL's first frame).
  // The control island ("Read ADC"): the ADC drivers come up here, the task starts
  // once the calibration is loaded, below.
  static constexpr hmi::control::Cycle kStickCycle{
      // 30 Hz: ADC read, LVGL bars, RTPS publish
      .period_ms = 33,
      .twist_channel = {.unit = ADC_UNIT_2,
                        .channel = ADC_CHANNEL_3,
                        .attenuation = ADC_ATTEN_DB_12},
      // Twist is the noisy axis (~50 mV peak-to-peak at rest, where X/Y read ~1
      // mV): each cycle averages this many oneshot reads of it, which costs no
      // lag, then lowpasses the result to iron out what is left. 80 ms is short
      // enough that the chair does not feel late to turn.
      .twist_oversample = 8,
  };
  hmi::control::StickIsland<StickSlot, AdcStickIo, kStickCycle> stick_island({
      .task =
          {
              .name = "Read ADC",
              // espp's defaults, written out. Priority 0 runs at IDF's pthread default
              // (5), and unpinned the task is pinned by its first FPU use (core 0 on the
              // board): H11 is the fix, not this.
              .stack_size_bytes = 4096,
              .priority = 0,
              .core_id = -1,
          },
      .task_log_level = espp::Logger::Verbosity::INFO,
      // this initailizes the DMA and filter task for the continuous adc
      .adc = {.sample_rate_hz = 1 * 1000,
              .channels =
                  {{.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0, .attenuation = ADC_ATTEN_DB_12},
                   {.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_1, .attenuation = ADC_ATTEN_DB_12}},
              .convert_mode = ADC_CONV_SINGLE_UNIT_1,
              .window_size_bytes = 1024,
              .log_level = espp::Logger::Verbosity::WARN},
      .twist_lowpass = {.time_constant = 0.08f},
  });

  // Joystick calibration: where each axis rests and the two ends of its travel,
  // in raw mV. Measured per unit by CALIBRATE on the JoystickTest screen and
  // kept in flash (joystick_cal.cpp); these ideal-divider values are only what
  // a unit that was never calibrated runs on, and WILL be off on real hardware.
  // How the travel is mapped (deadzones, axis wiring) is under "Joystick
  // mapping" at the top of this file.
  static constexpr JoystickAxisCal kIdealAxis{
      .min_mv = 0.0f, .center_mv = 1650.0f, .max_mv = 3300.0f};
  const JoystickCal joystick_cal = joystick_cal_load({kIdealAxis, kIdealAxis, kIdealAxis});
  // The stick pipeline (components/stick): the joystick mapping on this
  // calibration, the key trigger and the gate, owned by the island's task. A
  // StickSlot is the StickPipeline itself, or with CONFIG_HMI_BENCH_STICK_INJECT
  // the bench stick injection in front of its reads (stick_inject.hpp).
  stick_island.start(stick_pipeline_config(joystick_cal));

  // bring up W5500 Ethernet + RTPS last so a missing cable / module can't
  // delay the HMI; on failure the UI keeps running without comms
  logger.info("Starting RTPS comms...");
  // remote LCD brightness (rtps_brightness.py on the PC); floor at 5% so a
  // remote command can't turn the screen fully off. brightness() drives the
  // backlight directly (no LVGL), so it's safe from the RTPS receive task.
  rtps_comms_on_brightness(
      [](float percent) { brightness_set(static_cast<int>(std::lround(percent))); });
  // MCB status -> the two DriveBand labels. Runs on the RTPS receive task,
  // so it only writes subjects — and takes the LVGL lock to do it, because
  // lv_subject_set_int runs the observers synchronously on this task and they
  // touch widgets.
  // RTPS handlers run on the RTPS task: subjects only, under the LVGL lock.
  rtps_comms_on_mib_status([&system_clock](const MIB::MibStatus &status) {
    system_clock.note_mcb_time(status); // no LVGL: sets the system clock and the RTC
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    rtps_ui_bridge.apply_mib_status(status);
  });
  rtps_comms_on_diagnostics([](const rammp::Diagnostics &diag) {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    rtps_ui_bridge.apply_diagnostics(diag);
  });
  if (!rtps_comms_start(static_cast<NetLink>(settings_get(SETTINGS_PARAM_NETWORK)))) {
    logger.warn("RTPS comms not started (network bring-up failed)");
  }
  {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    topbar_view.set_link(link_text(rtps_comms_net_link()));
  }
  // The firmware's SHA-256 for the About screen: ~4 MB of flash read on a
  // low-priority thread. Only once rtps_comms_start has set the W5500 up: run
  // across that, it left the chip with no TX buffer on most boots (no
  // link, or no DHCP lease).
  fw_info_start();

  // The remote UI debug channel (CONFIG_HMI_REMOTE_UI, off by default). Last,
  // because it drives everything above it: input goes into the same latches the
  // ADC task and the GPIO48 callback write, so what a script exercises is the
  // real handling and not a parallel path.
  remote_ui_start(remote_ui_config());

  // loop forever
  while (true) {
    std::this_thread::sleep_for(1s);
  }
  //! [m5stack tab5 example]
}

// The click sound's samples: click.wav, embedded by main/CMakeLists.txt
// (EMBED_TXTFILES). The sound itself is hmi::feedback::ClickSound.
static bool load_audio(size_t &out_size, size_t &out_sample_rate) {
  extern const uint8_t click_wav_start[] asm("_binary_click_wav_start");
  extern const uint8_t click_wav_end[] asm("_binary_click_wav_end");
  return feedback->sound().load({click_wav_start, click_wav_end}, out_size, out_sample_rate);
}

// The click: a touch landing, the stick button selecting, a hold completing.
// Silent with Settings "Sounds" off. Every caller passes the one Tab5, which is
// the speaker the Feedback plays on.
static void play_click(espp::M5StackTab5 & /*tab5*/) { feedback->sound().play_click(); }

// "Can't do that", heard. `warning` is a banner coming up: it sounds even with
// Sounds off. LVGL task, or under lvgl_mutex.
static void play_refusal(bool warning) { feedback->sound().play_refusal(warning); }

// A refused press: the DRV2605's double click, which says the same by touch.
static void refusal_feedback() { feedback->refusal(); }
