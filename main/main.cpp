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
#include <vector>

#include "m5stack-tab5.hpp"

#include "da7280.hpp"
#include "drv2605.hpp"

#include "kalman_filter.hpp"
#include "madgwick_filter.hpp"
#include "simple_lowpass_filter.hpp"

#include "ui.h"
// The chrome (DriveBand, ErrorBanner, TopBar, MenuKey, MenuOverlay) is made of
// SquareLine *components*, so their children are reached by index
// through ui_comp_get_child() rather than by a ui_* global.
#include "components/ui_comp_driveband.h"
#include "components/ui_comp_errorbanner.h"
#include "components/ui_comp_topbar.h"

#include "button.hpp"
#include "continuous_adc.hpp"
#include "joystick.hpp"
#include "keypad_input.hpp"
#include "oneshot_adc.hpp"

#include "about_ui.hpp"
#include "actions_spec.h"
#include "boot_logo.h"
#include "fw_info.hpp"
#include "github_ota.hpp"
#include "heap_watch.hpp"
#include "internet_ui.hpp"
#include "joystick_cal.hpp"
#include "log_capture.hpp"
#include "log_view.hpp"
#include "remote_ui.hpp"
#include "rtps_comms.hpp"
#include "selftest.hpp"
#include "settings.hpp"
#include "storage.hpp"
#include "update_ui.hpp"

#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_system.h"
#include "esp_timer.h"

using namespace std::chrono_literals;

