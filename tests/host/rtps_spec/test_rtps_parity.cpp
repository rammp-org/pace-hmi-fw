// L1 parity of the host codec, scripts/rammp_rtps.py, with the firmware's C++ (cases
// RTPS-060..RTPS-067; TS-UNIT-04, CS-CFG-03). rammp_rtps.py scrapes the headers at import
// and keeps its own copy of each message layout (MSG_*) and of the seat rounding rule
// (SeatAxis.parse); parity.py writes what it sees into py_spec.inc at build time.
//
// Both directions of the codec, per message vector: C++ encode == Python encode (byte for
// byte), C++ decodes the Python bytes to the values, and Python decodes those same bytes -
// which the case has just shown are the C++ bytes - to the values.
//
// Where the two disagree today, the disagreement is pinned by name or by count, and every
// one must fall in a known class; an unexplained one fails the case.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "rtps_test_util.hpp"
#include "test_case.hpp"

using namespace rtps_test;

namespace {

struct PySeatAxis {
  int id;
  const char *name, *short_name, *label;
  int32_t min_value, max_value, step;
  int decimals;
  const char *unit;
};
struct PyDiagItem {
  int id;
  const char *name, *short_name, *label;
  std::array<const char *, 3> units;
  std::array<int, 3> decimals;
};
struct PyNamed {
  const char *name;
  long long value;
};
struct PyText {
  const char *name, *text;
};
struct PySeatParse {
  int axis;
  const char *text;
  int32_t raw;
};
struct PySeatFormat {
  int axis;
  int32_t raw;
  const char *text;
};
template <class T> struct Vec {
  const char *name;
  T value;
  Bytes py_bytes;
  bool py_ok;
  T py_decoded;
};
enum class Msg {
  XYTwist,
  DriveCommand,
  SeatCommand,
  MibStatus,
  Diagnostics,
  UInt32,
  SelfTestReport
};
struct Hostile {
  Msg msg;
  const char *name;
  Bytes data;
  bool py_ok;
  Bytes py_reencoded;
};

#include "py_spec.inc"

bool text_eq(const char *a, const char *b) { return std::string_view{a} == std::string_view{b}; }

template <class T, size_t N> void check_vectors(const Vec<T> (&vecs)[N]) {
  for (const Vec<T> &v : vecs) {
    TEST_ASSERT_TRUE_MESSAGE(encode(v.value) == v.py_bytes, v.name); // same bytes
    const auto back = decode<T>(v.py_bytes);
    TEST_ASSERT_TRUE_MESSAGE(back.has_value(), v.name); // C++ reads Python's bytes
    TEST_ASSERT_TRUE_MESSAGE(eq(v.value, *back), v.name);
    TEST_ASSERT_TRUE_MESSAGE(v.py_ok, v.name); // Python reads C++'s bytes
    TEST_ASSERT_TRUE_MESSAGE(eq(v.value, v.py_decoded), v.name);
  }
}

struct Tally {
  size_t agree = 0;  // same verdict, same value
  size_t header = 0; // C++ accepts a header Python refuses (BE, XCDR2, ...)
  size_t no_nul = 0; // Python accepts a string without its NUL; C++ refuses
  size_t text = 0;   // both accept; Python cuts at a NUL or drops non-ASCII bytes
  size_t snan = 0;   // both accept; Python quiets a signalling NaN (float32 -> double)
  size_t unexplained = 0;
};

/// True if `a` and `b` differ only in 4-byte words that are NaN in both: XCDR1 aligns a
/// float to 4 from the first payload byte, so such a word is one float field.
bool differ_only_in_nans(const Bytes &a, const Bytes &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 4; i < a.size(); ++i) {
    if (a[i] != b[i]) {
      const size_t word = 4 + ((i - 4) / 4) * 4;
      if (word + 4 > a.size()) {
        return false;
      }
      uint32_t wa = 0;
      uint32_t wb = 0;
      std::memcpy(&wa, &a[word], 4);
      std::memcpy(&wb, &b[word], 4);
      if (!std::isnan(F(wa)) || !std::isnan(F(wb))) {
        return false;
      }
    }
  }
  return true;
}

