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
#include "frag_state.inc" // split_main.py
// --
#include "frag_haptics.inc" // split_main.py
// --
#include "hmi_ui/status_band_view.hpp"
#include "hmi_ui/ui_build.hpp"
// The "FPS counter" Skunk Works slot -> LVGL's built-in perf overlay (hmi::ui::PerfOverlay).
static constinit hmi::ui::PerfOverlay perf_overlay;
static void fps_toggle() { perf_overlay.toggle(); }

/////////////////////////////////////////////////////////////////////////////
// MCB status panel
//
// One view (hmi::ui::StatusBandView) serves both labels on every DriveBand
// instance. It sets the text (from the shared spec, so the MCB's logs and
// these labels use the same words) and the colour — a style property, which
// has no built-in binding, hence an observer rather than lv_label_bind_text.
/////////////////////////////////////////////////////////////////////////////

// The one instance, for every DriveBand; bound from app_main and the screens built on demand.
static constinit hmi::ui::StatusBandView status_band_view{{.shared = &ui_shared_subjects}};
static_assert(hmi::ui::StatusBandView::TEXT_SIZE == rammp::kMcbTextLen,
              "the band's text buffers are the shared spec's");
static void bind_status_panel(lv_obj_t *panel) { status_band_view.bind(panel); }

// --
#include "frag_stick_config.inc" // split_main.py
// --
#include "frag_rtps_label.inc" // split_main.py
// --
#include "hmi_format/speed.hpp"
#include "hmi_ui/drive_band_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// DriveScreen: drive-mode selection and the speed readout (hmi::ui::DriveBandView)
//
// Three plain LVGL buttons, so they are already touch-clickable; all this adds
// is what a tap means and which one looks selected. Deliberately touch-only:
// the joystick is busy driving on this screen, and stealing left/right for menu
// navigation is exactly the class of bug that made pulling back exit the
// screen.
/////////////////////////////////////////////////////////////////////////////

// The drive session's input, defined with the drive adapter below.
static bool drive_session_input(hmi::drive_session::Input input);

// The one instance; bound from app_main.
static constinit hmi::ui::DriveBandView drive_band_view{{
    .profiles = {static_cast<int32_t>(MIB::DriveProfile::HIGH),
                 static_cast<int32_t>(MIB::DriveProfile::NORMAL),
                 static_cast<int32_t>(MIB::DriveProfile::LOW)},
    // The ADC task cannot take the LVGL lock, so the profile reaches it through an atomic.
    .store_profile =
        [](int32_t profile) {
          drive_profile_published.store(static_cast<MIB::DriveProfile>(profile));
        },
    // PUBLISH_DRIVE: the drive request as it stands, with the new profile (rows 29-34).
    .profile_clicked = [] { (void)drive_session_input(hmi::drive_session::Input::PROFILE_CLICK); },
    .nav = &ui_nav_port,
}};

// MibStatus.speed is metres per second; the label shows mph to one decimal.
// Any task: pure arithmetic, no LVGL (components/hmi_format, REQ-FMT-01).
using hmi::format::speed_display_tenths;
static_assert(hmi::format::MPH_PER_MPS == rammp::kMphPerMps,
              "hmi_format's mph per m/s must be the shared spec's");
static_assert(hmi::format::SPEED_MAX_TENTHS == rammp::kSpeedMaxTenths,
              "hmi_format's speed clamp must be the shared spec's");

// --
#include "frag_rtps_poll.inc" // split_main.py
// --
#include "frag_brightness.inc" // split_main.py
// --
#include "frag_clock.inc" // split_main.py
// --
// The stick button's edges (hmi::ui::StickButton): the pressed panel, the level the ADC task
// publishes, the select key and the press counter.
#include "hmi_ui/stick_button.hpp"
static constinit hmi::ui::StickButton stick_button{{
    .view = &joystick_view,
    .level = &joy_button_pressed,
    .select = &select_key,
}};
static void stick_button_edge(bool active) {
  // lv_subject_set_int runs the observers synchronously on this task, and they
  // touch widgets, so this needs the LVGL lock
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
  stick_button.edge(active);
}

// Runs on the Button's interrupt task (not in ISR context).
static void gpio48_button_callback(const espp::Interrupt::Event &event) {
  stick_button_edge(event.active);
}

