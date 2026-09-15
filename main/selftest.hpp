#pragma once

/**
 * @file selftest.hpp
 * @brief The HMI self test: runner, on-screen overlay and RTPS report.
 *
 * What is checked, and the limits each check is held to, live in
 * selftest_spec.h - that header is the spec. This module only measures.
 *
 * A run starts from the SELF TEST settings row (selftest_request LOCAL) or
 * from a PC over RTPS (see "Self test" in rammp_rtps_spec.h). Either
 * way it runs on its own short-lived task, shows progress and results on an
 * overlay above whatever screen is up, prints the table to the serial log and
 * publishes it on rammp/selftest/report.
 */

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

/// Board-specific probes the runner calls. main.cpp fills these in; one left
/// empty makes its check unmeasurable, which selftest_spec.h turns into a FAIL
/// or a SKIP according to that check's `need`.
struct SelfTestPlatform {
  /// The lock every LVGL call is made under (see CLAUDE.md).
  std::recursive_mutex *lvgl_mutex = nullptr;
  /// Magnitude of the last accelerometer reading, in milli-g.
  std::function<std::optional<int32_t>()> imu_accel_mg;
  /// RTC time in seconds; only differences are used.
  std::function<std::optional<int64_t>()> rtc_seconds;
  /// Battery voltage in mV, or nullopt when no battery is present.
  std::function<std::optional<int32_t>()> battery_mv;
  /// Backlight level, 0..100.
  std::function<int32_t()> backlight_percent;
  /// Does anything ACK at this 7-bit address on the internal I2C bus?
  std::function<bool(uint8_t)> i2c_probe;
  /// Addresses that answered the boot-time scan.
  std::vector<uint8_t> boot_i2c_devices;
  /// Raw DRV2605 STATUS register: DEVICE_ID[7:5] DIAG_RESULT[3] OVER_TEMP[1]
  /// OC_DETECT[0].
  std::function<std::optional<uint8_t>()> drv2605_status;
  /// Plays one click and waits for the DRV2605 to report it finished: true =
  /// it did. `detail` says how long it took, or what went wrong.
  std::function<std::optional<bool>(std::string &detail)> drv2605_play;
  bool da7280_found = false;
  /// LVGL is rendering straight into the DSI frame buffers (DIRECT mode).
  bool direct_render = false;
  /// Rest position in raw mV of each joystick axis (horizontal, vertical,
  /// twist) in the saved calibration; nullopt while the compiled-in defaults
  /// are in use because nothing is saved.
  std::function<std::optional<std::array<float, 3>>()> joystick_cal_centers_mv;
};

enum class SelfTestTrigger {
  LOCAL,  ///< the SELF TEST row; peer-dependent checks SKIP without a peer
  REMOTE, ///< an RTPS command; a peer is known to exist, so they FAIL instead
};

/// Call once from app_main, after ui_init and before the LVGL task starts (it
/// sets up the overlay's subjects and render hooks without taking the LVGL
/// lock) and before rtps_comms_start (it registers the self-test RTPS handlers).
void selftest_init(SelfTestPlatform config);

/// Start a run, from any task. Returns false if one is already running, or if
/// `run_id` repeats the last remote run (a resent command). `run_id` is echoed
/// in the report; LOCAL runs use 0.
bool selftest_request(SelfTestTrigger trigger, uint8_t run_id);

/// Called by the ADC task every cycle, whatever it read. Cheap unless a run is
/// capturing. `published` is whether the RTPS joystick sample went out.
void selftest_note_adc(bool valid, float x_mv, float y_mv, float twist_mv, bool published,
                       bool button_pressed);

/// Whether the overlay is up. While it is, it owns the joystick: callers stop
/// passing keys and gestures to the screens behind it. Safe from any task.
bool selftest_ui_visible();

/// Close the overlay, if the run behind it has finished. LVGL task only.
void selftest_ui_dismiss();
