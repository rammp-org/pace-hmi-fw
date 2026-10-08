#pragma once
// The housekeeping island (app-main-shrink §3). Moved from main.cpp's app_main.

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "kalman_filter.hpp"
#include "m5stack-tab5.hpp"
#include "task.hpp"

namespace hmi::housekeeping {

/// @brief The housekeeping island: the IMU, the battery monitor and the RTC, read every
/// `period` on one task, and the Kalman filter that turns the IMU's readings into an
/// orientation. The self test reads what it leaves behind (the IMU's accelerometer, the
/// battery status) without bus traffic of its own. Built once, an app_main local
/// (app-main-shrink V6); it must outlive the IMU's use of orientation_filter().
class Housekeeping {
public:
  using Imu = espp::M5StackTab5::Imu;

  struct Config {
    /// The task, as a literal copy of today's (name, stack, priority, core).
    espp::Task::BaseConfig task;
    /// The wait before each read.
    std::chrono::milliseconds period;
    /// The Kalman orientation filter's measurement noise (the accelerometer's angles).
    float angle_noise;
    /// The Kalman orientation filter's process noise (the gyro's rates).
    float rate_noise;
  };

  /// @brief Sets the filter up; touches no hardware and starts nothing.
  /// @param config See Config.
  explicit Housekeeping(const Config &config);
  Housekeeping(const Housekeeping &) = delete;
  Housekeeping &operator=(const Housekeeping &) = delete;
  Housekeeping(Housekeeping &&) = delete;
  Housekeeping &operator=(Housekeeping &&) = delete;
  ~Housekeeping() = default;

  /// @brief The IMU's orientation filter, for M5StackTab5::initialize_imu. It runs inside
  /// Imu::update, which only this island's task calls.
  /// @return A filter bound to this object.
  [[nodiscard]] Imu::filter_fn orientation_filter();

  /// @brief Starts the task (app_main, once the IMU, the battery monitor and the RTC are up).
  /// @return Whether the task started.
  bool start();

private:
  Imu::Value filter(float dt, const Imu::Value &accel, const Imu::Value &gyro);
  void read();

  espp::M5StackTab5 &tab5_;
  std::chrono::milliseconds period_;
  espp::KalmanFilter<2> kf_;
  std::shared_ptr<Imu> imu_;
  std::optional<int64_t> t0_us_;
  espp::Task task_;
};

} // namespace hmi::housekeeping
