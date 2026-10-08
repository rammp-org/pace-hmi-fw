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
#include "frag_status_band.inc" // split_main.py
// --
#include "frag_stick_config.inc" // split_main.py
// --
#include "frag_rtps_label.inc" // split_main.py
// --
#include "frag_drive_band.inc" // split_main.py
// --
#include "frag_rtps_poll.inc" // split_main.py
// --
#include "frag_brightness.inc" // split_main.py
// --
#include "frag_clock.inc" // split_main.py
// --
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
  // Theme: the row switches the UI's palette; rtps_poll_cb notices the switch
  // (however it was made) and saves it, and keeps this subject in step.
  lv_subject_init_int(&theme_subject, ui_theme_idx == UI_THEME_DAY ? 1 : 0);
  lv_subject_add_observer(
      &theme_subject,
      [](lv_observer_t *, lv_subject_t *subject) {
        const uint8_t want = lv_subject_get_int(subject) != 0 ? UI_THEME_DAY : UI_THEME_DEFAULT;
        if (ui_theme_idx != want) {
          ui_theme_set(want);
        }
      },
      nullptr);
  // The rest of Settings: each row's subject starts at its saved value, and
  // setting_store_observer applies it (on this first run too, which is what
  // puts a saved flip or stick mapping back at boot) and saves any change.
  for (const auto &[subject, param] : std::initializer_list<std::pair<lv_subject_t *, int>>{
           {&menu_slide_subject, SETTINGS_PARAM_MENU_SLIDE},
           {&flip_subject, SETTINGS_PARAM_FLIP},
           {&stick_sensitivity_subject, SETTINGS_PARAM_STICK_SENSITIVITY},
           {&drive_speed_subject, SETTINGS_PARAM_DRIVE_SPEED},
           {&stick_invert_x_subject, SETTINGS_PARAM_STICK_INVERT_X},
           {&stick_invert_y_subject, SETTINGS_PARAM_STICK_INVERT_Y},
           {&stick_swap_subject, SETTINGS_PARAM_STICK_SWAP},
           {&sounds_subject, SETTINGS_PARAM_SOUNDS},
           {&network_subject, SETTINGS_PARAM_NETWORK},
       }) {
    lv_subject_init_int(subject, settings_get(param));
    lv_subject_add_observer(subject, setting_store_observer,
                            reinterpret_cast<void *>(static_cast<intptr_t>(param)));
  }
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
      .connection = &network_subject,
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

  setting_group = settings_view.init();

  // Initialised before the screen's warning panel ever binds to it.
  lv_subject_init_int(&setting_page_subject, SETTINGS_PAGE_DISPLAY);

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
