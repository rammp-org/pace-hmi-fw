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
#include <optional>
#include <stdlib.h>
#include <vector>

#include "m5stack-tab5.hpp"

#include "da7280.hpp"

#include "kalman_filter.hpp"
#include "madgwick_filter.hpp"

#include "ui.h"

#include "sample_ui_home.h"

#include "continuous_adc.hpp"
#include "oneshot_adc.hpp"

#include "rtps_comms.hpp"

#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_timer.h"

using namespace std::chrono_literals;

static std::vector<uint8_t> audio_bytes;

static std::recursive_mutex lvgl_mutex;

// ---------------------------------------------------------------------------
// Frame-rate instrumentation (temporary; remove once tuning is finished).
//
// Reports over serial rather than the LVGL perf overlay, so throughput can be
// measured without eyes on the panel. RENDER_START/RENDER_READY fire only when
// LVGL actually rasterizes, so these numbers are real frame cost -- REFR_*
// would tick even on an idle screen and read as a meaningless "infinite fps".
//
// kFpsStress forces a full-screen invalidation every LVGL cycle. Without it a
// static SquareLine screen invalidates nothing and renders nothing, which
// measures the redraw path not at all. With it we get sustained worst case.
// ---------------------------------------------------------------------------
static constexpr bool kFpsInstrument = false;
static constexpr bool kFpsStress = false;
static constexpr bool kFpsHideImages = false;
static std::atomic<uint32_t> fps_frames{0};
static std::atomic<uint64_t> fps_render_us_total{0};
static std::atomic<uint32_t> fps_render_us_max{0};
static int64_t fps_render_start_us = 0;

static void fps_render_start_cb(lv_event_t *) { fps_render_start_us = esp_timer_get_time(); }

static void fps_render_ready_cb(lv_event_t *) {
  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - fps_render_start_us);
  fps_frames.fetch_add(1, std::memory_order_relaxed);
  fps_render_us_total.fetch_add(us, std::memory_order_relaxed);
  uint32_t prev = fps_render_us_max.load(std::memory_order_relaxed);
  while (us > prev && !fps_render_us_max.compare_exchange_weak(prev, us)) {
  }
}

// Subjects (observer pattern) feeding the Settings-screen axis bars; static
// lifetime because the bound observers keep pointers to them
static lv_subject_t adc_x_subject;
static lv_subject_t adc_y_subject;
static lv_subject_t adc_twist_subject;

static bool load_audio(size_t &out_size, size_t &out_sample_rate);
static void play_click(espp::M5StackTab5 &tab5);

// DA7280 haptic driver bring-up test (raw register read, no driver class yet)
static void test_da7280(espp::Logger &logger, espp::I2c &i2c,
                        const std::vector<uint8_t> &found_addresses);

// DA7280 driver functional test: exercises vibrate()/stop(), the
// acceleration/rapid-stop/frequency-tracking toggles, and a fault-status
// register peek, all with the Da7280 driver class (DRO mode).
static void test_da7280_functional(espp::Logger &logger, espp::I2c &i2c);

// Direct-mode flush. LVGL renders into the DPI panel's own frame buffers, so
// px_map is already a frame buffer and esp_lcd_panel_draw_bitmap takes its
// no-copy branch: it writes the cache back, sets cur_fb_index, and returns,
// instead of copying 1.8 MB.
//
// KNOWN DEFECT -- this is NOT vsync-gated. That no-copy branch calls
// on_color_trans_done SYNCHRONOUSLY (esp_lcd_panel_dpi.c), which the BSP wires
// to lv_display_flush_ready, so disp->flushing clears before this function even
// returns and LVGL's wait_for_flushing() becomes a no-op. LVGL then starts
// rasterizing into the buffer the DSI DMA may still be scanning out, so partial
// updates can tear. The real fix is to register on_refresh_done (currently
// nullptr in the BSP) and defer flush_ready until the flip actually lands.
//
// Ignoring `area` and flushing full-screen IS correct here: call_flush_cb passes
// the buffer START (not an offset), and refr_sync_areas already copies the
// previous frame's invalid areas forward, so both buffers stay coherent.
static void direct_flush_cb(lv_display_t *disp, const lv_area_t * /*area*/, uint8_t *px_map) {
  if (!lv_display_flush_is_last(disp)) {
    lv_display_flush_ready(disp);
    return;
  }
  auto &tab5 = espp::M5StackTab5::get();
  tab5.write_lcd_lines(0, 0, static_cast<int>(tab5.display_width()) - 1,
                       static_cast<int>(tab5.display_height()) - 1, px_map, 0);
  // completion arrives via the panel's on_color_trans_done -> flush_ready
}