static bool load_audio(size_t &out_size, size_t &out_sample_rate);
static void play_click(espp::M5StackTab5 &tab5);
// "Can't do that", heard (play_refusal) or heard and felt (refusal_feedback);
// defined with play_click. A warning or error sounds even with Sounds off.
static void play_refusal(bool warning = false);
static void refusal_feedback();

/////////////////////////////////////////////////////////////////////////////
// Push-and-hold gestures
//
// Three places in the HMI ask the user to hold an input for kHoldMs before
// something happens:
//
//   LockedScreen   stick button    enters driving mode  -> DriveScreen
//   DriveScreen    stick button    leaves driving mode  -> LockedScreen
//   JoystickScreen stick button, or a finger on CALIBRATE: starts calibration
//
// Both on the button, never the stick (issue #11): an enable held on the stick
// left the user holding it forward the moment the chair would take it, and
// the chair drove off at full speed.
//
// Everything else that used to be a hold -- enter seat, enter actions, and the
// "pull the stick back to leave" exits on seat, bench, settings, actions,
// diagnostics and log -- is a burger-menu row now, one tap away from anywhere,
// so those gestures and the bars that showed their fill are gone.
//
// The DriveScreen is the odd one out because pulling the stick back is how you
// reverse: an exit on LV_KEY_DOWN there fired every time the user drove
// backwards, so it exits on the stick button instead.
//
// Each gesture's fill runs through a subject: the ring round the padlock and
// Calibrate's meter are bound to theirs, and the exit hold's has no widget.
//
// The lock state itself (locked_subject) is declared with the other subjects at
// the top of the file. Locked still leaves the menu reachable -- Log, UI
// Settings and the bench tools are useful with the chair not driving -- but
// nothing moves: the stick only drives from Drive (stick_drives).
/////////////////////////////////////////////////////////////////////////////

#include "hmi_ui/hold_gesture.hpp"
static constexpr uint32_t kHoldMs = hmi::ui::HOLD_MS; // hold time to fill a gesture widget
// Dead time before a hold starts filling; a select is a press shorter than this
// (hmi::stick::SELECT_MAX_US, the same value).
static constexpr uint32_t kBarGraceMs = hmi::ui::HOLD_GRACE_MS;
static constexpr int32_t kHoldMax = hmi::ui::HOLD_MAX; // arc/bar range (LVGL's default)
// Poll cadence for the inputs (matched to the ADC task's 33 ms period).
static constexpr uint32_t kHoldPollMs = hmi::ui::HOLD_POLL_MS;
// The gestures' timings are the drive table's (drive_session_table.hpp).
static_assert(std::chrono::milliseconds{hmi::ui::HOLD_MS} == hmi::drive_session::kHoldFill);
static_assert(std::chrono::milliseconds{hmi::ui::HOLD_GRACE_MS} == hmi::drive_session::kBarGrace);
static_assert(std::chrono::milliseconds{hmi::ui::HOLD_POLL_MS} ==
              hmi::drive_session::kHoldPollPeriod);
static_assert(hmi::stick::SELECT_MAX_US == int64_t{hmi::ui::HOLD_GRACE_MS} * 1000,
              "a press short enough to select never starts a hold filling");

// --
// Each hold's "let go first" flag is DriveUi's (button_armed, calibrate_armed).

// The gestures and the engine that runs them (hmi::ui::HoldGesture, HoldEngine).
using hmi::ui::HoldGesture;

// Defined with the refusal banners, further down: the push check that shadows the unlock hold.
static void entry_refusal_poll();

// The one engine. Its confirmation is the same for all three gestures. Both calls are
// non-blocking - one short I2C burst, and one xStreamBufferSend with a zero timeout - so
// neither holds the LVGL task for the duration of the effect it starts.
static constinit hmi::ui::HoldEngine hold_engine{{
    .confirm =
        [] {
          haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
          play_click(espp::M5StackTab5::get());
        },
    // The self-test overlay owns the stick while it is up (see the keypad read in app_main).
    .overlay_up = [] { return selftest_ui_visible(); },
    .before_poll = [] { entry_refusal_poll(); },
}};

// The stick's own button. Reads the level the button callback mirrors out, not
// the edge-latched select_key: a hold gesture needs to know the button is still
// down, and select_key is consumed by the first indev read after the press.
static bool joy_button_held() { return joy_button_pressed.load(); }

/////////////////////////////////////////////////////////////////////////////
// Lock / unlock, and the LockedScreen gesture
/////////////////////////////////////////////////////////////////////////////

