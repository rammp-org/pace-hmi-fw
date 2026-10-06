// L1 characterisation of main/hmi_rtps_spec.hpp (cases RTPS-001..RTPS-021): the seat
// conversion helpers, the names, the self-test tags, the timing, the display limits and
// the HMI's own banner texts, pinned as they behave today (TS-UNIT-04, CS-SAF-03).
//
// Undefined behaviour is never executed here. seat_raw() ends in a float -> int32_t cast,
// which is UB when the rounded value does not fit int32_t (C++ [conv.fpint]): NaN, +-inf,
// 1e30 (plan H8). Each such case is pinned through the compiler's constant evaluator,
// which must reject UB: `raw_is_defined<V>` is false exactly when seat_raw(spec, V) is not
// a constant expression. The runtime cases call seat_raw() only on inputs the same
// evaluator accepted, so UBSan stays quiet and the suite is never aborted.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>
#include <type_traits>

#include "hmi_rtps_spec.hpp"
#include "test_case.hpp"

namespace {

using rammp::kSeatAxes;
using rammp::SeatAxis;
using rammp::SeatAxisSpec;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

/// A table row with only `decimals` set: scale_of and seat_raw read nothing else.
constexpr SeatAxisSpec with_decimals(uint8_t decimals) {
  return SeatAxisSpec{SeatAxis::FRONT_BACK_TILT, "", "", 0, 0, 0, decimals, ""};
}

/// True when seat_raw(kSeatAxes[Row], V) is defined: the constant evaluator rejects UB.
template <size_t Row, float V>
concept raw_is_defined = requires {
  typename std::integral_constant<int32_t, rammp::seat_raw(kSeatAxes[Row], V)>;
};

/// Same for scale_of with D decimals (10^D overflows int32_t from D = 10).
template <uint8_t D>
concept scale_is_defined = requires {
  typename std::integral_constant<int32_t, rammp::scale_of(with_decimals(D))>;
};

/// The input classes of plan H8, for every row of the table at once.
template <float V> constexpr bool raw_defined_on_every_axis() {
  return raw_is_defined<0, V> && raw_is_defined<1, V> && raw_is_defined<2, V> &&
         raw_is_defined<3, V>;
}
template <float V> constexpr bool raw_undefined_on_every_axis() {
  return !raw_is_defined<0, V> && !raw_is_defined<1, V> && !raw_is_defined<2, V> &&
         !raw_is_defined<3, V>;
}
static_assert(rammp::kSeatAxisCount == 4, "raw_*_on_every_axis names four rows");

/// seat_raw at run time, so it is compiled and counted by coverage.
int32_t raw_at_runtime(const SeatAxisSpec &spec, float value) {
  volatile float v = value;
  return rammp::seat_raw(spec, v);
}

} // namespace

TEST_CASE("RTPS-001 the seat axis table has four rows in id order with one decimal each",
          "[rtps_spec][seat]") {
  struct Row {
    int32_t min, max, step;
  };
  constexpr std::array<Row, 4> kExpected{
      {{-450, 900, 25}, {-300, 300, 25}, {0, 2500, 50}, {0, 500, 25}}};
  TEST_ASSERT_EQUAL_size_t(4, kSeatAxes.size());
  for (size_t i = 0; i < kSeatAxes.size(); ++i) {
    const SeatAxisSpec &spec = kSeatAxes[i];
    TEST_ASSERT_EQUAL_size_t(i, rammp::index_of(spec.id));
    TEST_ASSERT_EQUAL_INT32(kExpected[i].min, spec.min_value);
    TEST_ASSERT_EQUAL_INT32(kExpected[i].max, spec.max_value);
    TEST_ASSERT_EQUAL_INT32(kExpected[i].step, spec.step);
    TEST_ASSERT_EQUAL_UINT8(1, spec.decimals);
    TEST_ASSERT_EQUAL_INT32(10, rammp::scale_of(spec));
    TEST_ASSERT_TRUE(spec.min_value < spec.max_value);
    TEST_ASSERT_EQUAL_INT32(0, (spec.max_value - spec.min_value) % spec.step);
  }
  // index_of does no range check: an id outside the table maps past its end
  TEST_ASSERT_EQUAL_size_t(200, rammp::index_of(static_cast<SeatAxis>(200)));
}

