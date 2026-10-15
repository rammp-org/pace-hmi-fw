#pragma once
// The espp fork's 3-axis Joystick (components/joystick, espp 1.2.0 plus the fork's change J1), as
// the legacy harness uses it, rebuilt on espp's 2-axis Joystick: X/Y through espp's Joystick,
// z through its own range mapper, mapped after X/Y and never touched by them (the fork's
// recalculate()). Only what legacy_adc_cycle.cpp calls. Test code: the fork itself is gone.

#include <optional>

#include "joystick.hpp"

namespace espp_fork {

class Joystick {
public:
  using Type = espp::Joystick::Type;

  struct Config {
    espp::FloatRangeMapper::Config x_calibration;
    espp::FloatRangeMapper::Config y_calibration;
    std::optional<espp::FloatRangeMapper::Config> z_calibration{std::nullopt};
    Type type{Type::RECTANGULAR};
    float center_deadzone_radius{0};
    float range_deadzone{0};
    espp::Logger::Verbosity log_level{espp::Logger::Verbosity::WARN};
  };

  explicit Joystick(const Config &config)
      : xy_({.x_calibration = config.x_calibration,
             .y_calibration = config.y_calibration,
             .type = config.type,
             .center_deadzone_radius = config.center_deadzone_radius,
             .range_deadzone = config.range_deadzone,
             .log_level = config.log_level})
      , has_z_(config.z_calibration.has_value()) {
    if (has_z_) {
      z_mapper_.configure(config.z_calibration.value());
    }
  }

  void set_calibration(const espp::FloatRangeMapper::Config &x_calibration,
                       const espp::FloatRangeMapper::Config &y_calibration,
                       float center_deadzone_radius, float range_deadzone) {
    xy_.set_calibration(x_calibration, y_calibration, center_deadzone_radius, range_deadzone);
  }

  void set_z_calibration(const espp::FloatRangeMapper::Config &z_calibration) {
    z_mapper_.configure(z_calibration);
    has_z_ = true;
  }

  void update(float raw_x, float raw_y, float raw_z) {
    xy_.update(raw_x, raw_y);
    if (has_z_) {
      z_ = z_mapper_.map(raw_z);
    }
  }

  [[nodiscard]] float x() const { return xy_.x(); }
  [[nodiscard]] float y() const { return xy_.y(); }
  [[nodiscard]] float z() const { return z_; }

private:
  espp::Joystick xy_;
  espp::FloatRangeMapper z_mapper_{};
  bool has_z_{false};
  float z_{0};
};

} // namespace espp_fork