static std::vector<uint8_t> audio_bytes;

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
#include "frag_da7280.inc" // split_main.py
// --
extern "C" void app_main(void) {
  heap_watch_start(); // debug only: CONFIG_HMI_DEBUG_HEAP_WATCH (static task, no heap)
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

  // DRV2605 haptic motor driver, armed with the 1 s waveform the HAPTIC TEST
  // settings row plays (the button is wired up after ui_init() below).
  init_haptic(logger, i2c);

  // DA7280 bring-up test (raw register read) and driver functional test (Da7280
  // driver class, DRO mode): bench only, CONFIG_HMI_BENCH_DA7280_TEST.
  if constexpr (kBenchDa7280Test) {
    test_da7280(logger, i2c, found_addresses);
    test_da7280_functional(logger, i2c);
  }

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
  // rotation 0, which is what this panel runs at.
  bool direct_render = false; // reported by the self test
  {
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
      lv_display_set_buffers(lv_display_get_default(), fb0, fb1, fb_bytes,
                             LV_DISPLAY_RENDER_MODE_DIRECT);
      lv_display_set_flush_cb(lv_display_get_default(), direct_flush_cb);
      panel_fb[0] = static_cast<uint8_t *>(fb0);
      panel_fb[1] = static_cast<uint8_t *>(fb1);
      panel_fb_bytes = fb_bytes;
      panel_w = tab5.display_width();
      panel_h = tab5.display_height();
      direct_render = true;
      logger.info("LVGL rendering directly into the DSI frame buffers (DIRECT mode)");
    } else {
      logger.error("Could not get DPI frame buffers ({}); leaving BSP flush in place",
                   esp_err_to_name(fb_err));
    }
  }

  // run the LVGL refresh timer at 60 fps — the espp lv_conf.h compiles in a
  // 33 ms (30 fps) default period; the lv_task loop below already calls
  // lv_task_handler every 16 ms so it can keep up
  lv_display_t *const display = lv_display_get_default();
  lv_timer_set_period(lv_display_get_refr_timer(display), 16);

  if constexpr (kFpsInstrument) {
    lv_display_add_event_cb(display, fps_render_start_cb, LV_EVENT_RENDER_START, nullptr);
    lv_display_add_event_cb(display, fps_render_ready_cb, LV_EVENT_RENDER_READY, nullptr);
    logger.info("FPS instrumentation enabled (stress={})", kFpsStress);
  }

  auto touch_callback = [&](const auto &touch) {
    // NOTE: since we're directly using the touchpad data, and not using the
    // TouchpadInput + LVGL, we'll need to ensure the touchpad data is
    // converted into proper screen coordinates instead of simply using the
    // raw values.
    static auto previous_touchpad_data = tab5.touchpad_convert(touch);
    auto touchpad_data = tab5.touchpad_convert(touch);
    if (touchpad_data != previous_touchpad_data) {
      logger.debug("Touch: {}", touchpad_data);
      previous_touchpad_data = touchpad_data;

      // play a click sound only on the press transition (release + re-touch
      // required before it plays again)
      static bool was_pressed = false;
      bool is_pressed = touchpad_data.num_touch_points > 0;
      if (is_pressed && !was_pressed) {
        play_click(tab5);
      }
      was_pressed = is_pressed;
    }
  };

  // make the filter we'll use for the IMU to compute the orientation
  static constexpr float angle_noise = 0.001f;
  static constexpr float rate_noise = 0.1f;
  static espp::KalmanFilter<2> kf;
  kf.set_process_noise(rate_noise);
  kf.set_measurement_noise(angle_noise);
  static constexpr float beta = 0.5f; // higher = more accelerometer, lower = more gyro
  static espp::MadgwickFilter f(beta);

  using Imu = espp::M5StackTab5::Imu;
  auto kalman_filter_fn = [](float dt, const Imu::Value &accel,
                             const Imu::Value &gyro) -> Imu::Value {
    // Apply Kalman filter
    float accelRoll = atan2(accel.y, accel.z);
    float accelPitch = atan2(-accel.x, sqrt(accel.y * accel.y + accel.z * accel.z));
    kf.predict({espp::deg_to_rad(gyro.x), espp::deg_to_rad(gyro.y)}, dt);
    kf.update({accelRoll, accelPitch});
    float roll, pitch;
    std::tie(roll, pitch) = kf.get_state();
    // return the computed orientation
    Imu::Value orientation{};
    orientation.roll = roll;
    orientation.pitch = pitch;
    orientation.yaw = 0.0f;
    return orientation;
  };

  auto madgwick_filter_fn = [](float dt, const Imu::Value &accel,
                               const Imu::Value &gyro) -> Imu::Value {
    // Apply Madgwick filter
    f.update(dt, accel.x, accel.y, accel.z, espp::deg_to_rad(gyro.x), espp::deg_to_rad(gyro.y),
             espp::deg_to_rad(gyro.z));
    float roll, pitch, yaw;
    f.get_euler(roll, pitch, yaw);
    // return the computed orientation
    Imu::Value orientation{};
    orientation.roll = espp::deg_to_rad(roll);
    orientation.pitch = espp::deg_to_rad(pitch);
    orientation.yaw = espp::deg_to_rad(yaw);
    return orientation;
  };

  logger.info("Initializing IMU...");
  // initialize the IMU
  if (!tab5.initialize_imu(kalman_filter_fn)) {
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
    clock_set(current_time);
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
  auto button_callback = [&](const auto &state) {
    logger.info("Button state: {}", state.active);
    if (state.active) {
      brightness_step();
    }
  };
  if (!tab5.initialize_button(button_callback)) {
    logger.warn("Failed to initialize button");
  }

  logger.info("Setting up LVGL UI...");
  // set the background color to white
  lv_obj_t *bg = lv_obj_create(lv_screen_active());
  lv_obj_set_size(bg, tab5.display_width(), tab5.display_height());
  lv_obj_set_style_bg_color(bg, lv_color_make(255, 255, 255), 0);

  // add text in the center of the screen
  lv_obj_t *label = lv_label_create(lv_screen_active());
  static std::string label_text = "\n\n\n\nTouch the screen!";
  lv_label_set_text(label, label_text.c_str());
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);

  // Create style for line 0 (blue line, used for kalman filter)
  static lv_style_t style_line0;
  lv_style_init(&style_line0);
  lv_style_set_line_width(&style_line0, 8);
  lv_style_set_line_color(&style_line0, lv_palette_main(LV_PALETTE_BLUE));
  lv_style_set_line_rounded(&style_line0, true);

  // make a line for showing the direction of "down"
  lv_obj_t *line0 = lv_line_create(lv_screen_active());
  static lv_point_precise_t line_points0[] = {{0, 0},
                                              {tab5.display_width(), tab5.display_height()}};
  lv_line_set_points(line0, line_points0, 2);
  lv_obj_add_style(line0, &style_line0, 0);

  // Create style for line 1 (red line, used for madgwick filter)
  static lv_style_t style_line1;
  lv_style_init(&style_line1);
  lv_style_set_line_width(&style_line1, 8);
  lv_style_set_line_color(&style_line1, lv_palette_main(LV_PALETTE_RED));
  lv_style_set_line_rounded(&style_line1, true);

  // make a line for showing the direction of "down"
  lv_obj_t *line1 = lv_line_create(lv_screen_active());
  static lv_point_precise_t line_points1[] = {{0, 0},
                                              {tab5.display_width(), tab5.display_height()}};
  lv_line_set_points(line1, line_points1, 2);
  lv_obj_add_style(line1, &style_line1, 0);

  static auto rotate_display = [&]() {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    static auto rotation = LV_DISPLAY_ROTATION_0;
    rotation = static_cast<lv_display_rotation_t>((static_cast<int>(rotation) + 1) % 4);
    lv_display_t *disp = lv_display_get_default();
    lv_disp_set_rotation(disp, rotation);
    // update the size of the screen
    lv_obj_set_size(bg, tab5.rotated_display_width(), tab5.rotated_display_height());
  };

  // add a button in the top left which (when pressed) will rotate the display
  // through 0, 90, 180, 270 degrees
  lv_obj_t *btn = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn, 50, 50);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *label_btn = lv_label_create(btn);
  lv_label_set_text(label_btn, LV_SYMBOL_REFRESH);
  // center the text in the button
  lv_obj_align(label_btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(
      btn, [](auto event) { rotate_display(); }, LV_EVENT_PRESSED, nullptr);

  // disable scrolling on the screen (so that it doesn't behave weirdly when
  // rotated and drawing with your finger)
  lv_obj_set_scrollbar_mode(lv_screen_active(), LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);

  // Load the SquareLine Studio UI. This creates every screen and makes
  // ui_BootScreen the active one; the demo widgets above stay on the (now
  // hidden) default screen. To go back to the demo screen at runtime, keep a
  // pointer to it (lv_screen_active() before this call) and lv_screen_load()
  // it again.
  logger.info("Loading SquareLine UI...");
  // The Tab5 panel is natively 720x1280 portrait, which is what the UI is drawn
  // for. DIRECT rendering needs rotation 0; Flip screen turns the picture in
  // the flush instead (set_display_flipped).
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_0);
  ui_init();
  // ui_init builds every screen, and this one is dead: the actuators are a
  // page of the SettingsScreen now (DEBUG ACTUATORS). Its widget tree sits in
  // internal RAM, and the W5500's SPI bounce buffer is allocated from the same
  // DMA-capable pool when RTPS starts - with the Skunk Works screen added
  // that pool ran dry and the board boot-looped (spi_master does not check the
  // allocation). Delete the screen in SquareLine and this line goes too: the
  // build fails on it, which is the reminder.
  ui_BenchMotorsScreen_screen_destroy();
  // Built on demand instead: see "Screens built on demand".
  ui_SettingsScreen_screen_destroy();
  ui_SkunkWorksScreen_screen_destroy();
  ui_DiagnosticsScreen_screen_destroy();

  // Swap the boot logo from the export's embedded SVG to a pre-rasterised A8
  // mask (main/boot_logo.c). Done here rather than in the SquareLine project
  // because import_ui.ps1 mirrors components/ui/ with robocopy /MIR and would put the
  // SVG straight back on the next import.
  //
  // This is what lets LV_USE_SVG, LV_USE_THORVG and LV_USE_VECTOR_GRAPHIC all
  // stay off: this wordmark was the only vector asset in the project, and the
  // entire ThorVG engine was compiled in to draw it once at boot.
  //
  // The scale is deliberately left alone. The export draws this at
  // lv_image_set_scale(300) and LVGL scales about the image's centre pivot, so
  // boot_logo.c is rasterised at the size the SVG DECLARED (457x196) rather
  // than its on-screen size -- feed it a pre-scaled source and the logo lands
  // about 40 px from where it sits today.
  //
  // A8 carries no colour of its own, so the wordmark's white has to come from
  // image_recolor. That is the same convention the export already uses for the
  // other single-ink assets (the padlock, the arrows); it just never set one
  // here, because a vector image brought its own fill.
  lv_image_set_src(ui_Image3, &boot_logo);
  lv_obj_set_style_image_recolor(ui_Image3, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(ui_Image3, LV_OPA_COVER, LV_PART_MAIN);

  // Benchmark against a real screen rather than the boot screen, whose logo
  // otherwise dominates every measurement. Only with kFpsInstrument.
  if constexpr (kFpsInstrument) {
    lv_screen_load(ui_DriveScreen);
  }

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

  // Bind the Settings-screen axis bars to the ADC subjects (observer pattern).
  // Bars show the calibrated joystick position as a percentage: -100..+100,
  // centered at 0; RTPS carries the same values as -1..+1 (see the ADC task).
  lv_subject_init_int(&adc_x_subject, 0);
  lv_subject_init_int(&adc_y_subject, 0);
  lv_subject_init_int(&adc_twist_subject, 0);
  lv_bar_set_range(ui_XAxisBar, -100, 100);
  lv_bar_set_range(ui_YAxisBar, -100, 100);
  lv_bar_set_range(ui_TwistBar, -100, 100);
  lv_bar_bind_value(ui_XAxisBar, &adc_x_subject);
  lv_bar_bind_value(ui_YAxisBar, &adc_y_subject);
  lv_bar_bind_value(ui_TwistBar, &adc_twist_subject);

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
  lv_subject_init_string(&drive_text_subject, drive_text_buf, drive_text_prev_buf,
                         sizeof(drive_text_buf), "");
  lv_subject_init_string(&state_text_subject, state_text_buf, state_text_prev_buf,
                         sizeof(state_text_buf), "");
  lv_subject_init_int(&speed_tenths_subject, 0);
  lv_subject_init_string(&error_text_subject, error_text_buf, error_text_prev_buf,
                         sizeof(error_text_buf), "");
  lv_subject_init_string(&error_footer_subject, error_footer_buf, error_footer_prev_buf,
                         sizeof(error_footer_buf), "");
  // Every screen ui_init built, in the screen order of the header list above.
  // The three built on demand bind their own chrome in *_screen_ensure(), and
  // BenchMotorsScreen is destroyed a few lines after ui_init, so neither is
  // here. One list rather than three, because a screen whose band, TopBar and
  // chrome do not all get bound shows a frozen readout, and lining the three
  // calls up separately is how one gets forgotten.
  topbar_view.init(link_text(static_cast<NetLink>(settings_get(SETTINGS_PARAM_NETWORK))));
  struct ScreenChrome {
    lv_obj_t *bar;
    lv_obj_t *band;
    lv_obj_t *key;
    lv_obj_t *overlay;
    bool band_goes_home;
  };
  const ScreenChrome kChrome[] = {
      // DRIVE goes "home", which while locked IS this screen; on it, the cell
      // only closes the menu. The unlock hold, or the menu's Drive row, is the way in.
      {ui_TopBar1, ui_DriveBand1, ui_MenuKey1, ui_MenuOverlay1, true},
      // DriveScreen too: DRIVE is how you back out of the menu without
      // picking a row, and it has to do that on the screen it goes back to.
      {ui_TopBar2, ui_DriveBand2, ui_MenuKey2, ui_MenuOverlay2, true},
      {ui_TopBar3, ui_DriveBand4, ui_MenuKey3, ui_MenuOverlay3, true},     // JoystickScreen
      {ui_TopBar4, ui_DriveBand3, ui_MenuKey4, ui_MenuOverlay4, true},     // SeatScreen
      {ui_TopBar5, ui_DriveBand11, ui_MenuKey11, ui_MenuOverlay11, true},  // BenchGateScreen
      {ui_TopBar6, ui_DriveBand5, ui_MenuKey6, ui_MenuOverlay6, true},     // LogScreen
      {ui_TopBar11, ui_DriveBand10, ui_MenuKey10, ui_MenuOverlay10, true}, // UpdateScreen
      {ui_TopBar12, ui_DriveBand12, ui_MenuKey12, ui_MenuOverlay12, true}, // InternetScreen
      {ui_TopBar13, ui_DriveBand13, ui_MenuKey13, ui_MenuOverlay13, true}, // AboutScreen
  };
  for (const ScreenChrome &c : kChrome) {
    bind_chrome_views(c.band, c.bar);
    nav_attach_chrome(c.key, c.overlay, c.band_goes_home ? c.band : nullptr);
  }
  // The menu stays reachable while locked: Log, Diagnostics, Settings and
  // the bench tools are all useful with the chair not driving -- and without an
  // MCB at all. Locked still means nothing moves: the band reads LOCKED on every
  // screen and the stick only drives from Drive (stick_drives).
  topbar_view.start_clock();
  lv_subject_add_observer_obj(&speed_tenths_subject, speed_label_observer, ui_SpeedValue, nullptr);
  lv_subject_init_int(&drive_profile_subject, static_cast<int32_t>(MIB::DriveProfile::NORMAL));
  bind_drive_profile_button(ui_ModeManual, &kProfileHigh);
  bind_drive_profile_button(ui_ModeAssist, &kProfileNormal);
  bind_drive_profile_button(ui_ModeAuto, &kProfileLow);
  lv_subject_add_observer(&drive_profile_subject, drive_mode_publish_observer, nullptr);
  // Before the panels that observe it. lv_subject_init_int memzeroes the subject,
  // taking any observer already on it with it, and bind_mcb_lost_panel below watches
  // this one so the drive screen can show a refused exit.
  lv_subject_init_int(&entry_refused_subject, 0);
  entry_refused_timer = lv_timer_create(entry_refused_timer_cb, kDriveRefusedShowMs, nullptr);
  lv_timer_pause(entry_refused_timer);

  bind_mcb_lost_panel(ui_ErrorBanner4); // DriveScreen
  bind_mcb_lost_panel(ui_ErrorBanner1); // SeatScreen
  // Initialised before the bind: the panel's observer reads it on its first
  // run. LockedScreen's banner, because a refused unlock is what it reports
  // (entry_refusal_poll).
  bind_entry_refused_panel(ui_ErrorBanner2); // LockedScreen
  // A refusal from the menu (Seat Functions, no MCB) can happen on any screen,
  // so the screens whose banner has no other job say it too. Diagnostics and
  // Skunk Works keep theirs down on purpose; Drive, Seat and Settings have
  // their own causes to show.
  bind_entry_refused_panel(ui_ErrorBanner5);  // LogScreen
  bind_entry_refused_panel(ui_ErrorBanner10); // JoystickScreen
  bind_entry_refused_panel(ui_ErrorBanner11); // BenchGateScreen
  bind_entry_refused_panel(ui_ErrorBanner9);  // UpdateScreen
  bind_entry_refused_panel(ui_ErrorBanner12); // InternetScreen
  bind_entry_refused_panel(ui_ErrorBanner13); // AboutScreen
  // Diagnostics readings, and whether they are live: before the poll timer
  // that keeps the latter current, and before any RTPS sample can land.
  for (auto &item : diag_value) {
    for (auto &reading : item) {
      lv_subject_init_int(&reading, kValueUnknown);
    }
  }
  lv_subject_init_int(&diag_stale_subject, 1);
  lv_subject_init_int(&diag_rate_subject, 0);
  lv_timer_create(rtps_poll_cb, kRtpsPollMs, nullptr);

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

  // GPIO48 test button. The count has a built-in binding; the tint the press
  // used to show went with the panel behind it, so the observer paints the
  // count's own text colour instead (torn down with the object).
  lv_subject_init_int(&button_count_subject, 0);
  lv_subject_init_int(&button_pressed_subject, 0);
  lv_label_bind_text(ui_ButtonCounter, &button_count_subject, "%d");
  lv_subject_add_observer_obj(&button_pressed_subject, gpio48_panel_observer, ui_ButtonCounter,
                              nullptr);

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
  static espp::KeypadInput joystick_keypad(
      {.read = [](bool *up, bool *down, bool *left, bool *right, bool *enter, bool *escape) {
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
      }});
  joystick_indev = joystick_keypad.get_input_device();
  // The fallback group, for the screens whose content nothing focuses: Drive,
  // Update and Boot. It holds nothing, so the stick's LVGL half is idle there
  // while hold_poll still reads the same latch for the exit hold.
  joystick_group = lv_group_create();
  // The burger menu's rows. Filled per overlay when the menu opens, because
  // every screen carries its own instance of all seven.
  menu_group = lv_group_create();
  lv_indev_set_group(joystick_indev, joystick_group);
  // A backstop for the stick losing its cursor: if its group ever has nothing
  // focused while the screen has settled, hand it back to the screen that is
  // up, as a fresh arrival would. Logged, because it means some path left the
  // group behind and that path wants fixing too.
  lv_timer_create(
      [](lv_timer_t *) {
        lv_group_t *g = lv_indev_get_group(joystick_indev);
        static int lost = 0;
        if (g != nullptr && lv_group_get_focused(g) != nullptr) {
          lost = 0;
          return;
        }
        // Only screens with the burger key, which always has something to
        // focus (Boot and Update have none), and only when it stays lost for
        // two checks in a row: a screen change or a row press in flight
        // passes through an empty group on its way to SCREEN_LOADED.
        if (nav_chrome_of(lv_screen_active()) == nullptr || nav_press_timer != nullptr ||
            ++lost < 2) {
          return;
        }
        lost = 0;
        logger_nav.warn("the stick had nothing focused on {}; re-entering it",
                        active_screen_name());
        nav_arrive(lv_screen_active());
      },
      500, nullptr);
  // hold-to-repeat feel. LVGL's defaults (400 ms then every 100 ms) are tuned
  // for a keyboard and run the settings list far too fast for a joystick you
  // steer with; these are the two knobs if it feels wrong on the bench.
  lv_indev_set_long_press_time(joystick_keypad.get_input_device(), 500);
  lv_indev_set_long_press_repeat_time(joystick_keypad.get_input_device(), 250);

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

  // LV_USE_PERF_MONITOR makes lv_display_create() show the overlay immediately
  // (lv_display.c calls lv_sysmon_show_performance), so start it hidden — it is
  // a debug readout, not part of the normal HMI. The label already exists by
  // now, which is what makes the hide safe. The "FPS counter" Skunk Works slot
  // toggles it from here on.
  lv_sysmon_hide_performance(lv_display_get_default());

  // Remove the redundant nested background fills (see strip_redundant_backgrounds).
  strip_all_overdraw();

  // Leave the boot logo. Spec V1 did this with a screen-change event set in
  // SquareLine; firmware owns it now, because the honest moment to leave is
  // when everything behind the first screen is wired -- which is here, not a
  // fixed delay the designer picked. kBootHoldMs is only so the wordmark is
  // readable rather than a flash.
  //
  // LockedScreen and not DriveScreen: locked_subject starts at 1 and the chair
  // does not drive until someone unlocks it.
  static constexpr uint32_t kBootHoldMs = 1200;
  lv_timer_t *boot_done = lv_timer_create(
      [](lv_timer_t *) {
        _ui_screen_change(&ui_LockedScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, 280, 0,
                          &ui_LockedScreen_screen_init);
      },
      kBootHoldMs, nullptr);
  lv_timer_set_repeat_count(boot_done, 1);

  // The self test's checks and their limits are in selftest_spec.hpp;
  // selftest.cpp measures them. Started from the "Self test" Skunk Works slot,
  // or by a PC over RTPS (scripts/rtps_selftest.py) — which is why this comes
  // before rtps_comms_start: selftest_init registers the self-test RTPS
  // handlers.
  selftest_init({
      .lvgl_mutex = &lvgl_mutex,
      .imu_accel_mg = []() -> std::optional<int32_t> {
        // the cached reading the data display task keeps fresh, so no bus traffic
        auto imu = espp::M5StackTab5::get().imu();
        if (!imu) {
          return std::nullopt;
        }
        const auto a = imu->get_accelerometer();
        return static_cast<int32_t>(
            std::lround(std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z) * 1000.0f));
      },
      .rtc_seconds = []() -> std::optional<int64_t> {
        std::tm now{};
        if (!espp::M5StackTab5::get().get_rtc_time(now)) {
          return std::nullopt;
        }
        return static_cast<int64_t>(std::mktime(&now));
      },
      .battery_mv = []() -> std::optional<int32_t> {
        const auto battery = espp::M5StackTab5::get().get_battery_status();
        // is_present only says the INA226 answered. Whether the reading is a
        // pack at all is the self test's call (pwr.vbat in selftest_spec.hpp),
        // so the raw voltage goes through and can be seen in its detail.
        if (!battery.is_present) {
          return std::nullopt;
        }
        return static_cast<int32_t>(std::lround(battery.voltage_v * 1000.0f));
      },
      // 0..100: app_main sets 75.0f
      .backlight_percent = []() -> int32_t {
        return static_cast<int32_t>(std::lround(espp::M5StackTab5::get().brightness()));
      },
      .i2c_probe =
          [](uint8_t address) {
            return espp::M5StackTab5::get().internal_i2c().probe_device(address);
          },
      .boot_i2c_devices = found_addresses,
      .drv2605_status = drv2605_status,
      .drv2605_play = drv2605_play_click,
      .da7280_found = std::find(found_addresses.begin(), found_addresses.end(), kDa7280Address) !=
                      found_addresses.end(),
      .direct_render = direct_render,
      .joystick_cal_centers_mv = []() -> std::optional<std::array<float, 3>> {
        if (!joystick_cal_saved()) {
          return std::nullopt;
        }
        const JoystickCal cal = joystick_cal_current();
        return std::array<float, 3>{cal[JOY_HORIZONTAL].center_mv, cal[JOY_VERTICAL].center_mv,
                                    cal[JOY_TWIST].center_mv};
      },
  });

  // BenchGateScreen: the PIN pad, its four dots and the line above them.
  rd_group = bench_pin_view.init();

  // SettingsScreen: what outlives the screen, which is built on demand
  // (settings_screen_ensure). The seat values it steps are initialised further
  // up, with the seat screen that shares them.

  lv_subject_init_int(&actuator_reject_subject, kActuatorRejectNone);
  actuator_reject_timer =
      lv_timer_create(actuator_reject_clear_cb, kActuatorRejectFlashMs, nullptr);
  lv_timer_pause(actuator_reject_timer);
  press_flash_timer = lv_timer_create(press_flash_cb, kPressFlashMs, nullptr);
  lv_timer_pause(press_flash_timer);
  setting_group = lv_group_create();

  // Initialised before the screen's warning panel ever binds to it.
  lv_subject_init_int(&setting_page_subject, SETTINGS_PAGE_DISPLAY);

  // Which page is up is chosen in the burger menu: Settings' level has a row
  // per page (nav_go), so the screen needs no chooser of its own.

  // SkunkWorksScreen: what outlives the screen, which is built on demand
  // (actions_screen_ensure).
  actions_group = lv_group_create();

  // DiagnosticsScreen: what outlives the screen, which is built on demand
  // (diagnostics_screen_ensure). The menu row goes through diagnostics_open
  // rather than a SquareLine screen-change action, which would build the
  // screen without any of that.
  diag_group = lv_group_create();

  // The overlay hardcodes LVGL's 14 px default font (lv_sysmon_create sets no
  // font at all), which is unreadable on a 1280x720 panel at arm's length.
  // There's no API or Kconfig for it, but the label is parented to the sys
  // layer, and with LV_USE_MEM_MONITOR off it is that layer's only child.
  if (lv_obj_t *perf_label =
          lv_obj_get_child(lv_display_get_layer_sys(lv_display_get_default()), 0)) {
    lv_obj_set_style_text_font(perf_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_pad_all(perf_label, 10, 0); // grow the backing box to match
  }

  logger.info("Initializing touch...");
  if (!tab5.initialize_touch(touch_callback)) {
    logger.error("Failed to initialize touch!");
    return;
  }
  if (auto touchpad = tab5.touchpad_input()) {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    flip_wrap_touch(touchpad->get_touchpad_input_device());
  }

  // start a simple thread to do the lv_task_handler every 8ms — the refresh
  // timer runs at 16ms (60 fps), polling at twice that rate keeps its firing
  // jitter well under a frame
  logger.info("Starting LVGL task...");
  espp::Task lv_task(
      {.callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
         // steady_clock, never high_resolution_clock: on ESP-IDF that one is the
         // wall clock, which the MCB's time moves (see "TopBar clock"), and
         // wait_until on a wall clock that steps back sleeps out the whole step
         // - the screen froze for as long as the clock went back.
         auto start_time = std::chrono::steady_clock::now();
         {
           std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
           if constexpr (kFpsStress) {
             lv_obj_invalidate(lv_screen_active());
           }
           lv_task_handler();
         }
         if constexpr (kFpsInstrument) {
           static int64_t last_report_us = esp_timer_get_time();
           const int64_t now_us = esp_timer_get_time();
           if (now_us - last_report_us >= 1000000) {
             // Made per report rather than kept in a static: debug build, once a second.
             const espp::Logger fps_log({.tag = "fps", .level = espp::Logger::Verbosity::DEBUG});
             const uint32_t frames = fps_frames.exchange(0);
             const uint64_t total_us = fps_render_us_total.exchange(0);
             const uint32_t max_us = fps_render_us_max.exchange(0);
             const float secs = (now_us - last_report_us) / 1e6f;
             last_report_us = now_us;
             fps_log.debug("[FPS] {:.1f} fps | render avg {:.2f} ms | max {:.2f} ms", frames / secs,
                           frames ? (total_us / 1000.0f) / frames : 0.0f, max_us / 1000.0f);
           }
         }
         std::unique_lock<std::mutex> lock(m);
         // Always yield at least one tick: once a render cycle exceeds 8 ms
         // the deadline is already past and wait_until returns without
         // yielding, which pins core 1 at priority 20 and starves IDLE1.
         const auto deadline = std::max(start_time + 8ms, std::chrono::steady_clock::now() + 1ms);
         cv.wait_until(lock, deadline, []() { return false; });
         return false;
       },
       .task_config = {
           .name = "lv_task",
           // Measured peak ~6 KB (self test mem.stk_lvgl: 26964 B of 32 KB
           // never used). The stack is internal DMA-capable RAM, which RTPS
           // start-up runs dry on: at 32 KB the W5500 driver's bounce buffer
           // failed to allocate and the board boot-looped. mem.stk_lvgl
           // guards the headroom.
           .stack_size_bytes = 16 * 1024,
           .priority = 20,
           .core_id = 1,
       }});
  if (!lv_task.start()) {
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

  // make a task to read out various data such as IMU, battery monitoring, etc.
  // and print it to screen
  logger.info("Starting data display task...");
  espp::Task imu_task(
      {.callback = [&](std::mutex &m, std::condition_variable &cv) -> bool {
         // sleep first in case we don't get IMU data and need to exit early
         {
           std::unique_lock<std::mutex> lock(m);
           cv.wait_for(lock, 20ms);
         }
         static auto &tab5 = espp::M5StackTab5::get();
         static auto imu = tab5.imu();

         //////////////////////////////////////////////////////////////////////////
         // Update the Date/Time from the RTC
         //////////////////////////////////////////////////////////////////////////
         std::tm rtc_time;
         std::string rtc_text = "";
         if (tab5.get_rtc_time(rtc_time)) {
           rtc_text = fmt::format("\n{:%Y-%m-%d %H:%M:%S}\n", rtc_time);
         }

         //////////////////////////////////////////////////////////////////////////
         // Update the battery status
         //////////////////////////////////////////////////////////////////////////
         auto battery_status = tab5.read_battery_status();
         std::string battery_text =
             fmt::format("\nBattery: {:0.2f} V, {:0.1f} mA, {:0.1f} %, Charging: {}\n",
                         battery_status.voltage_v, battery_status.current_ma,
                         battery_status.charge_percent, battery_status.is_charging ? "Yes" : "No");

         auto now = esp_timer_get_time(); // time in microseconds
         static auto t0 = now;
         auto t1 = now;
         float dt = (t1 - t0) / 1'000'000.0f; // convert us to s
         t0 = t1;

         //////////////////////////////////////////////////////////////////////////
         // Update the IMU data
         //////////////////////////////////////////////////////////////////////////
         std::error_code ec;
         // update the imu data
         if (!imu->update(dt, ec)) {
           return false;
         }
         // get accel
         auto accel = imu->get_accelerometer();
         auto gyro = imu->get_gyroscope();
         auto temp = imu->get_temperature();
         auto orientation = imu->get_orientation();
         auto gravity_vector = imu->get_gravity_vector();
         // invert the axes
         gravity_vector.y = -gravity_vector.y;
         gravity_vector.x = -gravity_vector.x;

         // now update the gravity vector line to show the direction of "down"
         // taking into account the configured rotation of the display
         auto rotation = lv_display_get_rotation(lv_display_get_default());
         if (rotation == LV_DISPLAY_ROTATION_90) {
           std::swap(gravity_vector.x, gravity_vector.y);
           gravity_vector.x = -gravity_vector.x;
         } else if (rotation == LV_DISPLAY_ROTATION_180) {
           gravity_vector.x = -gravity_vector.x;
           gravity_vector.y = -gravity_vector.y;
         } else if (rotation == LV_DISPLAY_ROTATION_270) {
           std::swap(gravity_vector.x, gravity_vector.y);
           gravity_vector.y = -gravity_vector.y;
         }

         // separator for imu
         std::string imu_text = "\nIMU Data:\n";
         imu_text += fmt::format("Accel: {:02.2f} {:02.2f} {:02.2f}\n", accel.x, accel.y, accel.z);
         imu_text += fmt::format("Gyro: {:03.2f} {:03.2f} {:03.2f}\n", espp::deg_to_rad(gyro.x),
                                 espp::deg_to_rad(gyro.y), espp::deg_to_rad(gyro.z));
         imu_text += fmt::format("Angle: {:03.2f} {:03.2f}\n", espp::rad_to_deg(orientation.roll),
                                 espp::rad_to_deg(orientation.pitch));
         imu_text += fmt::format("Temp: {:02.1f} C\n", temp);

         // use the pitch to to draw a line on the screen indiating the
         // direction from the center of the screen to "down"
         int x0 = tab5.rotated_display_width() / 2;
         int y0 = tab5.rotated_display_height() / 2;

         int x1 = x0 + 50 * gravity_vector.x;
         int y1 = y0 + 50 * gravity_vector.y;

         static lv_point_precise_t line_points0[2] = {};
         line_points0[0].x = x0;
         line_points0[0].y = y0;
         line_points0[1].x = x1;
         line_points0[1].y = y1;

         // Now show the madgwick filter
         auto madgwick_orientation = madgwick_filter_fn(dt, accel, gyro);
         float roll = madgwick_orientation.roll;
         float pitch = madgwick_orientation.pitch;
         [[maybe_unused]] float yaw = madgwick_orientation.yaw;
         float vx = sin(pitch);
         float vy = -cos(pitch) * sin(roll);
         [[maybe_unused]] float vz = -cos(pitch) * cos(roll);

         // invert the axes
         vx = -vx;
         vy = -vy;

         // now update the line to show the direction of "down" based on the
         // configured rotation of the display
         if (rotation == LV_DISPLAY_ROTATION_90) {
           std::swap(vx, vy);
           vx = -vx;
         } else if (rotation == LV_DISPLAY_ROTATION_180) {
           vx = -vx;
           vy = -vy;
         } else if (rotation == LV_DISPLAY_ROTATION_270) {
           std::swap(vx, vy);
           vy = -vy;
         }

         x1 = x0 + 50 * vx;
         y1 = y0 + 50 * vy;

         static lv_point_precise_t line_points1[2] = {};
         line_points1[0].x = x0;
         line_points1[0].y = y0;
         line_points1[1].x = x1;
         line_points1[1].y = y1;

         std::string text = fmt::format("{}\n\n\n\n\n", label_text);
         text += battery_text;
         text += rtc_text;
         text += imu_text;

         std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
         lv_label_set_text(label, text.c_str());
         lv_line_set_points(line0, line_points0, 2);
         lv_line_set_points(line1, line_points1, 2);

         return false;
       },
       .task_config = {
           .name = "Data Display Task",
           .stack_size_bytes = 6 * 1024,
           .priority = 10,
           .core_id = 1,
       }});
  imu_task.start();

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
  std::vector<espp::AdcConfig> channels{
      {.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0, .attenuation = ADC_ATTEN_DB_12},
      {.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_1, .attenuation = ADC_ATTEN_DB_12}};
  static const espp::AdcConfig twist_channel{
      .unit = ADC_UNIT_2, .channel = ADC_CHANNEL_3, .attenuation = ADC_ATTEN_DB_12};
  // this initailizes the DMA and filter task for the continuous adc
  espp::ContinuousAdc adc({.sample_rate_hz = 1 * 1000,
                           .channels = channels,
                           .convert_mode = ADC_CONV_SINGLE_UNIT_1,
                           .window_size_bytes = 1024,
                           .log_level = espp::Logger::Verbosity::WARN});
  adc.start();
  static espp::OneshotAdc twist_adc({.unit = ADC_UNIT_2, .channels = {twist_channel}});

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
  // calibration, the key trigger and the gate. Owned by the ADC task below.
  // Named `stick`, as the espp::Joystick it wraps was: the same lazy static,
  // built at the same point (tools/guards init_order baseline).
  static hmi::stick::StickPipeline stick(stick_pipeline_config(joystick_cal));

  // customization knobs: sampling/LVGL/RTPS cadence, and how often the serial
  // line is printed. The log is divided down because 30 lines/s is the
  // console-flood pattern that starved LVGL once before.
  static constexpr auto kAdcUpdatePeriod = 33ms; // 30 Hz: ADC read, LVGL bars, RTPS publish
  // Twist is the noisy axis (~50 mV peak-to-peak at rest, where X/Y read ~1
  // mV): each cycle averages this many oneshot reads of it, which costs no
  // lag, then lowpasses the result to iron out what is left. 80 ms is short
  // enough that the chair does not feel late to turn.
  static constexpr int kTwistOversample = 8;
  static espp::SimpleLowpassFilter twist_lowpass({.time_constant = 0.08f});
  auto adc_task_fn = [&adc, &channels](std::mutex &m, std::condition_variable &cv) {
    // see the AXIS WIRING note at the calibrations: CH1 is horizontal, CH0 is
    // vertical
    auto vert_mv = adc.get_mv(channels[0]);  // ADC1_CH0 (GPIO16)
    auto horiz_mv = adc.get_mv(channels[1]); // ADC1_CH1 (GPIO17)
    // twist pot on ADC2 (GPIO52), sampled oneshot — see comment at the
    // channel definitions above — and averaged (kTwistOversample)
    std::optional<float> twist_mv;
    {
      float sum = 0.0f;
      int reads = 0;
      for (int i = 0; i < kTwistOversample; ++i) {
        if (auto mv = twist_adc.read_mv(twist_channel)) {
          sum += *mv;
          ++reads;
        }
      }
      if (reads > 0) {
        twist_mv = sum / static_cast<float>(reads);
      }
    }

    // raw mV -> calibrated stick -> the keypad key, the bars and XYTwist:
    // hmi::stick::StickPipeline (components/stick), fed through AdcStickIo
    // (frag_stick_config.inc) in the order this ran inline before. Only a
    // cycle with all three reads does anything; otherwise nothing is published.
    AdcStickIo stick_io{.twist_lowpass = twist_lowpass};
    const bool adc_published = stick.cycle(
        stick_io, {.horizontal_mv = horiz_mv, .vertical_mv = vert_mv, .twist_mv = twist_mv});
    // Every cycle, valid or not: the self test measures the loop's cadence and
    // how often a read fails, as well as the values. A no-op unless a run is
    // capturing. X is the horizontal channel, as everywhere above.
    selftest_note_adc(vert_mv && horiz_mv && twist_mv, horiz_mv.value_or(0.0f),
                      vert_mv.value_or(0.0f), twist_mv.value_or(0.0f), adc_published,
                      joy_button_pressed.load());

    // NOTE: sleeping in this way allows the sleep to exit early when the
    // task is being stopped / destroyed
    {
      std::unique_lock<std::mutex> lk(m);
      cv.wait_for(lk, kAdcUpdatePeriod);
    }
    // don't want to stop the task
    return false;
  };
  auto adc_task = espp::Task({.callback = adc_task_fn,
                              .task_config = {.name = "Read ADC"},
                              .log_level = espp::Logger::Verbosity::INFO});
  adc_task.start();

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
  rtps_comms_on_mib_status([](const MIB::MibStatus &status) {
    clock_note_mcb_time(status); // no LVGL: sets the system clock and the RTC
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    lv_subject_set_int(&mib_state_subject, static_cast<int32_t>(status.systemState));
    // What the MIB is actually driving with: the three profile buttons highlight from
    // this, so they follow the chair even when something else changed it.
    lv_subject_set_int(&drive_profile_subject, static_cast<int32_t>(status.activeProfile));
    // m/s on the wire, mph on the dial: the shared spec carries the real
    // quantity and the unit on the label is ours to pick.
    lv_subject_set_int(&speed_tenths_subject, speed_display_tenths(status.speed));
    // copy_string cuts each text to its subject's buffer (RAMMP_*_LEN). drive_text_subject
    // stays empty: the MIB sends one wording, and the state label is where it belongs.
    lv_subject_copy_string(&state_text_subject, status.status_text.c_str());
    lv_subject_copy_string(&error_text_subject, status.error_message.c_str());
    lv_subject_copy_string(&error_footer_subject, status.error_footer.c_str());
    seat_apply_state(status.currentSeatState);
  });
  rtps_comms_on_diagnostics([](const rammp::Diagnostics &diag) {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    for (size_t i = 0; i < std::min<size_t>(diag.items.size(), rammp::kDiagCount); i++) {
      const auto &values = diag.items[i].values;
      for (size_t f = 0; f < std::min<size_t>(values.size(), rammp::kDiagFields); f++) {
        lv_subject_set_int(&diag_value[i][f], values[f]);
      }
    }
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
  remote_ui_start({
      .lvgl_mutex = &lvgl_mutex,
      .set_key =
          [](uint32_t key) {
            remote_key.store(key);
            joy_key.store(key);
          },
      .press_select = [] { select_key.store(true); },
      .set_button = [](bool down) { stick_button_edge(down); },
      .screen_name = [] { return active_screen_name(); },
  });

  // loop forever
  while (true) {
    std::this_thread::sleep_for(1s);
  }
  //! [m5stack tab5 example]
}

#include "frag_audio.inc" // split_main.py
// --
