// L1 tests of the stick's mapping engine: espp's 2-axis espp::Joystick (the X/Y gimbal, its
// circular deadzone) and the twist's own espp::FloatRangeMapper, wired as StickPipeline wires
// them (components/stick, J1). Ported one behaviour per case from the 17 assert() statements of
// the espp fork's joystick_selftest() (components/joystick, removed; its z axis is the twist
// mapper here), plus a parity case that runs the boot's hmi::stick::mapping_selftest(). Each
// case builds its own joystick (TS-DET-03).

#include <concepts>

#include "joystick.hpp"
#include "stick/mapping_selftest.hpp"

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

// The fork's circular 3-axis joystick as StickPipeline::map() now does it: X/Y through espp's
// Joystick, then the twist (z) through its own range mapper, never touched by X/Y.
class Stick3 {
public:
  Stick3()
      : xy_({.x_calibration = LINEAR,
             .y_calibration = LINEAR,
             .type = espp::Joystick::Type::CIRCULAR,
             .center_deadzone_radius = CENTER_DEADZONE,
             .range_deadzone = RANGE_DEADZONE}) {
    twist_.configure(DEADBANDED);
  }
  void update(float x, float y, float z) {
    xy_.update(x, y);
    z_ = twist_.map(z);
  }
  void update(float x, float y) { xy_.update(x, y); }
  [[nodiscard]] float x() const { return xy_.x(); }
  [[nodiscard]] float y() const { return xy_.y(); }
  [[nodiscard]] float z() const { return z_; }
  [[nodiscard]] espp::Vector2f position() const { return xy_.position(); }

private:
  espp::Joystick xy_;
  espp::FloatRangeMapper twist_{};
  float z_ = 0.0f;
};

Stick3 make_stick() { return Stick3{}; }

// the fork's third-axis API: update(x, y, z) and z()
template <typename J>
concept HasZ = requires(J &j) {
  j.update(0.0f, 0.0f, 0.0f);
  j.z();
};

} // namespace

// --- rectangular, 2 axes ---

TEST_CASE("JOY-001 espp's joystick has no z axis: the twist is mapped on its own", "[joystick]") {
  // what the fork's has_z() / update(x, y, z) / z() were for is the twist mapper's job
  static_assert(!HasZ<espp::Joystick>);
  espp::Joystick rect = make_rect();
  rect.update(1650.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rect.position().magnitude());
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

TEST_CASE("JOY-005 a full twist reading leaves a rectangular joystick's x/y as they were",
          "[joystick]") {
  espp::Joystick rect = make_rect();
  espp::FloatRangeMapper twist(LINEAR);
  rect.update(2475.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, twist.map(3300.0f));
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.5f, rect.x());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, rect.y());
}

// --- circular, 3 axes ---

TEST_CASE("JOY-006 the twist mapper starts at 0 and maps its first reading", "[joystick]") {
  Stick3 stick = make_stick();
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.z());
  stick.update(1650.0f, 1650.0f, 3300.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.z());
}

TEST_CASE("JOY-007 a circular joystick clamps a full diagonal deflection to the unit circle",
          "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(3300.0f, 3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.position().magnitude());
}

TEST_CASE("JOY-008 a circular joystick at full diagonal deflection reads equal x and y",
          "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(3300.0f, 3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, stick.x(), stick.y());
}

TEST_CASE("JOY-009 a circular joystick inside its centre deadzone reads zero", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f + 82.5f, 1650.0f, 1650.0f); // x maps to 0.05, inside the 0.1 radius
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.x());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.y());
}

TEST_CASE("JOY-010 a circular joystick outside its centre deadzone rescales the magnitude "
          "across the live band",
          "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f + 330.0f, 1650.0f, 1650.0f); // x maps to 0.2
  const float expected = (0.2f - CENTER_DEADZONE) / (1.0f - CENTER_DEADZONE - RANGE_DEADZONE);
  TEST_ASSERT_FLOAT_WITHIN(EPS, expected, stick.x());
}

TEST_CASE("JOY-011 a centred z reading maps to 0", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f + 330.0f, 1650.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.z());
}

TEST_CASE("JOY-012 a z reading inside the z centre deadband maps to 0", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f, 1650.0f, 1700.0f); // 50 mV off centre, deadband 60 mV
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.z());
}

TEST_CASE("JOY-013 a z reading inside the z range deadband saturates at +1", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f, 1650.0f, 3270.0f); // 30 mV short of the rail, deadband 40 mV
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.z());
}

TEST_CASE("JOY-014 a z reading at the low rail maps to -1", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f, 1650.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, -1.0f, stick.z());
}

TEST_CASE("JOY-015 full twist survives the x/y centre deadzone", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f, 1650.0f, 3300.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.x());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.y());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.z());
}

TEST_CASE("JOY-016 a centred twist survives the x/y magnitude clamp", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(3300.0f, 3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.position().magnitude());
  TEST_ASSERT_FLOAT_WITHIN(EPS, 0.0f, stick.z());
}

TEST_CASE("JOY-017 the 2-axis update leaves z at its last value", "[joystick]") {
  Stick3 stick = make_stick();
  stick.update(1650.0f, 1650.0f, 3300.0f);
  stick.update(3300.0f, 1650.0f);
  TEST_ASSERT_FLOAT_WITHIN(EPS, 1.0f, stick.z());
}

// --- parity with the on-device check ---

TEST_CASE("JOY-018 the boot's stick mapping self test passes on the host", "[joystick]") {
  // hmi::stick::mapping_selftest(): the fork's joystick_selftest() checks, moved (J2)
  TEST_ASSERT_TRUE(hmi::stick::mapping_selftest());
}