TEST_CASE("RTPS-002 scale_of is ten to the decimals, and undefined from ten decimals",
          "[rtps_spec][seat]") {
  int32_t expected = 1;
  for (uint8_t d = 0; d <= 9; ++d) {
    volatile uint8_t dv = d;
    TEST_ASSERT_EQUAL_INT32(expected, rammp::scale_of(with_decimals(dv)));
    if (d < 9) {
      expected *= 10;
    }
  }
  static_assert(scale_is_defined<9>);
  // today: a table row with decimals >= 10 overflows int32_t (UB); nothing rejects it
  static_assert(!scale_is_defined<10>);
  static_assert(!scale_is_defined<255>);
}

TEST_CASE("RTPS-003 every raw value in each axis range survives units and back",
          "[rtps_spec][seat]") {
  for (const SeatAxisSpec &spec : kSeatAxes) {
    for (int32_t raw = spec.min_value - 1000; raw <= spec.max_value + 1000; ++raw) {
      const float units = rammp::seat_units(spec, raw);
      TEST_ASSERT_EQUAL_INT32(raw, raw_at_runtime(spec, units));
    }
  }
}

TEST_CASE("RTPS-004 the units-and-back round trip holds to 2^23 raw and breaks one past it",
          "[rtps_spec][seat]") {
  const SeatAxisSpec &spec = kSeatAxes[0];
  constexpr int32_t kLimit = 1 << 23; // 8388608: a float's 24-bit mantissa runs out
  int32_t first_bad = 0;
  for (int32_t raw = -kLimit; raw <= kLimit && first_bad == 0; ++raw) {
    if (rammp::seat_raw(spec, rammp::seat_units(spec, raw)) != raw) {
      first_bad = raw == 0 ? 1 : raw;
    }
  }
  TEST_ASSERT_EQUAL_INT32(0, first_bad);
  TEST_ASSERT_EQUAL_INT32(kLimit + 2, raw_at_runtime(spec, rammp::seat_units(spec, kLimit + 1)));
  TEST_ASSERT_EQUAL_INT32(-kLimit - 2, raw_at_runtime(spec, rammp::seat_units(spec, -kLimit - 1)));
}

TEST_CASE("RTPS-005 seat_raw rounds half away from zero after a float multiply",
          "[rtps_spec][seat]") {
  struct Case {
    float value;
    int32_t raw;
  };
  // today's results on every axis (all have one decimal). Two land above the true half:
  // 0.45f is 0.449999988, times 10 rounds to 4.5f in float, then up to 5 (not 4);
  // 1.15f is 1.14999998, times 10 rounds to 11.5f, then up to 12 (not 11).
  constexpr std::array<Case, 21> kCases{{
      {12.5f, 125}, {12.25f, 123}, {-12.25f, -123}, {0.04f, 0},    {0.05f, 1},      {-0.05f, -1},
      {0.0f, 0},    {-0.0f, 0},    {12.34f, 123},   {12.35f, 124}, {12.36f, 124},   {0.15f, 2},
      {-0.15f, -2}, {90.04f, 900}, {90.05f, 901},   {90.06f, 901}, {-45.05f, -451}, {0.45f, 5},
      {0.55f, 6},   {1.15f, 12},   {2.25f, 23},
  }};
  for (const SeatAxisSpec &spec : kSeatAxes) {
    for (const Case &c : kCases) {
      TEST_ASSERT_EQUAL_INT32(c.raw, raw_at_runtime(spec, c.value));
    }
  }
}

TEST_CASE("RTPS-006 seat_raw adds 0.5 in float, so it is one off near a half and above 2^23",
          "[rtps_spec][seat]") {
  const SeatAxisSpec spec0 = with_decimals(0);
  // 0.49999997 + 0.5 rounds to 1.0f: the true nearest integer is 0
  TEST_ASSERT_EQUAL_INT32(1, raw_at_runtime(spec0, 0.49999997f));
  // 8388609 + 0.5 is not a float and rounds to even: 8388610
  TEST_ASSERT_EQUAL_INT32(8388610, raw_at_runtime(spec0, 8388609.0f));
  TEST_ASSERT_EQUAL_INT32(3, raw_at_runtime(spec0, 2.5f));
  TEST_ASSERT_EQUAL_INT32(-3, raw_at_runtime(spec0, -2.5f));
}

