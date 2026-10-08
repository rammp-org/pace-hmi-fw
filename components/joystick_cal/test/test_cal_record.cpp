// L1 tests of components/joystick_cal's record: encode, decode, describe and plausible,
// called directly (cal_record.hpp). The same behaviour seen through main is pinned by the
// characterisation cases in test_joystick_cal.cpp; these hold the component to it on its own.

#include <algorithm>
#include <array>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#include "cal_record.hpp"
#include "test_case.hpp"

namespace {

using hmi::cal::AxisCal;
using hmi::cal::DecodeError;
using hmi::cal::Record;

const Record kBoard2{
    {{11.0f, 1507.0f, 2971.0f}, {6.0f, 1510.0f, 2962.0f}, {10.0f, 1477.0f, 2960.0f}}};
constexpr std::string_view kBoard2File = "# joystick calibration, raw ADC mV: min center max\n"
                                         "version 1\n"
                                         "horizontal 11.0 1507.0 2971.0\n"
                                         "vertical 6.0 1510.0 2962.0\n"
                                         "twist 10.0 1477.0 2960.0\n";
const Record kSentinel{{{-1.0f, -2.0f, -3.0f}, {-4.0f, -5.0f, -6.0f}, {-7.0f, -8.0f, -9.0f}}};

bool same(const Record &a, const Record &b) {
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].min_mv != b[i].min_mv || a[i].center_mv != b[i].center_mv ||
        a[i].max_mv != b[i].max_mv) {
      return false;
    }
  }
  return true;
}

bool decode_text(std::string_view text, Record &out, DecodeError &error) {
  std::istringstream in{std::string(text)};
  return hmi::cal::decode(in, out, error);
}

} // namespace

TEST_CASE("CAL-101 encode writes board 2's record as the golden version 1 file", "[cal][record]") {
  TEST_ASSERT_EQUAL_STRING(std::string(kBoard2File).c_str(), hmi::cal::encode(kBoard2).c_str());
}

TEST_CASE("CAL-102 decode reads the golden file back as board 2's record and clears the error",
          "[cal][record]") {
  Record out = kSentinel;
  DecodeError error = DecodeError::NO_TWIST;
  TEST_ASSERT_TRUE(decode_text(kBoard2File, out, error));
  TEST_ASSERT_TRUE(error == DecodeError::NONE);
  TEST_ASSERT_TRUE(same(out, kBoard2));
}

TEST_CASE("CAL-103 describe rounds to whole mV in the boot log's words", "[cal][record]") {
  TEST_ASSERT_EQUAL_STRING("horizontal 11/1507/2971, vertical 6/1510/2962, twist 10/1477/2960 mV",
                           hmi::cal::describe(kBoard2).c_str());
}

TEST_CASE("CAL-104 each rejection names its reason and leaves the output untouched",
          "[cal][record][hostile]") {
  struct Bad {
    const char *text;
    DecodeError error;
  };
  constexpr std::array<Bad, 7> kBad{{
      {"", DecodeError::NOT_VERSION_1},
      {"version 2\n", DecodeError::NOT_VERSION_1},
      {"version 1\n", DecodeError::NO_HORIZONTAL},
      {"version 1\nhorizontal 1 2\n", DecodeError::NO_HORIZONTAL},
      {"version 1\nhorizontal 1 2 3\n", DecodeError::NO_VERTICAL},
      {"version 1\nhorizontal 1 2 3\nvertical 1 2 3\n", DecodeError::NO_TWIST},
      {"version 1\nhorizontal 1 2 3\nvertical 1 2 3\ntwist 1 2 x\n", DecodeError::NO_TWIST},
  }};
  for (const Bad &b : kBad) {
    Record out = kSentinel;
    DecodeError error = DecodeError::NONE;
    TEST_ASSERT_FALSE_MESSAGE(decode_text(b.text, out, error), b.text);
    TEST_ASSERT_TRUE_MESSAGE(error == b.error, b.text);
    TEST_ASSERT_TRUE_MESSAGE(same(out, kSentinel), b.text);
  }
}

