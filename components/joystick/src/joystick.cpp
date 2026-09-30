#include "joystick.hpp"

#include <cassert>
#include <cmath>

using namespace espp;

espp::Joystick::Joystick(const espp::Joystick::Config &config)
    : BaseComponent("Joystick", config.log_level)
    , x_mapper_(config.x_calibration)
    , y_mapper_(config.y_calibration)
    , type_(config.type)
    , center_deadzone_radius_(config.center_deadzone_radius)
    , range_deadzone_(config.range_deadzone)
    , get_values_(config.get_values)
    , get_values_3d_(config.get_values_3d)
    , has_z_(config.z_calibration.has_value()) {
  if (has_z_) {
    z_mapper_.configure(config.z_calibration.value());
  }
}

void espp::Joystick::set_type(espp::Joystick::Type type, float radius, float range_deadzone) {
  type_ = type;
  if (type_ == Type::CIRCULAR) {
    x_mapper_.set_center_deadband(0);
    y_mapper_.set_center_deadband(0);
    x_mapper_.set_range_deadband(0);
    y_mapper_.set_range_deadband(0);
  }
  // NOTE: z_mapper_ is intentionally left alone - the circular deadzone is a
  // property of the x/y gimbal, not of the independent third axis.
  set_center_deadzone_radius(radius);
  set_range_deadzone(range_deadzone);
}

espp::Joystick::Type espp::Joystick::type() const { return type_; }

void espp::Joystick::set_center_deadzone_radius(float radius) {
  center_deadzone_radius_ = std::clamp<float>(radius, 0, 1);
}

float espp::Joystick::center_deadzone_radius() const { return center_deadzone_radius_; }

void espp::Joystick::set_range_deadzone(float range_deadzone) {
  range_deadzone_ = std::clamp<float>(range_deadzone, 0, 1);
}

float espp::Joystick::range_deadzone() const { return range_deadzone_; }

void espp::Joystick::set_calibration(const espp::FloatRangeMapper::Config &x_calibration,
                                     const espp::FloatRangeMapper::Config &y_calibration,
                                     float center_deadzone_radius, float range_deadzone) {
  x_mapper_.configure(x_calibration);
  y_mapper_.configure(y_calibration);
  if (type_ == Type::CIRCULAR) {
    x_mapper_.set_center_deadband(0);
    y_mapper_.set_center_deadband(0);
    x_mapper_.set_range_deadband(0);
    y_mapper_.set_range_deadband(0);
  }
  set_center_deadzone_radius(center_deadzone_radius);
  set_range_deadzone(range_deadzone);
}

void espp::Joystick::set_z_calibration(const espp::FloatRangeMapper::Config &z_calibration) {
  z_mapper_.configure(z_calibration);
  has_z_ = true;
}

bool espp::Joystick::has_z() const { return has_z_; }

void espp::Joystick::update() {
  if (get_values_3d_) {
    float _x, _y, _z;
    logger_.info("Getting x,y,z values");
    bool success = get_values_3d_(&_x, &_y, &_z);
    if (!success) {
      logger_.error("Could not get values!");
      return;
    }
    logger_.debug("Got x,y,z values: ({}, {}, {})", _x, _y, _z);
    recalculate(_x, _y, _z);
    return;
  }
  if (!get_values_) {
    logger_.error("No function provided with which to get values!");
    return;
  }
  float _x, _y;
  logger_.info("Getting x,y values");
  bool success = get_values_(&_x, &_y);
  if (!success) {
    logger_.error("Could not get values!");
    return;
  }
  logger_.debug("Got x,y values: ({}, {})", _x, _y);
  recalculate(_x, _y, std::nullopt);
}

void espp::Joystick::update(float raw_x, float raw_y) { recalculate(raw_x, raw_y, std::nullopt); }

void espp::Joystick::update(float raw_x, float raw_y, float raw_z) {
  recalculate(raw_x, raw_y, raw_z);
}

float espp::Joystick::x() const { return position_.x(); }

float espp::Joystick::y() const { return position_.y(); }

float espp::Joystick::z() const { return z_; }

const espp::Vector2f &espp::Joystick::position() const { return position_; }

const espp::Vector2f &espp::Joystick::raw() const { return raw_; }

float espp::Joystick::raw_z() const { return raw_z_; }