TEST_CASE("RTPS-007 seat_units divides by the scale in float", "[rtps_spec][seat]") {
  const SeatAxisSpec &spec = kSeatAxes[0];
  volatile int32_t one = 1;
  TEST_ASSERT_EQUAL_HEX32(0x3dcccccdU, std::bit_cast<uint32_t>(rammp::seat_units(spec, one)));
  TEST_ASSERT_EQUAL_HEX32(0xc2340000U, std::bit_cast<uint32_t>(rammp::seat_units(spec, -450)));
  TEST_ASSERT_EQUAL_HEX32(0x41480000U, std::bit_cast<uint32_t>(rammp::seat_units(spec, 125)));
  // the UI's "unknown" sentinel is not special here: it converts like any number
  TEST_ASSERT_EQUAL_FLOAT(-214748364.8f, rammp::seat_units(spec, INT32_MIN));
}

TEST_CASE("RTPS-008 seat_field reads each axis from its own field and 0 for an unknown id",
          "[rtps_spec][seat]") {
  const MIB::seatState seat{1.5f, -2.5f, 3.5f, 4.5f};
  TEST_ASSERT_EQUAL_FLOAT(1.5f, rammp::seat_field(seat, SeatAxis::FRONT_BACK_TILT));
  TEST_ASSERT_EQUAL_FLOAT(-2.5f, rammp::seat_field(seat, SeatAxis::LATERAL_TILT));
  TEST_ASSERT_EQUAL_FLOAT(3.5f, rammp::seat_field(seat, SeatAxis::ELEVATION));
  TEST_ASSERT_EQUAL_FLOAT(4.5f, rammp::seat_field(seat, SeatAxis::TRANSLATION));
  for (const SeatAxisSpec &spec : kSeatAxes) {
    TEST_ASSERT_EQUAL_FLOAT(
        rammp::seat_field(seat, spec.id),
        rammp::seat_field(seat, static_cast<SeatAxis>(rammp::index_of(spec.id))));
  }
  // today: an id outside the table reads 0.0, a real-looking value, not "unknown"
  volatile uint8_t bad = 4;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rammp::seat_field(seat, static_cast<SeatAxis>(bad)));
}

TEST_CASE("RTPS-009 H8 hazard: seat_raw of NaN is undefined behaviour today on every axis",
          "[rtps_spec][seat][hazard]") {
  // pinned on the input class: the constant evaluator rejects the conversion (UB)
  static_assert(raw_undefined_on_every_axis<kNaN>());
  static_assert(raw_undefined_on_every_axis<-kNaN>());
  // nothing in the header filters it: seat_field passes a NaN field straight through
  const MIB::seatState seat{kNaN, kNaN, kNaN, kNaN};
  for (const SeatAxisSpec &spec : kSeatAxes) {
    TEST_ASSERT_TRUE(std::isnan(rammp::seat_field(seat, spec.id)));
  }
}

TEST_CASE("RTPS-010 H8 hazard: seat_raw of plus or minus infinity is undefined behaviour today",
          "[rtps_spec][seat][hazard]") {
  static_assert(raw_undefined_on_every_axis<kInf>());
  static_assert(raw_undefined_on_every_axis<-kInf>());
  TEST_PASS();
}

TEST_CASE("RTPS-011 H8 hazard: seat_raw of a huge value (1e30, -1e30, 3e8) is undefined today",
          "[rtps_spec][seat][hazard]") {
  static_assert(raw_undefined_on_every_axis<1e30f>());
  static_assert(raw_undefined_on_every_axis<-1e30f>());
  static_assert(raw_undefined_on_every_axis<3e8f>());
  static_assert(raw_undefined_on_every_axis<-3e8f>());
  static_assert(raw_undefined_on_every_axis<std::numeric_limits<float>::max()>());
  static_assert(raw_undefined_on_every_axis<std::numeric_limits<float>::lowest()>());
  TEST_PASS();
}

TEST_CASE("RTPS-012 H8 boundary: seat_raw is defined to +214748352 and down to -214748368",
          "[rtps_spec][seat][hazard]") {
  static_assert(raw_defined_on_every_axis<214748352.0f>());    // raw 2147483520
  static_assert(raw_undefined_on_every_axis<214748368.0f>());  // the next float up
  static_assert(raw_defined_on_every_axis<-214748368.0f>());   // raw INT32_MIN
  static_assert(raw_undefined_on_every_axis<-214748384.0f>()); // the next float down
  TEST_ASSERT_EQUAL_INT32(2147483520, raw_at_runtime(kSeatAxes[0], 214748352.0f));
  // today: this finite value lands exactly on INT32_MIN, the UI's VALUE_UNKNOWN sentinel
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, raw_at_runtime(kSeatAxes[0], -214748368.0f));
}