TEST_CASE("CAL-105 the error messages are the words the firmware has always logged",
          "[cal][record]") {
  using hmi::cal::message;
  TEST_ASSERT_EQUAL_STRING("not a version 1 calibration file",
                           message(DecodeError::NOT_VERSION_1).c_str());
  TEST_ASSERT_EQUAL_STRING("expected a 'horizontal min center max' line",
                           message(DecodeError::NO_HORIZONTAL).c_str());
  TEST_ASSERT_EQUAL_STRING("expected a 'vertical min center max' line",
                           message(DecodeError::NO_VERTICAL).c_str());
  TEST_ASSERT_EQUAL_STRING("expected a 'twist min center max' line",
                           message(DecodeError::NO_TWIST).c_str());
  TEST_ASSERT_EQUAL_STRING("unknown joystick_cal error",
                           message(static_cast<DecodeError>(99)).c_str());
}

TEST_CASE("CAL-106 plausible needs 1000 mV each way on every axis, inclusive; NaN fails",
          "[cal][record][plausible]") {
  const AxisCal exact{500.0f, 1500.0f, 2500.0f};
  TEST_ASSERT_TRUE(hmi::cal::plausible({exact, exact, exact}));
  // 0.1 mV too close. (One float step is not enough: 1500 - nextafter(500) rounds to 1000.)
  const float below = 500.1f;
  const float above = 2499.9f;
  for (std::size_t axis = 0; axis < hmi::cal::kAxisCount; ++axis) {
    Record r{exact, exact, exact};
    r[axis].min_mv = below;
    TEST_ASSERT_FALSE(hmi::cal::plausible(r));
    r = {exact, exact, exact};
    r[axis].max_mv = above;
    TEST_ASSERT_FALSE(hmi::cal::plausible(r));
    r = {exact, exact, exact};
    r[axis].center_mv = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_FALSE(hmi::cal::plausible(r));
  }
}

TEST_CASE("CAL-107 HAZARD (H3) plausible has no upper bound: even infinite travel passes",
          "[cal][record][plausible][hazard]") {
  constexpr float kInf = std::numeric_limits<float>::infinity();
  const AxisCal huge{-1e30f, 0.0f, 1e30f};
  const AxisCal infinite{-kInf, 0.0f, kInf};
  TEST_ASSERT_TRUE(hmi::cal::plausible({huge, huge, huge}));
  TEST_ASSERT_TRUE(hmi::cal::plausible({infinite, infinite, infinite}));
}

TEST_CASE("CAL-108 encode then decode keeps a record to 0.1 mV and is stable after one pass",
          "[cal][record]") {
  std::mt19937 rng(1006u);
  std::uniform_real_distribution<float> mv(0.0f, 3300.0f);
  for (int i = 0; i < 1000; ++i) {
    Record r{};
    // Braced initialisers evaluate left to right: min, center, max, as before.
    std::generate(r.begin(), r.end(), [&] { return AxisCal{mv(rng), mv(rng), mv(rng)}; });
    const std::string once = hmi::cal::encode(r);
    Record back{};
    DecodeError error = DecodeError::NONE;
    TEST_ASSERT_TRUE(decode_text(once, back, error));
    for (std::size_t axis = 0; axis < r.size(); ++axis) {
      TEST_ASSERT_FLOAT_WITHIN(0.051f, r[axis].min_mv, back[axis].min_mv);
      TEST_ASSERT_FLOAT_WITHIN(0.051f, r[axis].center_mv, back[axis].center_mv);
      TEST_ASSERT_FLOAT_WITHIN(0.051f, r[axis].max_mv, back[axis].max_mv);
    }
    TEST_ASSERT_EQUAL_STRING(once.c_str(), hmi::cal::encode(back).c_str());
  }
}

