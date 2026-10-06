// L1 golden tests for the screen text formatters (components/hmi_format, REQ-FMT-xx in its
// README). Every case walks a frozen golden table (goldens.hpp, recorded from the pre-move code
// with LVGL's own printf) through the code under test (subject.hpp). One behaviour per case.

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "goldens.hpp"
#include "specs.hpp"
#include "subject.hpp"
#include "test_case.hpp"

namespace {

using fmt_test::Row;
using fmt_test::Table;

const Row &row_of(Table table, uint8_t row) {
  switch (table) {
  case Table::SETTINGS:
    return fmt_test::SETTINGS_ROWS.at(row);
  case Table::ACTUATORS:
    return fmt_test::ACTUATOR_ROWS.at(row);
  case Table::SEAT:
    return fmt_test::SEAT_ROWS.at(row);
  case Table::DIAG:
    return fmt_test::DIAG_ROWS.at(row);
  case Table::DECIMALS:
    return fmt_test::DECIMALS_ROWS.at(row);
  }
  TEST_FAIL_MESSAGE("unknown table");
  return fmt_test::SETTINGS_ROWS.at(0);
}

// Says which golden row failed, so a failure points at its input.
struct Where {
  std::array<char, 96> text{};
  Where(const char *what, int table, int row, long long raw, std::size_t size) {
    std::snprintf(text.data(), text.size(), "%s table %d row %d raw %lld size %zu", what, table,
                  row, raw, size);
  }
  [[nodiscard]] const char *c_str() const { return text.data(); }
};

// Golden rows of one table at its caller's buffer size (the truncation rows are FMT-007's).
void check_stepper_table(Table table, std::size_t caller_size) {
  int checked = 0;
  for (const auto &g : fmt_test::STEPPER_GOLDEN) {
    if (g.table != table || g.out_size != caller_size) {
      continue;
    }
    std::array<char, 64> out{};
    sut::stepper_format(row_of(g.table, g.row), g.raw, out.data(), g.out_size);
    const Where where("stepper_format", static_cast<int>(g.table), g.row, g.raw, g.out_size);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, out.data(), where.c_str());
    ++checked;
  }
  TEST_ASSERT_GREATER_THAN_INT(0, checked);
}

} // namespace

TEST_CASE("FMT-001 a speed in m/s becomes the golden tenths of a mph, 0 for NaN, inf or <= 0",
          "[hmi_format][speed]") {
  for (const auto &g : fmt_test::SPEED_GOLDEN) {
    const Where where("speed_display_tenths", 0, 0, static_cast<long long>(g.tenths), 0);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(g.tenths, sut::speed_display_tenths(g.mps), where.c_str());
  }
}

TEST_CASE("FMT-002 the speed label reads N.N, clamped to 0.0..9.9, as the golden table says",
          "[hmi_format][speed]") {
  for (const auto &g : fmt_test::SPEED_TEXT_GOLDEN) {
    std::array<char, 64> out{};
    sut::speed_text(g.value, out.data(), out.size());
    const Where where("speed_text", 0, 0, g.value, out.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, out.data(), where.c_str());
  }
}

TEST_CASE("FMT-003 every settings row formats each raw value as the golden table says",
          "[hmi_format][stepper]") {
  check_stepper_table(Table::SETTINGS, fmt_test::SETTING_TEXT_SIZE);
}

TEST_CASE("FMT-004 every actuator row formats each raw value as the golden table says",
          "[hmi_format][stepper]") {
  check_stepper_table(Table::ACTUATORS, fmt_test::SETTING_TEXT_SIZE);
}

TEST_CASE("FMT-005 a diagnostics reading formats by its decimals as the golden table says",
          "[hmi_format][stepper]") {
  check_stepper_table(Table::DIAG, fmt_test::DIAG_TEXT_SIZE);
}

TEST_CASE("FMT-006 decimals 0 to 9 split a raw value with its sign as the golden table says",
          "[hmi_format][stepper]") {
  check_stepper_table(Table::DECIMALS, fmt_test::SETTING_TEXT_SIZE);
}

