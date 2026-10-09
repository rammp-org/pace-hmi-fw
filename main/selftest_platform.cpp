/// @file selftest_platform.cpp
/// @brief The board probes the self test reads. See selftest_platform.hpp.

#include "selftest_platform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <optional>
#include <string>

#include "feedback/da7280_bench.hpp"
#include "joystick_cal.hpp"
#include "m5stack-tab5.hpp"

SelfTestPlatform selftest_platform(hmi::feedback::Feedback *feedback,
                                   const std::vector<uint8_t> &found_addresses,
                                   bool direct_render) {
  return {
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
      .drv2605_status = [feedback] { return feedback->haptics().status(); },
      .drv2605_play =
          [feedback](std::string &detail) { return feedback->haptics().play_click(detail); },
      .da7280_found = std::find(found_addresses.begin(), found_addresses.end(),
                                hmi::feedback::kDa7280Address) != found_addresses.end(),
      .direct_render = direct_render,
      .joystick_cal_centers_mv = []() -> std::optional<std::array<float, 3>> {
        if (!joystick_cal_saved()) {
          return std::nullopt;
        }
        const JoystickCal cal = joystick_cal_current();
        return std::array<float, 3>{cal[JOY_HORIZONTAL].center_mv, cal[JOY_VERTICAL].center_mv,
                                    cal[JOY_TWIST].center_mv};
      },
      .motion_guard = {}, // app_main's, from the UiApp's motion guard channels
  };
}
