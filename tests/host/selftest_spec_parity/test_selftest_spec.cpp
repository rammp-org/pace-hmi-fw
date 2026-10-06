// L1: the self-test check table (main/selftest_spec.hpp) and its host-script copy
// (TS-UNIT-04, CS-TYP-03). Three views must agree row for row, in order: the firmware's
// table, the golden rows characterised from it (golden_rows.json), and the parse that
// scripts/rammp_rtps.py (used by rtps_selftest.py) makes of the header. Then the table's
// invariants, and the behaviour each row carries (in_limits, required, is_yes_no).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "selftest_spec.hpp"
#include "test_case.hpp"

namespace {

using selftest_spec::Check;
using selftest_spec::Id;
using selftest_spec::kAnyHi;
using selftest_spec::kAnyLo;
using selftest_spec::kChecks;
using selftest_spec::Need;

struct Row {
  const char *id;
  const char *name;
  const char *unit;
  int32_t lo;
  int32_t hi;
  const char *need;
  const char *why;
};

constexpr Row kGolden[] = {
#include "golden_rows.inc"
};

constexpr Row kScript[] = {
#include "script_rows.inc"
};

const char *need_name(Need need) {
  switch (need) {
  case Need::REQUIRED:
    return "required";
  case Need::OPTIONAL:
    return "optional";
  case Need::REMOTE:
    return "remote";
  }
  return "?";
}

// Id's enumerator names, in declaration order, so a row's `Id::X` can be compared with
// the golden "X". Kept by hand like Id itself; SST-001 fails if the two drift.
constexpr std::string_view kIdNames[] = {
    "SYS_RESET",     "SYS_CPU",        "SYS_UPTIME",      "LOG_CAPTURE",     "NET_LINK",
    "NET_IP",        "NET_WIFI_RSSI",  "RTPS_LINK",       "RTPS_MCB_PERIOD", "RTPS_MCB_GAP",
    "RTPS_MCB_LOSS", "RTPS_ADC_HZ",    "RTPS_RTT_P50",    "RTPS_RTT_P99",    "RTPS_PING_LOSS",
    "MEM_INT_FREE",  "MEM_INT_MIN",    "MEM_INT_BLOCK",   "MEM_DMA_FREE",    "MEM_DMA_MIN",
    "MEM_DMA_BLOCK", "MEM_PSRAM_FREE", "MEM_HEAP_OK",     "MEM_STK_LVGL",    "MEM_STK_ADC",
    "MEM_STK_RTPS",  "I2C_MISSING",    "I2C_COUNT",       "IMU_ACCEL",       "RTC_TICK",
    "PWR_VBAT",      "HAP_DRV_ID",     "HAP_DRV_FAULT",   "HAP_DRV_PLAY",    "HAP_DA7280",
    "DISP_DIRECT",   "DISP_BACKLIGHT", "TIME_RENDER_AVG", "TIME_RENDER_MAX", "TIME_UI_STALL",
    "TIME_ADC_AVG",  "TIME_ADC_MAX",   "JOY_VALID",       "JOY_X",           "JOY_Y",
    "JOY_TWIST",     "JOY_X_NOISE",    "JOY_Y_NOISE",     "JOY_TWIST_NOISE", "JOY_CAL",
    "JOY_X_CAL",     "JOY_Y_CAL",      "JOY_TWIST_CAL",   "JOY_BUTTON",
};
static_assert(std::size(kIdNames) == kChecks.size());

void where(char (&buf)[96], std::size_t i, const char *name) {
  std::snprintf(buf, sizeof buf, "row %zu (%s)", i, name);
}

void expect_rows(std::span<const Row> want, std::span<const Row> got) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.size(), got.size(), "row count");
  for (std::size_t i = 0; i < want.size() && i < got.size(); ++i) {
    char at[96];
    where(at, i, want[i].name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].id, got[i].id, at);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].name, got[i].name, at);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].unit, got[i].unit, at);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(want[i].lo, got[i].lo, at);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(want[i].hi, got[i].hi, at);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].need, got[i].need, at);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].why, got[i].why, at);
  }
}

void expect_text(const char *want, std::string_view got, const char *at) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(std::string_view(want).size(), got.size(), at);
  TEST_ASSERT_TRUE_MESSAGE(std::string_view(want) == got, at);
}

