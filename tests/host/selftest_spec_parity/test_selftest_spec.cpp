// L1: the self-test check table (main/selftest_spec.h) and its host-script copy
// (TS-UNIT-04, CS-TYP-03). Three views must agree row for row, in order: the firmware's
// table, the golden rows characterised from it (golden_rows.json), and the parse that
// scripts/rammp_rtps.py (used by rtps_selftest.py) makes of the header.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "selftest_spec.h"
#include "test_case.hpp"

namespace {

struct Row {
  const char *id;
  const char *name;
  const char *unit;
  int32_t lo;
  int32_t hi;
  const char *need;
  const char *why;
};

constexpr const char *need_name(int need) {
  return need == ST_REQUIRED   ? "required"
         : need == ST_OPTIONAL ? "optional"
         : need == ST_REMOTE   ? "remote"
                               : "?";
}

// The table as the firmware sees it: SELFTEST_TABLE expanded the way selftest.cpp does.
#define SST_ROW(id, name, unit, lo, hi, need, desc)                                                \
  Row{#id, name, unit, lo, hi, need_name(need), desc},
constexpr Row kFirmware[] = {SELFTEST_TABLE(SST_ROW)};
#undef SST_ROW

constexpr Row kGolden[] = {
#include "golden_rows.inc"
};

constexpr Row kScript[] = {
#include "script_rows.inc"
};

void expect_rows(std::span<const Row> want, std::span<const Row> got) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.size(), got.size(), "row count");
  for (std::size_t i = 0; i < want.size() && i < got.size(); ++i) {
    char where[96];
    std::snprintf(where, sizeof where, "row %zu (%s)", i, want[i].name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].id, got[i].id, where);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].name, got[i].name, where);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].unit, got[i].unit, where);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(want[i].lo, got[i].lo, where);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(want[i].hi, got[i].hi, where);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].need, got[i].need, where);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want[i].why, got[i].why, where);
  }
}

} // namespace

TEST_CASE("SST-001 the firmware's check table matches the golden rows, in order",
          "[selftest_spec]") {
  expect_rows(kGolden, kFirmware);
}

TEST_CASE("SST-002 the host scripts' parse of selftest_spec.h matches the golden rows, in order",
          "[selftest_spec]") {
  expect_rows(kGolden, kScript);
}