template <class T> void classify(const Hostile &h, Tally &t) {
  const auto cpp = decode<T>(h.data);
  if (cpp.has_value() == h.py_ok) {
    if (!h.py_ok || encode(*cpp) == h.py_reencoded) {
      ++t.agree;
    } else if (odd_text(*cpp)) {
      ++t.text;
    } else if (differ_only_in_nans(encode(*cpp), h.py_reencoded)) {
      ++t.snan;
    } else {
      ++t.unexplained;
      std::printf("RTPS-064 unexplained value difference: %s\n", h.name);
    }
  } else if (cpp.has_value() && !(h.data[0] == 0x00 && h.data[1] == 0x01)) {
    ++t.header;
  } else if (!cpp.has_value() && cpp.error().code == cdr::errc::invalid_string) {
    ++t.no_nul;
  } else {
    ++t.unexplained;
    std::printf("RTPS-064 unexplained verdict difference: %s (C++ %s, Python %s)\n", h.name,
                cpp ? "accepts" : "rejects", h.py_ok ? "accepts" : "rejects");
  }
}

} // namespace

TEST_CASE("RTPS-060 the host codec's seat axis and diagnostics tables match the firmware's",
          "[rtps_parity][safety]") {
  TEST_ASSERT_EQUAL_size_t(rammp::kSeatAxes.size(), std::size(py::kSeatAxes));
  for (size_t i = 0; i < rammp::kSeatAxes.size(); ++i) {
    const rammp::SeatAxisSpec &c = rammp::kSeatAxes[i];
    const PySeatAxis &p = py::kSeatAxes[i];
    TEST_ASSERT_EQUAL_INT(static_cast<int>(rammp::index_of(c.id)), p.id);
    TEST_ASSERT_TRUE(text_eq(c.short_name, p.short_name) && text_eq(c.label, p.label));
    TEST_ASSERT_TRUE(text_eq(c.unit, p.unit));
    TEST_ASSERT_EQUAL_INT32(c.min_value, p.min_value);
    TEST_ASSERT_EQUAL_INT32(c.max_value, p.max_value);
    TEST_ASSERT_EQUAL_INT32(c.step, p.step);
    TEST_ASSERT_EQUAL_INT(c.decimals, p.decimals);
  }
  constexpr std::array kAxisNames{"FRONT_BACK_TILT", "LATERAL_TILT", "ELEVATION", "TRANSLATION"};
  for (size_t i = 0; i < kAxisNames.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(kAxisNames[i], py::kSeatAxes[i].name);
  }
  TEST_ASSERT_EQUAL_size_t(rammp::kDiagItems.size(), std::size(py::kDiagItems));
  for (size_t i = 0; i < rammp::kDiagItems.size(); ++i) {
    const rammp::DiagSpec &c = rammp::kDiagItems[i];
    const PyDiagItem &p = py::kDiagItems[i];
    TEST_ASSERT_EQUAL_INT(static_cast<int>(c.id), p.id);
    TEST_ASSERT_TRUE(text_eq(c.short_name, p.short_name) && text_eq(c.label, p.label));
    for (size_t f = 0; f < rammp::kDiagFields; ++f) {
      TEST_ASSERT_TRUE(text_eq(c.unit[f], p.units[f]));
      TEST_ASSERT_EQUAL_INT(c.decimals[f], p.decimals[f]);
    }
  }
}

