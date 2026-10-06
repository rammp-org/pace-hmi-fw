// L1 tests of components/joystick_cal's record: encode, decode, describe and plausible,
// called directly (cal_record.hpp). The same behaviour seen through main is pinned by the
// characterisation cases in test_joystick_cal.cpp; these hold the component to it on its own.

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

bool decode_text(std::string_view text, Record &out, std::error_code &ec) {
  std::istringstream in{std::string(text)};
  return hmi::cal::decode(in, out, ec);
}

} // namespace

TEST_CASE("CAL-101 encode writes board 2's record as the golden version 1 file", "[cal][record]") {
  TEST_ASSERT_EQUAL_STRING(std::string(kBoard2File).c_str(), hmi::cal::encode(kBoard2).c_str());
}

TEST_CASE("CAL-102 decode reads the golden file back as board 2's record and clears the error",
          "[cal][record]") {
  Record out = kSentinel;
  std::error_code ec = DecodeError::NO_TWIST;
  TEST_ASSERT_TRUE(decode_text(kBoard2File, out, ec));
  TEST_ASSERT_FALSE(static_cast<bool>(ec));
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
    std::error_code ec;
    TEST_ASSERT_FALSE_MESSAGE(decode_text(b.text, out, ec), b.text);
    TEST_ASSERT_TRUE_MESSAGE(ec == b.error, b.text);
    TEST_ASSERT_TRUE_MESSAGE(same(out, kSentinel), b.text);
  }
}

TEST_CASE("CAL-105 the error messages are the words the firmware has always logged",
          "[cal][record]") {
  TEST_ASSERT_EQUAL_STRING("joystick_cal", hmi::cal::decode_category().name());
  const std::error_code v = DecodeError::NOT_VERSION_1;
  const std::error_code h = DecodeError::NO_HORIZONTAL;
  const std::error_code ve = DecodeError::NO_VERTICAL;
  const std::error_code t = DecodeError::NO_TWIST;
  TEST_ASSERT_EQUAL_STRING("not a version 1 calibration file", v.message().c_str());
  TEST_ASSERT_EQUAL_STRING("expected a 'horizontal min center max' line", h.message().c_str());
  TEST_ASSERT_EQUAL_STRING("expected a 'vertical min center max' line", ve.message().c_str());
  TEST_ASSERT_EQUAL_STRING("expected a 'twist min center max' line", t.message().c_str());
  TEST_ASSERT_EQUAL_STRING("unknown joystick_cal error",
                           hmi::cal::decode_category().message(99).c_str());
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
    for (AxisCal &a : r) {
      a = {mv(rng), mv(rng), mv(rng)};
    }
    const std::string once = hmi::cal::encode(r);
    Record back{};
    std::error_code ec;
    TEST_ASSERT_TRUE(decode_text(once, back, ec));
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
  std::error_code ec;
  TEST_ASSERT_TRUE(hmi::cal::decode(in, out, ec));
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
    for (char &ch : text) {
      ch = static_cast<char>(byte_dist(rng));
    }
    if (i % 2 == 0) {
      text = "version 1\nhorizontal " + text; // get past the header half the time
    }
    Record out = kSentinel;
    std::error_code ec;
    if (decode_text(text, out, ec)) {
      TEST_ASSERT_FALSE(static_cast<bool>(ec));
    } else {
      TEST_ASSERT_TRUE(ec.category() == hmi::cal::decode_category());
      TEST_ASSERT_TRUE(same(out, kSentinel));
    }
  }
}
