// hmi::stick: the mapping self check (see mapping_selftest.hpp). Each check is one assert() of
// the fork's espp::joystick_selftest() (components/joystick, espp 1.2.0 + J1/J2), in the same
// order and with the same values; the fork's z axis is the twist's own range mapper here, as
// in StickPipeline. A failure is counted, not asserted: the caller decides (main aborts the
// boot, as the fork's asserts did).

#include "stick/mapping_selftest.hpp"

#include <cmath>

#include "joystick.hpp"

namespace hmi::stick {

namespace {

// the fork's tolerance
constexpr float kEps = 1e-3f;

// a 0-3300 mV pot centred at 1650 mV, no dead bands
constexpr espp::FloatRangeMapper::Config kLinear{
    .center = 1650.0f, .center_deadband = 0.0f, .minimum = 0.0f, .maximum = 3300.0f};
// the same pot with a 60 mV centre dead band and a 40 mV range dead band
constexpr espp::FloatRangeMapper::Config kDeadbanded{.center = 1650.0f,
                                                     .center_deadband = 60.0f,
                                                     .minimum = 0.0f,
                                                     .maximum = 3300.0f,
                                                     .range_deadband = 40.0f};

bool close(float a, float b) { return std::abs(a - b) < kEps; }

// The fork's "circular, 3 axes" joystick: the X/Y gimbal plus the twist mapped on its own.
struct Stick3 {
  espp::Joystick xy{{.x_calibration = kLinear,
                     .y_calibration = kLinear,
                     .type = espp::Joystick::Type::CIRCULAR,
                     .center_deadzone_radius = 0.1f,
                     .range_deadzone = 0.05f}};
  espp::FloatRangeMapper twist_mapper{kDeadbanded};
  float twist = 0.0f;

  void update(float x, float y, float raw_twist) {
    xy.update(x, y);
    twist = twist_mapper.map(raw_twist);
  }
};

// rectangular, 2 axes: the classic behaviour (the fork's first 5 checks; its "z stays 0 without
// a z axis" has no counterpart: a 2-axis joystick has no z, the twist is mapped elsewhere)
int rectangular_failures() {
  espp::Joystick rect({.x_calibration = kLinear,
                       .y_calibration = kLinear,
                       .type = espp::Joystick::Type::RECTANGULAR});
  int failures = 0;
  rect.update(1650.0f, 1650.0f);
  failures += (close(rect.x(), 0.0f) && close(rect.y(), 0.0f)) ? 0 : 1;
  rect.update(3300.0f, 0.0f);
  failures += (close(rect.x(), 1.0f) && close(rect.y(), -1.0f)) ? 0 : 1;
  rect.update(2475.0f, 1650.0f);
  failures += close(rect.x(), 0.5f) ? 0 : 1;
  return failures;
}

// circular X/Y: the clamp, the centre dead zone and the rescale outside it
int circular_failures(Stick3 &stick) {
  int failures = 0;
  // full diagonal deflection is clamped to the unit circle
  stick.update(3300.0f, 3300.0f, 1650.0f);
  failures += close(stick.xy.position().magnitude(), 1.0f) ? 0 : 1;
  failures += close(stick.xy.x(), stick.xy.y()) ? 0 : 1;
  // inside the circular centre dead zone: exactly zero (x maps to 0.05 < 0.1)
  stick.update(1650.0f + 82.5f, 1650.0f, 1650.0f);
  failures += (close(stick.xy.x(), 0.0f) && close(stick.xy.y(), 0.0f)) ? 0 : 1;
  // outside it the magnitude is rescaled across the live band (x maps to 0.2)
  stick.update(1650.0f + 330.0f, 1650.0f, 1650.0f);
  failures += close(stick.xy.x(), (0.2f - 0.1f) / (1.0f - 0.1f - 0.05f)) ? 0 : 1;
  return failures;
}

// the twist's own dead bands: centre 0, full 1, saturating just short of the rail
int twist_failures(Stick3 &stick) {
  int failures = close(stick.twist, 0.0f) ? 0 : 1;
  stick.update(1650.0f, 1650.0f, 1700.0f); // within the 60 mV centre dead band
  failures += close(stick.twist, 0.0f) ? 0 : 1;
  stick.update(1650.0f, 1650.0f, 3270.0f); // within the 40 mV range dead band
  failures += close(stick.twist, 1.0f) ? 0 : 1;
  stick.update(1650.0f, 1650.0f, 0.0f);
  failures += close(stick.twist, -1.0f) ? 0 : 1;
  return failures;
}

// the twist is independent of the X/Y gimbal: full twist survives the centre dead zone, a
// centred twist survives the magnitude clamp, and an X/Y-only update leaves the twist alone
int independence_failures(Stick3 &stick) {
  stick.update(1650.0f, 1650.0f, 3300.0f);
  int failures =
      (close(stick.xy.x(), 0.0f) && close(stick.xy.y(), 0.0f) && close(stick.twist, 1.0f)) ? 0 : 1;
  stick.update(3300.0f, 3300.0f, 1650.0f);
  failures += (close(stick.xy.position().magnitude(), 1.0f) && close(stick.twist, 0.0f)) ? 0 : 1;
  stick.update(1650.0f, 1650.0f, 3300.0f);
  stick.xy.update(3300.0f, 1650.0f);
  failures += close(stick.twist, 1.0f) ? 0 : 1;
  return failures;
}

} // namespace

bool mapping_selftest() {
  Stick3 stick;
  int failures = rectangular_failures();
  failures += circular_failures(stick);
  failures += twist_failures(stick);
  failures += independence_failures(stick);
  return failures == 0;
}

} // namespace hmi::stick
