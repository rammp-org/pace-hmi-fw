// L1 tests for components/joystick: espp::Joystick mapping, the circular deadzone and the
// independent z axis. Ported one behaviour per case from the 17 assert() statements of
// espp::joystick_selftest() (components/joystick/src/joystick.cpp), plus a parity case that
// runs that self test itself. Each case builds its own joystick (TS-DET-03).

#include "joystick.hpp"

#include "test_case.hpp"

namespace {

// same tolerance as joystick_selftest()
constexpr float EPS = 1e-3f;

// a 0-3300 mV pot centred at 1650 mV, no deadbands
constexpr espp::FloatRangeMapper::Config LINEAR{
    .center = 1650.0f, .center_deadband = 0.0f, .minimum = 0.0f, .maximum = 3300.0f};
// the same pot with a 60 mV centre deadband and a 40 mV range deadband
constexpr espp::FloatRangeMapper::Config DEADBANDED{.center = 1650.0f,
                                                    .center_deadband = 60.0f,
                                                    .minimum = 0.0f,
                                                    .maximum = 3300.0f,
                                                    .range_deadband = 40.0f};

constexpr float CENTER_DEADZONE = 0.1f;
constexpr float RANGE_DEADZONE = 0.05f;

espp::Joystick make_rect() {
  return espp::Joystick({.x_calibration = LINEAR,
                         .y_calibration = LINEAR,
                         .type = espp::Joystick::Type::RECTANGULAR});
}

espp::Joystick make_stick() {
  return espp::Joystick({.x_calibration = LINEAR,
                         .y_calibration = LINEAR,
                         .z_calibration = DEADBANDED,
                         .type = espp::Joystick::Type::CIRCULAR,
                         .center_deadzone_radius = CENTER_DEADZONE,
                         .range_deadzone = RANGE_DEADZONE});
}

} // namespace

// --- rectangular, 2 axes ---

TEST_CASE("JOY-001 a joystick without a z calibration reports no z axis", "[joystick]") {
  const espp::Joystick rect = make_rect();
  TEST_ASSERT_FALSE(rect.has_z());
}

TEST_CASE("JOY-002 a rectangular joystick at the centre reads zero on both axes", "[joystick]") {
  espp::Joystick rect = make_rect();
  rect.update(1650.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rect.x());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rect.y());
}

TEST_CASE("JOY-003 a rectangular joystick at the rails reads +1 on x and -1 on y", "[joystick]") {
  espp::Joystick rect = make_rect();
  rect.update(3300.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, rect.x());
  TEST_ASSERT_FLOAT_WITHIN(EPS, -1.0f, rect.y());
}

TEST_CASE("JOY-004 a rectangular joystick at half deflection on x reads 0.5", "[joystick]") {
  espp::Joystick rect = make_rect();
  rect.update(2475.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.5f, rect.x());
}

TEST_CASE("JOY-005 a joystick without a z axis keeps z at 0 when given a z reading", "[joystick]") {
  espp::Joystick rect = make_rect();
  rect.update(2475.0f, 1650.0f, 3300.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rect.z());
}

// --- circular, 3 axes ---

TEST_CASE("JOY-006 a joystick with a z calibration reports a z axis", "[joystick]") {
  const espp::Joystick stick = make_stick();
  TEST_ASSERT_TRUE(stick.has_z());
}

TEST_CASE("JOY-007 a circular joystick clamps a full diagonal deflection to the unit circle",
          "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(3300.0f, 3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.position().magnitude());
}

TEST_CASE("JOY-008 a circular joystick at full diagonal deflection reads equal x and y",
          "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(3300.0f, 3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, stick.x(), stick.y());
}

TEST_CASE("JOY-009 a circular joystick inside its centre deadzone reads zero", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f + 82.5f, 1650.0f, 1650.0f); // x maps to 0.05, inside the 0.1 radius
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.x());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.y());
}

TEST_CASE("JOY-010 a circular joystick outside its centre deadzone rescales the magnitude "
          "across the live band",
          "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f + 330.0f, 1650.0f, 1650.0f); // x maps to 0.2
  const float expected = (0.2f - CENTER_DEADZONE) / (1.0f - CENTER_DEADZONE - RANGE_DEADZONE);
  TEST_ASSERT_FLOAT_WITHIN(EPS, expected, stick.x());
}

TEST_CASE("JOY-011 a centred z reading maps to 0", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f + 330.0f, 1650.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.z());
}

TEST_CASE("JOY-012 a z reading inside the z centre deadband maps to 0", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f, 1650.0f, 1700.0f); // 50 mV off centre, deadband 60 mV
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.z());
}

TEST_CASE("JOY-013 a z reading inside the z range deadband saturates at +1", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f, 1650.0f, 3270.0f); // 30 mV short of the rail, deadband 40 mV
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.z());
}

TEST_CASE("JOY-014 a z reading at the low rail maps to -1", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f, 1650.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, -1.0f, stick.z());
}

TEST_CASE("JOY-015 full twist survives the x/y centre deadzone", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f, 1650.0f, 3300.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.x());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.y());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.z());
}

TEST_CASE("JOY-016 a centred twist survives the x/y magnitude clamp", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(3300.0f, 3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.position().magnitude());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.z());
}

TEST_CASE("JOY-017 the 2-axis update leaves z at its last value", "[joystick]") {
  espp::Joystick stick = make_stick();
  stick.update(1650.0f, 1650.0f, 3300.0f);
  stick.update(3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.z());
}

// --- parity with the on-device check ---

TEST_CASE("JOY-018 the legacy joystick_selftest passes on the host", "[joystick]") {
  // asserts are live: the host build does not define NDEBUG
  TEST_ASSERT_TRUE(espp::joystick_selftest());
}