// DA7280 haptic driver bring-up test: read-only register probe, no driver
// class yet. Datasheet 7-bit slave address is 0x4A (its 0x94/0x95 values are
// the pre-shifted 8-bit write/read forms) — espp::I2c shifts internally, so
// the raw 7-bit address is passed here.
static constexpr uint8_t kDa7280Address = 0x4A;
static constexpr uint8_t kDa7280RegChipRev = 0x00;

static void test_da7280(espp::Logger &logger, espp::I2c &i2c,
                        const std::vector<uint8_t> &found_addresses) {
  bool found = std::find(found_addresses.begin(), found_addresses.end(), kDa7280Address) !=
               found_addresses.end();
  if (!found) {
    logger.warn("DA7280 not found at {:#02x} — check wiring", kDa7280Address);
    return;
  }
  uint8_t chip_rev = 0;
  if (!i2c.read_at_register(kDa7280Address, kDa7280RegChipRev, &chip_rev, 1)) {
    logger.error("DA7280 found at {:#02x} but CHIP_REV read failed", kDa7280Address);
    return;
  }
  logger.info("DA7280 CHIP_REV (reg {:#02x}) = {:#02x}", kDa7280RegChipRev, chip_rev);
}

// DA7280 driver functional test (SparkFun Qwiic Haptic Motor's bundled LRA
// electrical parameters). Exercises the driver's public API end to end:
//   1. Baseline smoke test (init + vibrate + stop)
//   2. Magnitude sweep across the DRO drive range
//   3. Acceleration mode: disabled, at high magnitudes (the "enabled at
//      high magnitude" case is invalid and is proven so at compile time
//      via static_assert, not exercised at runtime)
//   4. Rapid stop: enabled (snap stop) vs disabled (coast)
//   5. Frequency tracking: enabled vs disabled, with a fault-status peek
//   6. Custom Waveform Operation: SINE/SQUARE/TRIANGLE presets, then a raw
//      CUSTOM coefficient triple, then leaving the mode again
//
// Timing is deliberately generous: kDa7280DriveDuration is how long each
// vibration runs (long enough to unambiguously feel), kDa7280PauseDuration
// is silence between changes (long enough that "did it stop?" isn't a
// guess), and kDa7280StepPause gives time to read a step's header log
// before the first action in that step fires. These must be at namespace
// scope (not locals) since they're used as lambda default-argument values
// in test_da7280_functional() below.
static constexpr auto kDa7280DriveDuration = 750ms;
static constexpr auto kDa7280PauseDuration = 1500ms;
static constexpr auto kDa7280StepPause = 750ms;
static constexpr auto kDa7280FreqTrackDriveDuration = 1500ms;
static constexpr int kDa7280TotalTests = 15;