// Locked means "not driving", and the Locked screen is where that changes.
// Holding the stick button -- the legend under the padlock says so; the spec's
// ACTIVATE DRIVE button is gone -- asks the MIB to start driving, and nothing
// unlocks until the MIB says it has (activate_drive, lock_open). The ring round
// the padlock shows where that stands: it fills while the button is held, a
// quarter of it goes round while the MIB is asked, and it closes when the MIB
// answers. Then the spec's 01b frame -- the shackle rises, the band flips to
// ACTIVE -- held one second, and Drive dissolves in (Motion timing,
// "ACTIVATE DRIVE").
//
// Locking again is the MIB's call too: the chair stops, asked (the drive-exit
// hold) or not (a fault, the link), and drive_screen_follow_state brings the
// Locked screen back with the reason on its banner.

// --
/////////////////////////////////////////////////////////////////////////////
// Moving between screens
//
// Spec V2 navigates with the burger menu: the key at the bottom of every
// screen opens a full-screen overlay of destinations, and DRIVE in the band
// goes home from anywhere. A screen change is just a screen change (see
// docs/ui-architecture.md).
//
// Swaps go through the SquareLine helper rather than lv_screen_load, so they
// take the same path as the boot screen's own transition and re-create the
// target if it was ever destroyed.
/////////////////////////////////////////////////////////////////////////////

// SettingsScreen, defined with its rows further down. The actuators
// page comes after the settings_spec.hpp pages.
static constexpr int32_t kActuatorsPage = SETTINGS_PAGE_COUNT;
static void setting_page_open(int32_t page);
static void settings_screen_ensure();
static void actions_screen_ensure();
static void actions_open();
static void diagnostics_screen_ensure();
static void diagnostics_open();

// --
// Is the MCB fit to drive, or to move the seat (DriveUi's readiness checks, hmi_ui). The
// views and the rest of the unit reach them through these.
static bool mcb_ready() { return hmi::ui::mcb_ready(ui_shared_subjects); }
static bool seat_ready() { return hmi::ui::seat_ready(ui_shared_subjects); }

/////////////////////////////////////////////////////////////////////////////
// Saying why driving is not permitted (hmi::ui::RefusalView)
//
// One cause, several ErrorBanners. On the Locked screen, and on the screens a
// menu refusal can happen over: why a request to drive, or to open Seat
// Functions, was refused - the unlock hold is gated in applies(), so a refused
// one otherwise does nothing at all. On the DriveScreen and the SeatScreen: why
// it was cut short, by the link dropping or the MCB faulting. All word the
// cause from hmi_rtps_spec.hpp.
/////////////////////////////////////////////////////////////////////////////
#include "hmi_ui/refusal_view.hpp"

// Which push was refused, so the panel can say which (hmi::ui::Refused); its dwells are
// hmi::ui::DRIVE_REFUSED_SHOW_MS and EXIT_REFUSED_SHOW_MS (refused.hpp).
static lv_subject_t entry_refused_subject; // hmi::ui::Refused; panel up unless REFUSED_NONE

static_assert(sizeof(rammp::kHmiEthFailedText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiLinkDownText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiWifiFailedText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiWifiDownText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiNoIpText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiNoPeerText) <= rammp::kErrorTextLen,
              "refusal body outgrows the banner it shares with MCB faults");
static_assert(sizeof(rammp::kHmiEthFailedFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiLinkDownFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiWifiFailedFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiWifiDownFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiNoIpFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiNoPeerFooter) <= rammp::kErrorFooterLen,
              "refusal footer outgrows the banner it shares with MCB faults");

// The drive session's input, defined with the drive adapter below.
static bool drive_session_input(hmi::drive_session::Input input);