TEST_CASE("RTPS-061 the host codec's enums, topics, types and numbers match the firmware's",
          "[rtps_parity][safety]") {
  using rammp::SelfTestTag;
  const std::array<PyNamed, 21> enums{{
      {"BUTTONS_JOYSTICK", static_cast<long long>(rammp::Buttons::JOYSTICK)},
      {"BUTTONS_NONE", static_cast<long long>(rammp::Buttons::NONE)},
      {"DRIVE_PROFILE_HIGH", static_cast<long long>(MIB::DriveProfile::HIGH)},
      {"DRIVE_PROFILE_LOW", static_cast<long long>(MIB::DriveProfile::LOW)},
      {"DRIVE_PROFILE_NORMAL", static_cast<long long>(MIB::DriveProfile::NORMAL)},
      {"DRIVE_REQUEST_DISABLE", static_cast<long long>(rammp::DriveRequest::DISABLE)},
      {"DRIVE_REQUEST_ENABLE", static_cast<long long>(rammp::DriveRequest::ENABLE)},
      {"MIB_SYSTEM_STATE_ENABLED", static_cast<long long>(MIB::MibSystemState::ENABLED)},
      {"MIB_SYSTEM_STATE_ERROR", static_cast<long long>(MIB::MibSystemState::ERROR)},
      {"MIB_SYSTEM_STATE_IDLE", static_cast<long long>(MIB::MibSystemState::IDLE)},
      {"MIB_SYSTEM_STATE_INITIALIZING", static_cast<long long>(MIB::MibSystemState::INITIALIZING)},
      {"SELFTEST_KIND_FINISHED", static_cast<long long>(rammp::SelfTestKind::FINISHED)},
      {"SELFTEST_KIND_RESULT", static_cast<long long>(rammp::SelfTestKind::RESULT)},
      {"SELFTEST_KIND_STARTED", static_cast<long long>(rammp::SelfTestKind::STARTED)},
      {"SELFTEST_RESULT_FAIL", static_cast<long long>(rammp::SelfTestResult::FAIL)},
      {"SELFTEST_RESULT_PASS", static_cast<long long>(rammp::SelfTestResult::PASS)},
      {"SELFTEST_RESULT_SKIP", static_cast<long long>(rammp::SelfTestResult::SKIP)},
      {"SELFTEST_TAG_NONE", static_cast<long long>(SelfTestTag::NONE)},
      {"SELFTEST_TAG_PING", static_cast<long long>(SelfTestTag::PING)},
      {"SELFTEST_TAG_PONG", static_cast<long long>(SelfTestTag::PONG)},
      {"SELFTEST_TAG_RUN", static_cast<long long>(SelfTestTag::RUN)},
  }};
  // SeatAxis, DiagId, and the motor enums are not scraped: the codec has no ENUMS for them
  TEST_ASSERT_EQUAL_size_t(enums.size(), std::size(py::kEnums));
  for (size_t i = 0; i < enums.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(enums[i].name, py::kEnums[i].name);
    TEST_ASSERT_TRUE(enums[i].value == py::kEnums[i].value);
  }
  const std::array<PyText, 16> strings{{
      {"TOPIC_HMI_BRIGHTNESS", rammp::kHmiBrightness.name},
      {"TOPIC_HMI_COMMAND", rammp::kHmiCommand.name},
      {"TOPIC_HMI_COUNTER", rammp::kHmiCounter.name},
      {"TOPIC_JOYSTICK_DRIVE_COMMAND", rammp::kJoystickDriveCommand.name},
      {"TOPIC_JOYSTICK_SEAT_COMMAND", rammp::kJoystickSeatCommand.name},
      {"TOPIC_JOYSTICK_XY_TWIST", rammp::kJoystickXYTwist.name},
      {"TOPIC_MCB_DIAGNOSTICS", rammp::kMcbDiagnostics.name},
      {"TOPIC_MIB_STATUS", MIB::kMibStatus.name},
      {"TOPIC_SELFTEST_REPORT", rammp::kSelfTestReport.name},
      {"TYPE_DIAGNOSTICS", rammp::kMcbDiagnostics.type},
      {"TYPE_DRIVE_COMMAND", rammp::kJoystickDriveCommand.type},
      {"TYPE_MIB_STATUS", MIB::kMibStatus.type},
      {"TYPE_SEAT_COMMAND", rammp::kJoystickSeatCommand.type},
      {"TYPE_SELFTEST_REPORT", rammp::kSelfTestReport.type},
      {"TYPE_UINT32", rammp::kHmiCounter.type},
      {"TYPE_XY_TWIST", rammp::kJoystickXYTwist.type},
  }};
  TEST_ASSERT_EQUAL_size_t(strings.size(), std::size(py::kStrings));
  for (size_t i = 0; i < strings.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(strings[i].name, py::kStrings[i].name);
    TEST_ASSERT_EQUAL_STRING(strings[i].text, py::kStrings[i].text);
  }
  const std::array<PyNamed, 10> numbers{{
      {"DIAG_FIELDS", static_cast<long long>(rammp::kDiagFields)},
      {"DIAG_PERIOD_MS", rammp::kDiagPeriod.count()},
      {"DIAG_TIMEOUT_MS", rammp::kDiagTimeout.count()},
      {"ERROR_FOOTER_LEN", static_cast<long long>(rammp::kErrorFooterLen)},
      {"ERROR_TEXT_LEN", static_cast<long long>(rammp::kErrorTextLen)},
      {"MCB_TEXT_LEN", static_cast<long long>(rammp::kMcbTextLen)},
      {"MIB_STATUS_PERIOD_MS", rammp::kMibStatusPeriod.count()},
      {"MIB_STATUS_TIMEOUT_MS", rammp::kMibStatusTimeout.count()},
      {"SELFTEST_TAG_MASK", static_cast<long long>(rammp::kSelfTestTagMask)},
      {"SPEED_MAX_TENTHS", rammp::kSpeedMaxTenths},
  }};
  TEST_ASSERT_EQUAL_size_t(numbers.size(), std::size(py::kNumbers));
  for (size_t i = 0; i < numbers.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(numbers[i].name, py::kNumbers[i].name);
    TEST_ASSERT_TRUE(numbers[i].value == py::kNumbers[i].value);
  }
  // MPH_PER_MPS is a hand copy (not scraped): it must round to the same float
  TEST_ASSERT_EQUAL_HEX32(bits(rammp::kMphPerMps), bits(static_cast<float>(py::kMphPerMps)));
}