static void test_da7280_functional(espp::Logger &logger, espp::I2c &i2c) {
  std::error_code ec;
  auto da7280_i2c_device = i2c.add_device<uint8_t>(
      {
          .device_address = espp::Da7280::DEFAULT_ADDRESS,
          .timeout_ms = static_cast<int>(i2c.config().timeout_ms),
          .scl_speed_hz = i2c.config().clk_speed,
          .log_level = espp::Logger::Verbosity::WARN,
      },
      ec);
  if (!da7280_i2c_device) {
    logger.error("Could not create DA7280 I2C device: {}", ec.message());
    return;
  }

  espp::Da7280 da7280({
      .device_address = espp::Da7280::DEFAULT_ADDRESS,
      .motor_type = espp::Da7280::MotorType::LRA,
      .nominal_voltage = 2.106f,
      .abs_max_voltage = 2.26f,
      .max_current_ma = 165.4f,
      .impedance_ohms = 13.8f,
      .lra_freq_hz = 170.0f,
      .probe = espp::make_i2c_addressed_probe(da7280_i2c_device),
      .write = espp::make_i2c_addressed_write(da7280_i2c_device),
      .read_register = espp::make_i2c_addressed_read_register(da7280_i2c_device),
      .log_level = espp::Logger::Verbosity::INFO,
  });

  if (!da7280.initialize(ec)) {
    logger.error("DA7280 functional test: initialization failed: {}", ec.message());
    return;
  }

  // Every individual vibration is numbered [DA7280 TEST N/15] so a specific
  // failure (felt nothing, or an error) can be pinned to one line in the log
  // instead of just "somewhere in step 3". Status (IRQ_EVENT1/IRQ_STATUS1)
  // is checked after every test, not just the frequency-tracking ones, so
  // any future zero-output mystery has fault data attached instead of
  // needing a re-run to diagnose.
  // print_step_header draws a heavy divider (step boundary); run_test draws
  // a light divider (test boundary) before its own header line. Both print
  // raw via fmt::print (blank lines + dividers, no logger tag/timestamp
  // prefix) so the boundary is visually obvious when scrolling a busy log.
  auto print_step_header = [&](const char *text) {
    fmt::print("\n\n========================================\n");
    logger.info(text);
  };

  int test_index = 0;
  auto run_test = [&](const char *label, uint8_t magnitude,
                      std::chrono::milliseconds duration = kDa7280DriveDuration) {
    ++test_index;
    fmt::print("\n----------------------------------------\n");
    logger.info("[DA7280 TEST {:02}/{}] {}: vibrate({})", test_index, kDa7280TotalTests, label,
                magnitude);
    da7280.vibrate(magnitude, ec);
    if (ec) {
      logger.error("[DA7280 TEST {:02}/{}] vibrate failed: {}", test_index, kDa7280TotalTests,
                   ec.message());
    }
    std::this_thread::sleep_for(duration);
    da7280.check_faults(ec);
    da7280.stop(ec);
    std::this_thread::sleep_for(kDa7280PauseDuration);
  };

  // 1. Baseline smoke test
  print_step_header("[DA7280 1/6] Baseline");
  std::this_thread::sleep_for(kDa7280StepPause);
  run_test("Baseline", 25);

  // 2. Magnitude sweep (0-127 only: acceleration is enabled by default at
  // this point, and above 127 with acceleration enabled produces no output
  // at all rather than a clamped value - see vibrate()'s doc comment. The
  // 128-255 range is exercised deliberately in step 3, with acceleration
  // disabled.)
  print_step_header("[DA7280 2/6] Magnitude sweep");
  std::this_thread::sleep_for(kDa7280StepPause);
  for (uint8_t magnitude : {32, 64, 96, 127}) {
    run_test("Magnitude sweep", magnitude);
  }

  // 3. Acceleration mode: disabled, at 200/255 (should drive at full
  // strength, current/voltage-limited near 255 - see check_faults()'s
  // WARNING decode). The "enabled + >127" combination is NOT exercised
  // here at runtime: it's proven invalid at COMPILE time instead, by the
  // static_asserts right below using Da7280::is_valid_dro_magnitude(). If
  // magnitude 200/255 were paired with AccelerationEnabled=true in one of
  // those asserts, the build itself would fail with a clear message rather
  // than needing a hardware run + log read to discover the mistake.
  static_assert(espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/false, 200),
                "sanity check: 200 is valid with acceleration disabled");
  static_assert(espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/false, 255),
                "sanity check: 255 is valid with acceleration disabled");
  static_assert(!espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/true, 200),
                "sanity check: 200 must be rejected with acceleration enabled");
  static_assert(!espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/true, 255),
                "sanity check: 255 must be rejected with acceleration enabled");
  print_step_header("[DA7280 3/6] Acceleration mode: disabled, at 200/255");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_acceleration_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  for (uint8_t magnitude : {200, 255}) {
    run_test("Accel disabled", magnitude);
  }
  da7280.set_acceleration_enabled(true, ec); // restore default

  // 4. Rapid stop: enabled should snap to a stop, disabled should coast.
  // Magnitude 100 (not 150): acceleration is still enabled at this point
  // (left that way at the end of step 3), so anything above 127 would
  // produce no output here too.
  print_step_header("[DA7280 4/6] Rapid stop: enabled (snap) then disabled (coast)");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_rapid_stop_enabled(true, ec);
  run_test("Rapid stop enabled", 100);

  da7280.set_rapid_stop_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  run_test("Rapid stop disabled", 100);
  da7280.set_rapid_stop_enabled(true, ec); // restore default

  // 5. Frequency tracking: enabled then disabled. A restrained/blocked
  // motor can trip a spurious fault with tracking enabled; run_test's
  // fault-status log is how to actually see that instead of guessing from
  // silence.
  print_step_header(
      "[DA7280 5/6] Frequency tracking: enabled then disabled, checking fault status");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_frequency_tracking_enabled(true, ec);
  run_test("Freq tracking enabled", 80, kDa7280FreqTrackDriveDuration);

  da7280.set_frequency_tracking_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  run_test("Freq tracking disabled", 80, kDa7280FreqTrackDriveDuration);
  da7280.set_frequency_tracking_enabled(true, ec); // restore default

  // 6. Custom Waveform Operation: SINE/SQUARE/TRIANGLE presets, then a raw
  // CUSTOM coefficient triple. enable_custom_waveform(true, ...) disables
  // acceleration/rapid-stop/amp-PID/frequency-tracking for us (required by
  // the chip for this mode) - we restore them ourselves at the end since
  // the driver won't do that automatically, see enable_custom_waveform()'s
  // doc comment. Magnitude 150 is safe here regardless: acceleration is
  // off for the whole step.
  print_step_header("[DA7280 6/6] Custom Waveform Operation: SINE, SQUARE, TRIANGLE, CUSTOM");
  std::this_thread::sleep_for(kDa7280StepPause);
  if (!da7280.enable_custom_waveform(true, ec)) {
    logger.error("[DA7280 6/6] enable_custom_waveform(true) failed: {}", ec.message());
  }

  da7280.set_wave_shape(espp::Da7280::WaveShape::SINE, ec);
  run_test("Custom waveform SINE", 150);

  da7280.set_wave_shape(espp::Da7280::WaveShape::SQUARE, ec);
  run_test("Custom waveform SQUARE", 150);

  da7280.set_wave_shape(espp::Da7280::WaveShape::TRIANGLE, ec);
  run_test("Custom waveform TRIANGLE", 150);

  // Arbitrary asymmetric shape: stays low, then jumps sharply near the end
  // of the quarter-period - clearly distinct from the three presets above.
  da7280.set_wave_coefficients(0x10, 0x30, 0xF8, ec);
  run_test("Custom waveform CUSTOM", 150);

  if (!da7280.enable_custom_waveform(false, ec)) {
    logger.error("[DA7280 6/6] enable_custom_waveform(false) failed: {}", ec.message());
  }
  // Restore the closed-loop defaults enable_custom_waveform(true) turned off
  da7280.set_acceleration_enabled(true, ec);
  da7280.set_rapid_stop_enabled(true, ec);
  da7280.set_frequency_tracking_enabled(true, ec);
  da7280.set_amp_pid_enabled(false, ec);

  logger.info("DA7280 functional test complete");
}