TEST_CASE("RTPS-013 H8 hazard: an out-of-range finite seat value passes through unflagged",
          "[rtps_spec][seat][hazard]") {
  struct Case {
    size_t row;
    float value;
    int32_t raw;
  };
  // today: no clamp and no "unknown" - the caller gets a raw value outside min..max
  constexpr std::array<Case, 6> kCases{{{0, -45.1f, -451},
                                        {0, 90.1f, 901},
                                        {2, 1e6f, 10000000},
                                        {2, -1.0f, -10},
                                        {3, -1e6f, -10000000},
                                        {1, 2e8f, 2000000000}}};
  for (const Case &c : kCases) {
    const SeatAxisSpec &spec = kSeatAxes[c.row];
    const int32_t raw = raw_at_runtime(spec, c.value);
    TEST_ASSERT_EQUAL_INT32(c.raw, raw);
    TEST_ASSERT_TRUE(raw < spec.min_value || raw > spec.max_value);
  }
}

TEST_CASE("RTPS-014 names: each MIB state and profile has its name, any other value reads ?",
          "[rtps_spec]") {
  using MIB::DriveProfile;
  using MIB::MibSystemState;
  TEST_ASSERT_EQUAL_STRING("INITIALIZING", rammp::to_string(MibSystemState::INITIALIZING));
  TEST_ASSERT_EQUAL_STRING("IDLE", rammp::to_string(MibSystemState::IDLE));
  TEST_ASSERT_EQUAL_STRING("ENABLED", rammp::to_string(MibSystemState::ENABLED));
  TEST_ASSERT_EQUAL_STRING("ERROR", rammp::to_string(MibSystemState::ERROR));
  TEST_ASSERT_EQUAL_STRING("LOW", rammp::to_string(DriveProfile::LOW));
  TEST_ASSERT_EQUAL_STRING("NORMAL", rammp::to_string(DriveProfile::NORMAL));
  TEST_ASSERT_EQUAL_STRING("HIGH", rammp::to_string(DriveProfile::HIGH));
  for (unsigned v = 4; v <= 255; ++v) {
    volatile uint8_t b = static_cast<uint8_t>(v);
    TEST_ASSERT_EQUAL_STRING("?", rammp::to_string(static_cast<MibSystemState>(b)));
  }
  for (unsigned v = 3; v <= 255; ++v) {
    volatile uint8_t b = static_cast<uint8_t>(v);
    TEST_ASSERT_EQUAL_STRING("?", rammp::to_string(static_cast<DriveProfile>(b)));
  }
}

TEST_CASE("RTPS-015 self-test tags live in the top nibble and tagged() keeps the payload below",
          "[rtps_spec]") {
  using rammp::SelfTestTag;
  TEST_ASSERT_EQUAL_HEX32(0xF0000000U, rammp::kSelfTestTagMask);
  volatile uint32_t run7 = 0xC0000007U;
  TEST_ASSERT_TRUE(rammp::tag_of(run7) == SelfTestTag::RUN);
  TEST_ASSERT_TRUE(rammp::tag_of(0x50001234U) == SelfTestTag::PING);
  TEST_ASSERT_TRUE(rammp::tag_of(0xA0FF1234U) == SelfTestTag::PONG);
  TEST_ASSERT_TRUE(rammp::tag_of(0x0FFFFFFFU) == SelfTestTag::NONE);
  // a top nibble no tag uses comes back as that value, not as NONE
  TEST_ASSERT_EQUAL_HEX32(0x10000000U, static_cast<uint32_t>(rammp::tag_of(0x1ABCDEF0U)));
  volatile uint32_t payload = 0xFFFFFFFFU;
  TEST_ASSERT_EQUAL_HEX32(0xCFFFFFFFU, rammp::tagged(SelfTestTag::RUN, payload));
  TEST_ASSERT_EQUAL_HEX32(0x50001234U, rammp::tagged(SelfTestTag::PING, 0x1234U));
  TEST_ASSERT_EQUAL_HEX32(0x0ABCDEF0U, rammp::tagged(SelfTestTag::NONE, 0xFABCDEF0U));
}