// The banners' words, from the shared spec.
static constexpr hmi::ui::RefusalTexts kRefusalTexts{
    .eth_failed = {rammp::kHmiEthFailedText, rammp::kHmiEthFailedFooter},
    .wifi_failed = {rammp::kHmiWifiFailedText, rammp::kHmiWifiFailedFooter},
    .link_down = {rammp::kHmiLinkDownText, rammp::kHmiLinkDownFooter},
    .wifi_down = {rammp::kHmiWifiDownText, rammp::kHmiWifiDownFooter},
    .no_ip = {rammp::kHmiNoIpText, rammp::kHmiNoIpFooter},
    .no_peer = {rammp::kHmiNoPeerText, rammp::kHmiNoPeerFooter},
    .mcb_no_text_fmt = rammp::kHmiMcbNoTextFmt,
    .state_name = [](MIB::MibSystemState state) { return rammp::to_string(state); },
    .drive_stopped = {rammp::kHmiDriveStoppedTitle, rammp::kHmiDriveStoppedText,
                      rammp::kHmiDriveStoppedFooter},
    .drive_not_granted = {rammp::kHmiDriveNotGrantedTitle, rammp::kHmiDriveNotGrantedText,
                          rammp::kHmiDriveNotGrantedFooter},
    .exit_refused = {rammp::kHmiExitRefusedTitle, rammp::kHmiExitRefusedText,
                     rammp::kHmiExitRefusedFooter},
    .link_refused_title = rammp::kHmiLinkRefusedTitle,
    .mcb_refused_title = rammp::kHmiMcbRefusedTitle,
    .seat_link_refused_title = rammp::kHmiSeatLinkRefusedTitle,
    .seat_mcb_refused_title = rammp::kHmiSeatMcbRefusedTitle,
    .drive_lost_link_title = rammp::kHmiDriveLostLinkTitle,
    .drive_lost_mcb_title = rammp::kHmiDriveLostMcbTitle,
    .link_lost_title = rammp::kHmiLinkLostTitle,
    .mcb_fault_title = rammp::kHmiMcbFaultTitle,
};

// The MIB's texts' buffers are the shared spec's.
static_assert(hmi::ui::RefusalView::ERROR_TEXT_SIZE == rammp::kErrorTextLen &&
                  hmi::ui::RefusalView::ERROR_FOOTER_SIZE == rammp::kErrorFooterLen,
              "RefusalView's error text buffers are the shared spec's");

// The one instance, for every refusal banner.
static constinit hmi::ui::RefusalView refusal_view{{
    .shared = &ui_shared_subjects,
    .texts = &kRefusalTexts,
    .refused = &entry_refused_subject,
    .wifi = [] { return rtps_comms_net_link() == NetLink::WIFI; },
    .mcb_ready = mcb_ready,
    .play_refusal = [](bool warning) { play_refusal(warning); },
    .keep_overlay_fill = keep_overlay_fill,
    .button_held = joy_button_held,
    .now_us = [] { return esp_timer_get_time(); },
    .menu_open = [] { return nav_menu_open != nullptr; },
    // A push is decided by the drive session (rows 38-39: locked, on the Locked
    // screen, no menu, MCB not ready).
    .entry_push = [] { return drive_session_input(hmi::drive_session::Input::ENTRY_PUSH); },
    .grace_ms = kBarGraceMs,
}};

// What the rest of the unit calls (settings, the hold poll, app_main).
static void banner_show(lv_obj_t *panel, bool up) { refusal_view.show(panel, up); }
static void fill_drive_blocked_panel(lv_obj_t *panel, const char *link_title,
                                     const char *mcb_title) {
  refusal_view.fill_drive_blocked(panel, link_title, mcb_title);
}
static void bind_to_drive_blocked_cause(lv_obj_t *panel, lv_observer_cb_t cb) {
  refusal_view.bind_to_cause(panel, cb, nullptr);
}
static void entry_refusal_poll() { refusal_view.poll(); }

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
// --
// The drive session: lock, ask, unlock, drive, ask to stop. The decisions live in
// components/drive_session (DriveSession, checked against the AS-IS table in
// drive_session_table.hpp and its TABLE.md); sampling, the deadlines and performing the
// actions live in components/drive_adapter (DriveAdapter); each action's LVGL and RTPS call is
// hmi_ui's DrivePort (drive_port.hpp), over the drive UI (DriveUi: the padlock and the advance
// to Drive). Every input arrives on the LVGL task: rtps_poll_cb's tick, the hold completions,
// the nav callbacks and the unlock timer.
#include "hmi_ui/drive_port.hpp"
#include "hmi_ui/drive_ui.hpp"

// The one drive UI, DrivePort's `Ui`.
static constinit hmi::ui::DriveUi drive_ui{{
    .shared = &ui_shared_subjects,
    .refused = &entry_refused_subject,
    .refused_timer = [] { return refusal_view.timer(); },
    .menu_open = &nav_menu_open,
    .menu_on_arrival = &nav_menu_on_arrival,
    .profile = &drive_profile_published,
    .publish_drive = rtps_comms_publish_drive,
    .input = drive_session_input,
    .stick_drives = &stick_drives,
    .nav_home = nav_home,
    .refusal_feedback = refusal_feedback,
    .haptic_click = [] { haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1); },
}};