extern "C" void app_main(void) {
  espp::Logger logger({.tag = "M5Stack Tab5 Example", .level = espp::Logger::Verbosity::INFO});
  logger.info("Starting example!");

  //! [m5stack tab5 example]
  espp::M5StackTab5 &tab5 = espp::M5StackTab5::get();
  logger.info("Running on M5Stack Tab5");

  // first let's get the internal i2c bus and probe for all devices on the bus
  logger.info("Probing internal I2C bus...");
  auto &i2c = tab5.internal_i2c();
  std::vector<uint8_t> found_addresses;
  for (uint8_t address = 1; address < 128; address++) {
    if (i2c.probe_device(address)) {
      found_addresses.push_back(address);
    }
  }
  logger.info("Found devices at addresses: {::#02x}", found_addresses);

  // DA7280 haptic driver bring-up test (raw register read, no driver yet)
  test_da7280(logger, i2c, found_addresses);

  // DA7280 driver functional test (Da7280 driver class, DRO mode)
  test_da7280_functional(logger, i2c);

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
  auto controller_type = tab5.get_display_controller();
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
  // vsync flip rather than a copy. DIRECT requires rotation 0, which is what
  // this panel runs at.
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
      logger.info("LVGL rendering directly into the DSI frame buffers (DIRECT mode)");
    } else {
      logger.error("Could not get DPI frame buffers ({}); leaving BSP flush in place",
                   esp_err_to_name(fb_err));
    }
  }

  // run the LVGL refresh timer at 60 fps — the espp lv_conf.h compiles in a
  // 33 ms (30 fps) default period; the lv_task loop below already calls
  // lv_task_handler every 16 ms so it can keep up
  lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()), 16);

  if (kFpsInstrument) {
    lv_display_add_event_cb(lv_display_get_default(), fps_render_start_cb, LV_EVENT_RENDER_START,
                            nullptr);
    lv_display_add_event_cb(lv_display_get_default(), fps_render_ready_cb, LV_EVENT_RENDER_READY,
                            nullptr);
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

  // only set the time if the year is before 2024
  if (current_time.tm_year < 124) {
    // set the RTC time to a known value (2024-01-15 14:30:45)
    // Set time using std::tm
    std::tm time = {};
    time.tm_year = 124; // 2024 - 1900
    time.tm_mon = 0;    // January (0-based)
    time.tm_mday = 15;  // 15th
    time.tm_hour = 14;  // 2 PM
    time.tm_min = 30;
    time.tm_sec = 45;
    time.tm_wday = 1; // Monday
    if (!tab5.set_rtc_time(time)) {
      logger.error("Failed to set RTC time");
      return;
    }
  } else {
    logger.info("RTC time is already set to a valid value {:%Y-%m-%d %H:%M:%S}", current_time);
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
      // Cycle through brightness levels: 25%, 50%, 75%, 100%
      static int brightness_level = 0;
      float brightness_values[] = {0.25f, 0.5f, 0.75f, 1.0f};
      brightness_level = (brightness_level + 1) % 4;
      float new_brightness = brightness_values[brightness_level];
      tab5.brightness(new_brightness);
      logger.info("Set brightness to {:.0f}%", new_brightness * 100);
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

  // Load the SquareLine Studio UI. This creates ui_Screen1 and makes it the
  // active screen; the demo widgets above stay on the (now hidden) default
  // screen. To go back to the demo screen at runtime, keep a pointer to it
  // (lv_screen_active() before this call) and lv_screen_load() it again.
  logger.info("Loading SquareLine UI...");
  // The Tab5 panel is natively 720x1280 portrait; rotate LVGL 270 degrees so
  // the UI is 1280x720 landscape (use ROTATION_90 for the other direction).
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_0);
  ui_init();

  // Benchmark against the real flex UI (PNG assets only) rather than the boot
  // screen, whose SVG logo otherwise dominates every measurement. Temporary,
  // paired with kFpsInstrument.
  if (kFpsInstrument) {
    lv_screen_load(ui_MainScreenFlex);
    // Experiment: hide the two scaled RGB565A8 images. lv_image_set_scale(150)
    // sets transformed=true in lv_draw_sw_img.c, which skips the fast
    // RGB565A8 blit and routes through transform_and_recolor per pixel.
    // Hiding them isolates that cost from the rest of the screen.
    if (kFpsHideImages) {
      lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Sample wheelchair dashboard mockup (static, no sensor wiring): builds its
  // own screen and swaps it in over ui_MainScreen. Comment out to see the
  // real telemetry UI again; RTPS/ADC binding below is unaffected either way.
  // sample_ui_home_init();
  // lv_screen_load(sample_ui_home_screen);

  // Bind the Settings-screen axis bars to the ADC subjects (observer pattern).
  // Bars show raw millivolts; 3300 ≈ full scale at 12 dB attenuation.
  lv_subject_init_int(&adc_x_subject, 0);
  lv_subject_init_int(&adc_y_subject, 0);
  lv_subject_init_int(&adc_twist_subject, 0);
  lv_bar_set_range(ui_XBar, 0, 3300);
  lv_bar_set_range(ui_YBar, 0, 3300);
  lv_bar_set_range(ui_TwistBar, 0, 3300);
  lv_bar_bind_value(ui_XBar, &adc_x_subject);
  lv_bar_bind_value(ui_YBar, &adc_y_subject);
  lv_bar_bind_value(ui_TwistBar, &adc_twist_subject);

  logger.info("Initializing touch...");
  if (!tab5.initialize_touch(touch_callback)) {
    logger.error("Failed to initialize touch!");
    return;
  }

  // start a simple thread to do the lv_task_handler every 8ms — the refresh
  // timer runs at 16ms (60 fps), polling at twice that rate keeps its firing
  // jitter well under a frame
  logger.info("Starting LVGL task...");
  espp::Task lv_task(
      {.callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
         auto start_time = std::chrono::high_resolution_clock::now();
         {
           std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
           if (kFpsStress) {
             lv_obj_invalidate(lv_screen_active());
           }
           lv_task_handler();
         }
         if (kFpsInstrument) {
           static int64_t last_report_us = 0;
           const int64_t now_us = esp_timer_get_time();
           if (last_report_us == 0)
             last_report_us = now_us;
           if (now_us - last_report_us >= 1000000) {
             const uint32_t frames = fps_frames.exchange(0);
             const uint64_t total_us = fps_render_us_total.exchange(0);
             const uint32_t max_us = fps_render_us_max.exchange(0);
             const float secs = (now_us - last_report_us) / 1e6f;
             last_report_us = now_us;
             fmt::print("[FPS] {:.1f} fps | render avg {:.2f} ms | max {:.2f} ms\n", frames / secs,
                        frames ? (total_us / 1000.0f) / frames : 0.0f, max_us / 1000.0f);
           }
         }
         std::unique_lock<std::mutex> lock(m);
         // Always yield at least one tick: once a render cycle exceeds 8 ms
         // the deadline is already past and wait_until returns without
         // yielding, which pins core 1 at priority 20 and starves IDLE1.
         const auto deadline =
             std::max(start_time + 8ms, std::chrono::high_resolution_clock::now() + 1ms);
         cv.wait_until(lock, deadline, []() { return false; });
         return false;
       },
       .task_config = {
           .name = "lv_task",
           .stack_size_bytes = 32 * 1024,
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

  // set the brightness to 75%
  tab5.brightness(75.0f);

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
  // customization knobs: sampling/LVGL/RTPS cadence, and how often the serial
  // line is printed. The log is divided down because 30 lines/s is the
  // console-flood pattern that starved LVGL once before.
  static constexpr auto kAdcUpdatePeriod = 33ms; // 30 Hz: ADC read, LVGL bars, RTPS publish
  static constexpr int kAdcLogDivider = 6;       // serial log every Nth cycle (~5 Hz)
  auto adc_task_fn = [&adc, &channels](std::mutex &m, std::condition_variable &cv) {
    static uint32_t cycle = 0;
    const bool log_this_cycle = (cycle++ % kAdcLogDivider) == 0;

    std::optional<float> x_mv;
    std::optional<float> y_mv;
    for (auto &conf : channels) {
      auto maybe_mv = adc.get_mv(conf);
      if (conf.channel == ADC_CHANNEL_0) {
        x_mv = maybe_mv;
      } else if (conf.channel == ADC_CHANNEL_1) {
        y_mv = maybe_mv;
      }
      if (maybe_mv.has_value()) {
        lv_subject_t *subject = conf.channel == ADC_CHANNEL_0   ? &adc_x_subject
                                : conf.channel == ADC_CHANNEL_1 ? &adc_y_subject
                                                                : nullptr;
        if (subject) {
          // lv_subject_set_int runs the bar's observer callback synchronously,
          // which touches the widget, so it needs the LVGL lock
          std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
          lv_subject_set_int(subject, static_cast<int32_t>(maybe_mv.value()));
        }
      }
    }
    // twist pot on ADC2 (GPIO52), sampled oneshot — see comment at the
    // channel definitions above
    auto twist_mv = twist_adc.read_mv(twist_channel);
    if (twist_mv.has_value()) {
      std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
      lv_subject_set_int(&adc_twist_subject, static_cast<int32_t>(twist_mv.value()));
    }

    // stream the snapshot to the PC (rtps_adc_plot.py); quiet no-op until
    // RTPS is up and a subscriber is discovered
    if (x_mv && y_mv && twist_mv) {
      auto to_mv = [](float v) { return static_cast<uint32_t>(std::max(v, 0.0f)); };
      rtps_comms_publish_adc(to_mv(*x_mv), to_mv(*y_mv), to_mv(*twist_mv));
    }

    if (log_this_cycle) {
      auto fmt_mv = [](const std::optional<float> &v) {
        return v ? fmt::format("{} mV", static_cast<int>(*v)) : std::string("no value");
      };
      // monotonically increasing tag: if the serial log ever goes quiet and
      // later resumes with a gap in this counter, the task kept running and the
      // console transport dropped the lines; a continuous sequence would mean
      // the task itself had paused
      static uint32_t print_seq = 0;
      // fmt::print("#{} ADC1_CH0: V = {}\tADC1_CH1: V = {}\tADC2_CH0: V = {}\n", print_seq++,
      //            fmt_mv(x_mv), fmt_mv(y_mv), fmt_mv(twist_mv));
    }
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
      [](float percent) { espp::M5StackTab5::get().brightness(std::max(percent, 5.0f)); });
  if (!rtps_comms_start()) {
    logger.warn("RTPS comms not started (Ethernet bring-up failed)");
  }

  // loop forever
  while (true) {
    std::this_thread::sleep_for(1s);
  }
  //! [m5stack tab5 example]
}

static bool load_audio(size_t &out_size, size_t &out_sample_rate) {
  // if the audio_bytes vector is already populated, return the size
  if (audio_bytes.size() > 0) {
    return true;
  }

  // load the audio data. these are configured in the CMakeLists.txt file

  extern const uint8_t click_wav_start[] asm("_binary_click_wav_start");
  extern const uint8_t click_wav_end[] asm("_binary_click_wav_end");
  audio_bytes = std::vector<uint8_t>(click_wav_start, click_wav_end);
  // ensure we have at least a wav header
  if (audio_bytes.size() < 44) {
    audio_bytes.clear();
    return false;
  }
  // get the sample rate from the wav header (bytes 24-27)
  uint32_t sample_rate = *(reinterpret_cast<const uint32_t *>(&audio_bytes[24]));
  // set the audio sample rate accordingly
  // decode the wav file header (first 44 bytes) and remove it
  if (audio_bytes.size() > 44) {
    audio_bytes.erase(audio_bytes.begin(), audio_bytes.begin() + 44);
  }
  out_size = audio_bytes.size();
  out_sample_rate = sample_rate;
  return true;
}

static void play_click(espp::M5StackTab5 &tab5) {
  if (audio_bytes.size() > 0) {
    tab5.play_audio(audio_bytes);
  }
}