TEST_CASE("RTPS-016 link timing: status and diagnostics every 500 ms, stale after 2000 ms",
          "[rtps_spec][safety]") {
  TEST_ASSERT_EQUAL_INT64(500, rammp::kMibStatusPeriod.count());
  TEST_ASSERT_EQUAL_INT64(2000, rammp::kMibStatusTimeout.count());
  TEST_ASSERT_EQUAL_INT64(500, rammp::kDiagPeriod.count());
  TEST_ASSERT_EQUAL_INT64(2000, rammp::kDiagTimeout.count());
  // four periods of silence before "lost": three consecutive drops are tolerated
  TEST_ASSERT_EQUAL_INT64(4, rammp::kMibStatusTimeout / rammp::kMibStatusPeriod);
  TEST_ASSERT_EQUAL_INT64(4, rammp::kDiagTimeout / rammp::kDiagPeriod);
}

TEST_CASE("RTPS-017 display limits and the speed readout constants", "[rtps_spec]") {
  TEST_ASSERT_EQUAL_size_t(16, rammp::kMcbTextLen);
  TEST_ASSERT_EQUAL_size_t(64, rammp::kErrorTextLen);
  TEST_ASSERT_EQUAL_size_t(32, rammp::kErrorFooterLen);
  TEST_ASSERT_EQUAL_HEX32(std::bit_cast<uint32_t>(2.236936f),
                          std::bit_cast<uint32_t>(rammp::kMphPerMps));
  TEST_ASSERT_EQUAL_INT32(99, rammp::kSpeedMaxTenths);
}

TEST_CASE("RTPS-018 every HMI banner text fits the banner field it is shown in", "[rtps_spec]") {
  constexpr std::array kBodies{std::string_view{rammp::kHmiDriveNotGrantedText},
                               std::string_view{rammp::kHmiDriveStoppedText},
                               std::string_view{rammp::kHmiExitRefusedText},
                               std::string_view{rammp::kHmiEthFailedText},
                               std::string_view{rammp::kHmiLinkDownText},
                               std::string_view{rammp::kHmiWifiFailedText},
                               std::string_view{rammp::kHmiWifiDownText},
                               std::string_view{rammp::kHmiNoIpText},
                               std::string_view{rammp::kHmiNoPeerText}};
  constexpr std::array kFooters{std::string_view{rammp::kHmiDriveNotGrantedFooter},
                                std::string_view{rammp::kHmiDriveStoppedFooter},
                                std::string_view{rammp::kHmiExitRefusedFooter},
                                std::string_view{rammp::kHmiEthFailedFooter},
                                std::string_view{rammp::kHmiLinkDownFooter},
                                std::string_view{rammp::kHmiWifiFailedFooter},
                                std::string_view{rammp::kHmiWifiDownFooter},
                                std::string_view{rammp::kHmiNoIpFooter},
                                std::string_view{rammp::kHmiNoPeerFooter}};
  constexpr std::array kTitles{std::string_view{rammp::kHmiLinkRefusedTitle},
                               std::string_view{rammp::kHmiMcbRefusedTitle},
                               std::string_view{rammp::kHmiSeatLinkRefusedTitle},
                               std::string_view{rammp::kHmiSeatMcbRefusedTitle},
                               std::string_view{rammp::kHmiDriveLostLinkTitle},
                               std::string_view{rammp::kHmiDriveLostMcbTitle},
                               std::string_view{rammp::kHmiLinkLostTitle},
                               std::string_view{rammp::kHmiMcbFaultTitle},
                               std::string_view{rammp::kHmiDriveNotGrantedTitle},
                               std::string_view{rammp::kHmiDriveStoppedTitle},
                               std::string_view{rammp::kHmiExitRefusedTitle}};
  for (const std::string_view text : kBodies) {
    TEST_ASSERT_TRUE(text.size() < rammp::kErrorTextLen);
  }
  for (const std::string_view text : kFooters) {
    TEST_ASSERT_TRUE(text.size() < rammp::kErrorFooterLen);
  }
  for (const std::string_view text : kTitles) {
    TEST_ASSERT_TRUE(text.size() < rammp::kErrorTextLen);
  }
  TEST_ASSERT_EQUAL_STRING("topic rammp/mib/status", rammp::kHmiNoPeerFooter);
}