void espp::Joystick::recalculate(float raw_x, float raw_y, std::optional<float> raw_z) {
  raw_.x(raw_x);
  raw_.y(raw_y);
  position_.x(x_mapper_.map(raw_x));
  position_.y(y_mapper_.map(raw_y));
  if (type_ == Type::CIRCULAR) {
    auto magnitude = position_.magnitude();
    if (magnitude < center_deadzone_radius_) {
      position_.x(0);
      position_.y(0);
    } else if (magnitude >= 1.0f - range_deadzone_) {
      position_ = position_.normalized();
    } else {
      const float magnitude_range = 1.0f - center_deadzone_radius_ - range_deadzone_;
      const float new_magnitude = (magnitude - center_deadzone_radius_) / magnitude_range;
      position_ = position_.normalized() * new_magnitude;
    }
  }
  // the third axis is mapped independently: its deadbands come from its own
  // range mapper and it is never touched by the x/y circular clamping above
  if (has_z_ && raw_z.has_value()) {
    raw_z_ = raw_z.value();
    z_ = z_mapper_.map(raw_z_);
  }
}

bool espp::joystick_selftest() {
  static constexpr float kEps = 1e-3f;
  auto close = [](float a, float b) { return std::abs(a - b) < kEps; };
  (void)close; // asserts compile out under NDEBUG

  // a 0-3300 mV pot centered at 1650 mV, no deadbands
  const espp::FloatRangeMapper::Config linear{
      .center = 1650.0f, .center_deadband = 0.0f, .minimum = 0.0f, .maximum = 3300.0f};
  // same pot, but with center and range deadbands
  const espp::FloatRangeMapper::Config deadbanded{.center = 1650.0f,
                                                  .center_deadband = 60.0f,
                                                  .minimum = 0.0f,
                                                  .maximum = 3300.0f,
                                                  .range_deadband = 40.0f};

  // --- rectangular, 2 axes: the classic behaviour must be unchanged ---
  espp::Joystick rect({.x_calibration = linear,
                       .y_calibration = linear,
                       .type = espp::Joystick::Type::RECTANGULAR});
  assert(!rect.has_z());
  rect.update(1650.0f, 1650.0f);
  assert(close(rect.x(), 0.0f) && close(rect.y(), 0.0f));
  rect.update(3300.0f, 0.0f);
  assert(close(rect.x(), 1.0f) && close(rect.y(), -1.0f));
  rect.update(2475.0f, 1650.0f);
  assert(close(rect.x(), 0.5f));
  // z() is 0 and stays 0 on a joystick with no z axis
  rect.update(2475.0f, 1650.0f, 3300.0f);
  assert(close(rect.z(), 0.0f));

  // --- circular, 3 axes ---
  espp::Joystick stick({.x_calibration = linear,
                        .y_calibration = linear,
                        .z_calibration = deadbanded,
                        .type = espp::Joystick::Type::CIRCULAR,
                        .center_deadzone_radius = 0.1f,
                        .range_deadzone = 0.05f});
  assert(stick.has_z());

  // full diagonal deflection is clamped to the unit circle
  stick.update(3300.0f, 3300.0f, 1650.0f);
  assert(close(stick.position().magnitude(), 1.0f));
  assert(close(stick.x(), stick.y()));

  // inside the circular center deadzone -> exactly zero
  stick.update(1650.0f + 82.5f, 1650.0f, 1650.0f); // x maps to 0.05, radius 0.05 < 0.1
  assert(close(stick.x(), 0.0f) && close(stick.y(), 0.0f));

  // outside the deadzone the magnitude is rescaled across the live band
  stick.update(1650.0f + 330.0f, 1650.0f, 1650.0f); // x maps to 0.2
  assert(close(stick.x(), (0.2f - 0.1f) / (1.0f - 0.1f - 0.05f)));

  // z uses its own deadbands: center -> 0, full -> 1, and the range deadband
  // means it saturates just short of the rail
  assert(close(stick.z(), 0.0f));
  stick.update(1650.0f, 1650.0f, 1700.0f); // within the 60 mV center deadband
  assert(close(stick.z(), 0.0f));
  stick.update(1650.0f, 1650.0f, 3270.0f); // within the 40 mV range deadband
  assert(close(stick.z(), 1.0f));
  stick.update(1650.0f, 1650.0f, 0.0f);
  assert(close(stick.z(), -1.0f));

  // THE point of the 3-axis design: z is independent of the x/y gimbal. Full
  // twist survives the x/y center deadzone, and centered twist survives the
  // x/y magnitude clamp.
  stick.update(1650.0f, 1650.0f, 3300.0f);
  assert(close(stick.x(), 0.0f) && close(stick.y(), 0.0f) && close(stick.z(), 1.0f));
  stick.update(3300.0f, 3300.0f, 1650.0f);
  assert(close(stick.position().magnitude(), 1.0f) && close(stick.z(), 0.0f));

  // the 2-axis update() overload leaves z where it was
  stick.update(1650.0f, 1650.0f, 3300.0f);
  stick.update(3300.0f, 1650.0f);
  assert(close(stick.z(), 1.0f));

  return true;
}