// The drive adapter's port: stateless over drive_ui.
static constexpr hmi::ui::DrivePort<hmi::ui::DriveUi> drive_port{&drive_ui};
static_assert(hmi::drive_adapter::DrivePort<hmi::ui::DrivePort<hmi::ui::DriveUi>>);

// The drive session's adapter: the session, its deadlines and latches, and the logger for a
// corrupted state, made at start-up with the rest (CS-SAF-04). Its windows are the table's
// kDriveAnswer and kDriveWait (pinned to the RTPS spec with the refusal banners above).
static hmi::drive_adapter::DriveAdapter<hmi::ui::DrivePort<hmi::ui::DriveUi>> drive_adapter{
    {.view = drive_port}};

// The inputs the fragments before this one declare: one input now; the 250 ms tick from
// rtps_poll_cb; the unlock hold completed (unlock_gesture).
static bool drive_session_input(hmi::drive_session::Input input) {
  return drive_adapter.input(input);
}
static void drive_wait_poll() { drive_adapter.tick(); }
static void drive_unlock_hold_done() { drive_adapter.unlock_hold_done(); }

// Holding the stick button on the Locked screen: how driving is asked for, the
// same hold that leaves it on Drive (and so the same "let go first" flag). Only
// while there is something to ask -- a MIB that is not ready gets the refusal
// once the press is a hold instead (entry_refusal_poll), rather than a ring
// that fills for a second and then says no.
static constinit HoldGesture unlock_gesture{
    .armed = drive_ui.button_armed(),
    .is_held = joy_button_held,
    .applies =
        [] {
          return lv_subject_get_int(&locked_subject) != 0 && !drive_ui.lock_waiting() &&
                 lv_screen_active() == ui_LockedScreen && nav_menu_open == nullptr && mcb_ready();
        },
    .completed = [] { drive_unlock_hold_done(); },
    // The button doubles as select (a tap opens the menu from the key), so a
    // tap must not tick the ring.
    .grace_ms = kBarGraceMs,
};

// Exits on the stick BUTTON, not on pulling the stick back: pulling back is how
// you drive in reverse, so a pull-to-exit gesture fired every time the user
// reversed. The button is the only input on this screen that means nothing to
// driving.
//
// This one survives the move to the burger menu because it is not navigation:
// it asks the MIB to stop. Leaving the screen by any other route does not.
static constinit HoldGesture drive_exit_gesture{
    .armed = drive_ui.button_armed(),
    .is_held = joy_button_held,
    .applies = [] { return lv_screen_active() == ui_DriveScreen && nav_menu_open == nullptr; },
    // Ask only. The session's relock (TICK_FOLLOW) closes the screen once the MIB actually
    // stops driving, so a MIB that does not stop cannot leave someone looking at the
    // main screen while the chair is still moving.
    .completed = [] { drive_adapter.exit_hold_done(); },
    // The button doubles as select, so a tap would visibly tick the bar and
    // snap back without this.
    .grace_ms = kBarGraceMs,
};
// --
// Defined with the rest of the seat navigation below, which is where the button
// grid it restores focus into is declared.
static void seat_show_buttons_page();

// Calibrate is press-and-HOLD, never a tap: a run takes the stick over for the
// best part of a minute, so it should not start from a brush of the screen or
// a stray click of the stick button. Holding the Calibrate button, or the stick
// button anywhere on the joystick screen, fills the meter along the bottom of
// the button (bound in app_main) and starts a run when it is full; the same
// hold during a run cancels it.
//
// The touch half is polled -- "is a pointer down, and on Calibrate?" at the
// gesture's own cadence -- rather than tracked from the button's press events,
// so there is no event to miss and nothing to keep in step. Both inputs feed
// the one gesture and share its grace and fill time.
static bool calibrate_held() {
  return joy_button_held() || hmi::ui::touch_held_on(ui_JoystickScreen, ui_CalibrateButton);
}

// Its own "let go first" flag (DriveUi's calibrate_armed), not the stick button's.
static constinit HoldGesture calibrate_gesture{
    .armed = drive_ui.calibrate_armed(),
    .is_held = calibrate_held,
    .applies = [] { return lv_screen_active() == ui_JoystickScreen && nav_menu_open == nullptr; },
    .completed = [] { joystick_cal_toggle(); },
    // The button doubles as select, so a tap must not tick the fill.
    .grace_ms = kBarGraceMs,
};