TEST_CASE("RTPS-062 joystick-to-MIB messages: same bytes from both codecs, each reads the other's",
          "[rtps_parity][safety]") {
  check_vectors(py::kXYTwist);
  check_vectors(py::kDriveCommand);
  check_vectors(py::kSeatCommand);
}

TEST_CASE("RTPS-063 MIB-to-HMI and bench messages: same bytes from both codecs, each reads the "
          "other's",
          "[rtps_parity][safety]") {
  check_vectors(py::kMibStatus);
  check_vectors(py::kDiagnostics);
  check_vectors(py::kUInt32);
  check_vectors(py::kSelfTestReport);
}

TEST_CASE("RTPS-064 hostile corpus: both codecs agree except in the known divergence classes",
          "[rtps_parity][hostile]") {
  Tally t;
  for (const Hostile &h : py::kCorpus) {
    switch (h.msg) {
    case Msg::XYTwist:
      classify<rammp::XYTwist>(h, t);
      break;
    case Msg::DriveCommand:
      classify<rammp::DriveCommand>(h, t);
      break;
    case Msg::SeatCommand:
      classify<rammp::SeatCommand>(h, t);
      break;
    case Msg::MibStatus:
      classify<MIB::MibStatus>(h, t);
      break;
    case Msg::Diagnostics:
      classify<rammp::Diagnostics>(h, t);
      break;
    case Msg::UInt32:
      classify<rammp::UInt32>(h, t);
      break;
    case Msg::SelfTestReport:
      classify<rammp::SelfTestReport>(h, t);
      break;
    }
  }
  std::printf("RTPS-064 corpus %zu: agree %zu, header %zu, no_nul %zu, text %zu, snan %zu, "
              "unexplained %zu\n",
              std::size(py::kCorpus), t.agree, t.header, t.no_nul, t.text, t.snan, t.unexplained);
  TEST_ASSERT_EQUAL_size_t(0, t.unexplained);
  // today's counts for the seeded corpus (parity.py SEED); each class is a divergence
  TEST_ASSERT_EQUAL_size_t(986, std::size(py::kCorpus));
  TEST_ASSERT_EQUAL_size_t(916, t.agree);
  TEST_ASSERT_EQUAL_size_t(11, t.header);
  TEST_ASSERT_EQUAL_size_t(6, t.no_nul);
  TEST_ASSERT_EQUAL_size_t(52, t.text);
  TEST_ASSERT_EQUAL_size_t(1, t.snan);
}

TEST_CASE("RTPS-065 divergence: the host's seat parse rounds half to even and clamps",
          "[rtps_parity][seat]") {
  // C++ has no text parse; its rule is seat_raw (half away from zero, in float, no clamp).
  // Compared as strtof -> seat_raw -> clamp to the axis range, what the HMI would send.
  std::string differ;
  for (const PySeatParse &p : py::kSeatParse) {
    const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[static_cast<size_t>(p.axis)];
    const float value = std::strtof(p.text, nullptr);
    const int32_t cpp = std::clamp(rammp::seat_raw(spec, value), spec.min_value, spec.max_value);
    if (cpp != p.raw) {
      differ += std::to_string(p.axis) + ":" + p.text + "=" + std::to_string(p.raw) + "/" +
                std::to_string(cpp) + " ";
    }
  }
  std::printf("RTPS-065 axis:text=python/c++ that differ: %s\n", differ.c_str());
  // today: Python gives 122 where the HMI's rule gives 123 (12.25 is exact: a true half)
  TEST_ASSERT_EQUAL_STRING("0:12.25=122/123 0:-12.25=-122/-123 0:0.05=0/1 0:-0.05=0/-1 "
                           "1:12.25=122/123 1:-12.25=-122/-123 1:0.05=0/1 1:-0.05=0/-1 "
                           "2:12.25=122/123 2:0.05=0/1 3:12.25=122/123 3:0.05=0/1 ",
                           differ.c_str());
}

TEST_CASE("RTPS-066 the host's seat format matches seat_units printed at the row's decimals",
          "[rtps_parity][seat]") {
  for (const PySeatFormat &p : py::kSeatFormat) {
    const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[static_cast<size_t>(p.axis)];
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.*f", static_cast<int>(spec.decimals),
                  static_cast<double>(rammp::seat_units(spec, p.raw)));
    TEST_ASSERT_EQUAL_STRING(p.text, text.data());
  }
}