TEST_CASE("RTPS-019 the no-text fault line fits the banner for every state byte", "[rtps_spec]") {
  std::array<char, 128> line{};
  size_t longest = 0;
  for (unsigned v = 0; v <= 255; ++v) {
    const auto state = static_cast<MIB::MibSystemState>(v);
    const int n = std::snprintf(line.data(), line.size(), rammp::kHmiMcbNoTextFmt, v,
                                rammp::to_string(state));
    TEST_ASSERT_TRUE(n > 0);
    longest = std::max(longest, static_cast<size_t>(n));
  }
  std::snprintf(line.data(), line.size(), rammp::kHmiMcbNoTextFmt, 0U, "INITIALIZING");
  TEST_ASSERT_EQUAL_STRING("systemState=0 (INITIALIZING), error_message empty", line.data());
  TEST_ASSERT_EQUAL_size_t(49, longest);
  TEST_ASSERT_TRUE(longest < rammp::kErrorTextLen);
}

TEST_CASE("RTPS-020 topic and type names on the wire (CS-CFG-03)", "[rtps_spec][safety]") {
  TEST_ASSERT_EQUAL_STRING("rammp/joystick/xy_twist", rammp::kJoystickXYTwist.name);
  TEST_ASSERT_EQUAL_STRING("rammp/msg/XYTwist", rammp::kJoystickXYTwist.type);
  TEST_ASSERT_EQUAL_STRING("rammp/joystick/drive_command", rammp::kJoystickDriveCommand.name);
  TEST_ASSERT_EQUAL_STRING("rammp/msg/DriveCommand", rammp::kJoystickDriveCommand.type);
  TEST_ASSERT_EQUAL_STRING("rammp/joystick/seat_command", rammp::kJoystickSeatCommand.name);
  TEST_ASSERT_EQUAL_STRING("rammp/msg/SeatCommand", rammp::kJoystickSeatCommand.type);
  TEST_ASSERT_EQUAL_STRING("rammp/mcb/diagnostics", rammp::kMcbDiagnostics.name);
  TEST_ASSERT_EQUAL_STRING("rammp/msg/Diagnostics", rammp::kMcbDiagnostics.type);
  TEST_ASSERT_EQUAL_STRING("rammp/mib/status", MIB::kMibStatus.name);
  TEST_ASSERT_EQUAL_STRING("rammp/msg/MibStatus", MIB::kMibStatus.type);
  TEST_ASSERT_EQUAL_STRING("rammp/hmi/counter", rammp::kHmiCounter.name);
  TEST_ASSERT_EQUAL_STRING("rammp/hmi/command", rammp::kHmiCommand.name);
  TEST_ASSERT_EQUAL_STRING("rammp/hmi/brightness", rammp::kHmiBrightness.name);
  TEST_ASSERT_EQUAL_STRING("std_msgs/msg/UInt32", rammp::kHmiCommand.type);
  TEST_ASSERT_EQUAL_STRING("rammp/selftest/report", rammp::kSelfTestReport.name);
  TEST_ASSERT_EQUAL_STRING("rammp/msg/SelfTestReport", rammp::kSelfTestReport.type);
}

TEST_CASE("RTPS-021 enum values and wire widths of the messages", "[rtps_spec][safety]") {
  static_assert(sizeof(rammp::SeatAxis) == 1 && sizeof(rammp::DriveRequest) == 1);
  static_assert(sizeof(MIB::DriveProfile) == 1 && sizeof(MIB::MibSystemState) == 1);
  static_assert(sizeof(rammp::Buttons) == 4);
  static_assert(sizeof(rammp::SelfTestKind) == 1 && sizeof(rammp::SelfTestResult) == 1);
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(rammp::DriveRequest::DISABLE));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(rammp::DriveRequest::ENABLE));
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(MIB::DriveProfile::LOW));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(MIB::DriveProfile::HIGH));
  TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(MIB::MibSystemState::ERROR));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(rammp::SeatAxis::ELEVATION));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(rammp::Buttons::JOYSTICK));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(rammp::SelfTestResult::SKIP));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(rammp::SelfTestKind::FINISHED));
  // a freshly made MibStatus says INITIALIZING / NORMAL / "No error" and seat 0
  const MIB::MibStatus fresh{};
  TEST_ASSERT_TRUE(fresh.systemState == MIB::MibSystemState::INITIALIZING);
  TEST_ASSERT_TRUE(fresh.activeProfile == MIB::DriveProfile::NORMAL);
  TEST_ASSERT_EQUAL_STRING("No error", fresh.error_message.c_str());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, fresh.currentSeatState.elevation);
}