// Three gestures. Everything else that used to be one -- enter the drive, seat
// and actions screens, leave the seat, bench, settings, actions, diagnostics
// and log screens -- is a menu row or the DRIVE cell now.
static HoldGesture *const kHoldGestures[] = {
    &unlock_gesture,
    &drive_exit_gesture,
    &calibrate_gesture,
};

static void hold_poll_cb(lv_timer_t *) { hold_engine.poll_all(kHoldGestures); }

/////////////////////////////////////////////////////////////////////////////
// SeatScreen: joystick navigation of both pages
//
// The joystick walks each page as the grid the user sees rather than as the
// flat list LVGL's own focus_next would give. Each page has its own group and
// its own cursor:
//
//   function buttons                adjustment page (spec 04b)
//   [ FB Tilt   ] [ Side Tilt   ]   [ < ]
//   [ Elevation ] [ Translation ]   [     -     ] [     +     ]
//   [ Static    ] [ Dynamic     ]   [ 0deg ] [ 15deg ] [ 25deg ]
//
// The rows are different lengths, which is why a grid carries a per-row count
// rather than one column total. The ButtonGrid also serves the bench gate's
// PIN pad.
//
// Picking one of the four function buttons names its motion on the adjustment
// page and shows it over the buttons; "<", or left from the first column,
// hides it again. The page's buttons step the motion or send it to a preset.
/////////////////////////////////////////////////////////////////////////////

static constexpr int kGridMaxRows = 4; // the PIN pad's bottom row is the fourth
static constexpr int kGridMaxCols = 3;

// --
#include "hmi_models/grid.hpp"
#include "hmi_ui/button_grid.hpp"
#include "hmi_ui/seat_view.hpp"
using hmi::ui::ButtonGrid;
using hmi::ui::grid_key_cb;
using hmi::ui::grid_sync_cursor;
// The subtree helpers (components/hmi_ui).
#include "hmi_ui/widget_tree.hpp"
using hmi::ui::clear_click_focusable_recursive;
using hmi::ui::set_focused_recursive;

static_assert(kGridMaxRows == hmi::ui::GRID_MAX_ROWS && kGridMaxCols == hmi::ui::GRID_MAX_COLS,
              "ButtonGrid and the cursor model must agree on the grid's size");

// Both defined with the settings rows further down, which own the seat values
// and the path that asks the MCB to move one.
static void seat_step(size_t row, int direction);
static void seat_request(rammp::SeatAxis axis, int32_t target);

// Raw value per seat axis, in the table's units, as the MCB last reported it
// (seat_apply_state). Shared by the seat view and the DEBUG ACTUATORS rows;
// the command path (seat_step, seat_request) reads it too.
static lv_subject_t seat_axis_value[rammp::kSeatAxisCount];

// The one instance.
static constinit hmi::ui::SeatView seat_view{{
    .nav = &ui_nav_port,
    .values = seat_axis_value,
    .step = seat_step,
    .request = seat_request,
    .show_buttons_page = seat_show_buttons_page,
    .keep_overlay_fill = keep_overlay_fill,
}};
// nav_enter_screen resets the function buttons' cursor on arrival.
static constinit ButtonGrid &seat_buttons_grid = seat_view.buttons_grid();
static void seat_show_buttons_page() { seat_view.show_buttons_page(); }
// --
#include "frag_bench_pin.inc" // split_main.py
// --
#include "frag_settings_ui.inc" // split_main.py
// --
#include "frag_actions.inc" // split_main.py
// --
#include "frag_diag.inc" // split_main.py
// --
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
  refusal_view.start_timer(hmi::ui::DRIVE_REFUSED_SHOW_MS);

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
  // Where the shackle sits at rest, so the 01b rise can be undone exactly, and the ring's
  // range (DriveUi).
  drive_ui.init_lock();

  // The three push-and-hold gestures, polled by one shared timer — only the
  // gesture whose applies() is true on the current screen can be filling at any
  // moment. The subjects carry the fill, which is how hold_poll tells a hold
  // from a tap and what the ring and Calibrate's meter are bound to.
  lv_subject_init_int(&unlock_gesture.progress, 0);
  lv_subject_init_int(&drive_exit_gesture.progress, 0);
  lv_subject_init_int(&calibrate_gesture.progress, 0);
  // The ring round the padlock fills with the button hold.
  drive_ui.bind_ring(&unlock_gesture.progress);
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

#include "frag_audio.inc" // split_main.py
// --