TEST_CASE("CAL-109 decode stops after the twist line and leaves the rest of the stream unread",
          "[cal][record]") {
  std::istringstream in{std::string(kBoard2File) + "tail"};
  Record out{};
  DecodeError error = DecodeError::NONE;
  TEST_ASSERT_TRUE(hmi::cal::decode(in, out, error));
  std::string rest;
  in >> rest;
  TEST_ASSERT_EQUAL_STRING("tail", rest.c_str());
}

TEST_CASE("CAL-110 seeded random bytes are rejected with a decode error or read as a record",
          "[cal][record][hostile]") {
  std::mt19937 rng(42u);
  std::uniform_int_distribution<int> len_dist(0, 256);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  for (int i = 0; i < 2000; ++i) {
    std::string text(static_cast<std::size_t>(len_dist(rng)), '\0');
    std::generate(text.begin(), text.end(), [&] { return static_cast<char>(byte_dist(rng)); });
    if (i % 2 == 0) {
      text = "version 1\nhorizontal " + text; // get past the header half the time
    }
    Record out = kSentinel;
    DecodeError error = DecodeError::NONE;
    if (decode_text(text, out, error)) {
      TEST_ASSERT_TRUE(error == DecodeError::NONE);
    } else {
      // A decode error, not some other code: one of the four reasons.
      TEST_ASSERT_TRUE(error == DecodeError::NOT_VERSION_1 || error == DecodeError::NO_HORIZONTAL ||
                       error == DecodeError::NO_VERTICAL || error == DecodeError::NO_TWIST);
      TEST_ASSERT_TRUE(same(out, kSentinel));
    }
  }
}

TEST_CASE("CAL-410 valid() boundaries: min -0.1 / 0; span 999.9 / 1000 each way; max 3050 / "
          "3050.1; NaN or inf in any of the 9 numbers",
          "[cal][record][valid]") {
  // Hazard fix C2 (hazard-c2-spec.md §6, §8.4). Expected: invalid / valid; invalid / valid;
  // valid / invalid; invalid.
  const AxisCal ok{300.0f, 1500.0f, 2600.0f};
  TEST_ASSERT_TRUE(hmi::cal::valid({ok, ok, ok}));
  struct Case {
    AxisCal axis;
    bool valid;
  };
  const Case cases[] = {
      {{-0.1f, 1500.0f, 2600.0f}, false},  {{0.0f, 1500.0f, 2600.0f}, true},
      {{500.1f, 1500.0f, 2600.0f}, false}, {{500.0f, 1500.0f, 2600.0f}, true},
      {{300.0f, 1500.0f, 2499.9f}, false}, {{300.0f, 1500.0f, 2500.0f}, true},
      {{300.0f, 1500.0f, 3050.0f}, true},  {{300.0f, 1500.0f, 3050.1f}, false},
  };
  for (std::size_t axis = 0; axis < hmi::cal::kAxisCount; ++axis) {
    for (const Case &c : cases) {
      Record r{ok, ok, ok};
      r[axis] = c.axis;
      TEST_ASSERT_EQUAL(c.valid, hmi::cal::valid(r));
    }
    for (const float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()}) {
      for (int field = 0; field < 3; ++field) {
        Record r{ok, ok, ok};
        (field == 0 ? r[axis].min_mv : field == 1 ? r[axis].center_mv : r[axis].max_mv) = bad;
        TEST_ASSERT_FALSE(hmi::cal::valid(r));
      }
    }
  }
  // Board 2's saved record passes (max 2971 mV, 79 mV to spare).
  TEST_ASSERT_TRUE(
      hmi::cal::valid({AxisCal{11.0f, 1507.0f, 2971.0f}, AxisCal{6.0f, 1510.0f, 2962.0f},
                       AxisCal{10.0f, 1477.0f, 2960.0f}}));
}
