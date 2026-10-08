#include "housekeeping/housekeeping.hpp"

#include <cmath>
#include <condition_variable>
#include <ctime>
#include <mutex>
#include <system_error>
#include <tuple>

#include "esp_timer.h"

namespace hmi::housekeeping {

Housekeeping::Housekeeping(const Config &config)
    : tab5_(espp::M5StackTab5::get())
    , period_(config.period)
    , task_({.callback = [this](std::mutex &m, std::condition_variable &cv) -> bool {
               // sleep first in case we don't get IMU data and need to exit early
               {
                 std::unique_lock<std::mutex> lock(m);
                 cv.wait_for(lock, period_);
               }
               read();
               return false;
             },
             .task_config = config.task}) {
  kf_.set_process_noise(config.rate_noise);
  kf_.set_measurement_noise(config.angle_noise);
}

Housekeeping::Imu::filter_fn Housekeeping::orientation_filter() {
  return [this](float dt, const Imu::Value &accel, const Imu::Value &gyro) {
    return filter(dt, accel, gyro);
  };
}

bool Housekeeping::start() {
  imu_ = tab5_.imu();
  return task_.start();
}

Housekeeping::Imu::Value Housekeeping::filter(float dt, const Imu::Value &accel,
                                              const Imu::Value &gyro) {
  // Apply Kalman filter
  float accelRoll = static_cast<float>(atan2(accel.y, accel.z));
  float accelPitch =
      static_cast<float>(atan2(-accel.x, sqrt(accel.y * accel.y + accel.z * accel.z)));
  kf_.predict({espp::deg_to_rad(gyro.x), espp::deg_to_rad(gyro.y)}, dt);
  kf_.update({accelRoll, accelPitch});
  float roll, pitch;
  std::tie(roll, pitch) = kf_.get_state();
  // return the computed orientation
  Imu::Value orientation{};
  orientation.roll = roll;
  orientation.pitch = pitch;
  orientation.yaw = 0.0f;
  return orientation;
}

void Housekeeping::read() {
  // The RTC and the battery monitor, read as they were when their values were drawn.
  std::tm rtc_time;
  (void)tab5_.get_rtc_time(rtc_time);
  (void)tab5_.read_battery_status();

  const int64_t now = esp_timer_get_time(); // time in microseconds
  if (!t0_us_) {
    t0_us_ = now;
  }
  float dt = static_cast<float>(now - *t0_us_) / 1'000'000.0f; // convert us to s
  t0_us_ = now;

  // update the imu data
  std::error_code ec;
  (void)imu_->update(dt, ec);
}

} // namespace hmi::housekeeping