// The firmware's table against the golden rows. The golden rows come in as a span (like
// expect_rows), not as kGolden itself: a reader that cannot see the generated
// golden_rows.inc (cppcheck, with no build folder) would take kGolden to be empty.
void expect_table(std::span<const Row> golden) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(golden.size(), kChecks.size(), "row count");
  for (std::size_t i = 0; i < kChecks.size() && i < golden.size(); ++i) {
    const Check &c = kChecks[i];
    const Row &g = golden[i];
    char at[96];
    where(at, i, g.name);
    expect_text(g.id, kIdNames[selftest_spec::index_of(c.id)], at);
    expect_text(g.name, c.name, at);
    expect_text(g.unit, c.unit, at);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(g.lo, c.lo, at);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(g.hi, c.hi, at);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.need, need_name(c.need), at);
    expect_text(g.why, c.why, at);
  }
}

} // namespace

TEST_CASE("SST-001 the firmware's check table matches the golden rows, in order",
          "[selftest_spec]") {
  expect_table(kGolden);
}

TEST_CASE("SST-002 the host scripts' parse of selftest_spec.hpp matches the golden rows, in order",
          "[selftest_spec]") {
  expect_rows(kGolden, kScript);
}

TEST_CASE("SST-003 each row's id is its position, names are unique and limits ordered",
          "[selftest_spec]") {
  TEST_ASSERT_TRUE(kChecks.size() < 256);
  for (std::size_t i = 0; i < kChecks.size(); ++i) {
    const Check &c = kChecks[i];
    char at[96];
    where(at, i, kGolden[i].name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(i, selftest_spec::index_of(c.id), at);
    TEST_ASSERT_TRUE_MESSAGE(&selftest_spec::check(c.id) == &c, at);
    TEST_ASSERT_TRUE_MESSAGE(c.lo <= c.hi, at);
    TEST_ASSERT_FALSE_MESSAGE(c.name.empty(), at);
    TEST_ASSERT_FALSE_MESSAGE(c.why.empty(), at);
    for (std::size_t j = i + 1; j < kChecks.size(); ++j) {
      TEST_ASSERT_FALSE_MESSAGE(c.name == kChecks[j].name, at);
    }
  }
}

TEST_CASE("SST-004 a measured value passes exactly when lo <= value <= hi", "[selftest_spec]") {
  for (std::size_t i = 0; i < kChecks.size(); ++i) {
    const Check &c = kChecks[i];
    char at[96];
    where(at, i, kGolden[i].name);
    TEST_ASSERT_TRUE_MESSAGE(c.in_limits(c.lo), at);
    TEST_ASSERT_TRUE_MESSAGE(c.in_limits(c.hi), at);
    if (c.lo != kAnyLo) {
      TEST_ASSERT_FALSE_MESSAGE(c.in_limits(c.lo - 1), at);
    }
    if (c.hi != kAnyHi) {
      TEST_ASSERT_FALSE_MESSAGE(c.in_limits(c.hi + 1), at);
    }
  }
  // no limit on a side means every value on that side passes
  const Check &uptime = selftest_spec::check(Id::SYS_UPTIME);
  TEST_ASSERT_TRUE(uptime.in_limits(kAnyHi));
  TEST_ASSERT_FALSE(uptime.in_limits(-1));
}

TEST_CASE("SST-005 an unmeasurable check fails when required, and a remote one only remotely",
          "[selftest_spec]") {
  for (std::size_t i = 0; i < kChecks.size(); ++i) {
    const Check &c = kChecks[i];
    char at[96];
    where(at, i, kGolden[i].name);
    const std::string_view need(kGolden[i].need);
    TEST_ASSERT_EQUAL_MESSAGE(need != "optional", c.required(true), at);
    TEST_ASSERT_EQUAL_MESSAGE(need == "required", c.required(false), at);
  }
}

TEST_CASE("SST-006 a yes/no check is the one with limits 1..1 and no unit", "[selftest_spec]") {
  for (std::size_t i = 0; i < kChecks.size(); ++i) {
    const Row &g = kGolden[i];
    char at[96];
    where(at, i, g.name);
    const bool yes_no = g.lo == 1 && g.hi == 1 && g.unit[0] == '\0';
    TEST_ASSERT_EQUAL_MESSAGE(yes_no, kChecks[i].is_yes_no(), at);
  }
  TEST_ASSERT_TRUE(selftest_spec::check(Id::SYS_RESET).is_yes_no());
  TEST_ASSERT_FALSE(selftest_spec::check(Id::I2C_MISSING).is_yes_no()); // 0..0: a count
  TEST_ASSERT_FALSE(selftest_spec::check(Id::JOY_VALID).is_yes_no());   // 99..100 %
}