TEST_CASE("FMT-007 a stepper text longer than its buffer is cut and still terminated",
          "[hmi_format][stepper]") {
  int checked = 0;
  for (const auto &g : fmt_test::STEPPER_GOLDEN) {
    if (g.out_size >= fmt_test::DIAG_TEXT_SIZE) {
      continue;
    }
    std::array<char, 64> out{};
    out.fill('#');
    sut::stepper_format(row_of(g.table, g.row), g.raw, out.data(), g.out_size);
    const Where where("stepper_format", static_cast<int>(g.table), g.row, g.raw, g.out_size);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, out.data(), where.c_str());
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('#', out.at(g.out_size), where.c_str()); // nothing past it
    ++checked;
  }
  TEST_ASSERT_GREATER_THAN_INT(0, checked);
}

TEST_CASE("FMT-008 a zero-size buffer is left untouched", "[hmi_format][stepper]") {
  std::array<char, 8> out{};
  out.fill('#');
  sut::stepper_format(fmt_test::SETTINGS_ROWS.at(0), 50, out.data(), 0);
  sut::stepper_format(fmt_test::SETTINGS_ROWS.at(1), 1, out.data(), 0);
  sut::seat_format(fmt_test::SEAT_ROWS.at(0), 125, out.data(), 0);
  sut::seat_format(fmt_test::SEAT_ROWS.at(0), INT32_MIN, out.data(), 0);
  for (const char c : out) {
    TEST_ASSERT_EQUAL_CHAR('#', c);
  }
}

TEST_CASE("FMT-009 a seat value reads number, space, unit, and -- while unknown, as the golden "
          "table says",
          "[hmi_format][seat]") {
  for (const auto &g : fmt_test::SEAT_GOLDEN) {
    std::array<char, fmt_test::SEAT_TEXT_SIZE> out{};
    sut::seat_format(row_of(g.table, g.row), g.raw, out.data(), out.size());
    const Where where("seat_format", static_cast<int>(g.table), g.row, g.raw, out.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, out.data(), where.c_str());
  }
}

TEST_CASE("FMT-010 the seat adjustment page's reading and its of-footer match the golden table",
          "[hmi_format][seat]") {
  for (const auto &g : fmt_test::SEAT_ANGLE_GOLDEN) {
    char text[32] = {};
    char footer[40] = {};
    sut::seat_angle_texts(row_of(g.table, g.row), g.raw, text, footer);
    const Where where("seat_angle_texts", static_cast<int>(g.table), g.row, g.raw, 0);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, text, where.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.footer, footer, where.c_str());
  }
}

TEST_CASE("FMT-011 a clock is plausible from 2025 on and not before", "[hmi_format][clock]") {
  for (const auto &g : fmt_test::CLOCK_PLAUSIBLE_GOLDEN) {
    std::tm t{};
    t.tm_year = g.tm_year;
    const Where where("clock_plausible", 0, 0, g.tm_year, 0);
    TEST_ASSERT_EQUAL_MESSAGE(g.plausible, sut::clock_plausible(t), where.c_str());
  }
}

TEST_CASE("FMT-012 the clock reads HH:MM as the golden table says, cut to its 8-byte buffer",
          "[hmi_format][clock]") {
  for (const auto &g : fmt_test::CLOCK_TEXT_GOLDEN) {
    std::tm t{};
    t.tm_hour = g.hour;
    t.tm_min = g.minute;
    char text[8] = {};
    sut::clock_text(t, text);
    const Where where("clock_text", g.hour, g.minute, 0, sizeof(text));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, text, where.c_str());
  }
}

TEST_CASE("FMT-013 the link label reads BT and WI-FI on WiFi and BT and ETH on anything else",
          "[hmi_format][link]") {
  for (const auto &g : fmt_test::LINK_GOLDEN) {
    const Where where("link_text", 0, 0, g.link, 0);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, sut::link_text(g.link), where.c_str());
  }
}

TEST_CASE("FMT-014 the diagnostics rate reads N.N Hz - Live as the golden table says",
          "[hmi_format][diag]") {
  for (const auto &g : fmt_test::DIAG_RATE_GOLDEN) {
    std::array<char, 64> out{};
    sut::diag_rate_text(g.value, out.data(), out.size());
    const Where where("diag_rate_text", 0, 0, g.value, out.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, out.data(), where.c_str());
  }
}
