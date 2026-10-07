// L1 golden tests for the screen text formatters (components/hmi_format, REQ-FMT-xx in its
// README). FMT-001..014 walk a frozen golden table (goldens.hpp, recorded from the pre-move code
// with LVGL's own printf) through the code under test (subject.hpp); FMT-015..020 compare the
// code under test with the pre-move code itself over wide sweeps. One behaviour per case.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "goldens.hpp"
#include "legacy_format.hpp"
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

// --- Differential: the component against the pre-move code (legacy_format.cpp, printing with
// LVGL's own lv_snprintf), byte for byte, over far more inputs than the goldens hold. ---

namespace {

constexpr std::size_t PROBE_SIZE = 48;   // larger than any text here
constexpr std::size_t CUT_SIZE_MAX = 24; // the cut is checked at every size up to this

using Probe = std::array<char, PROBE_SIZE>;

Probe fresh() {
  Probe p{};
  p.fill('#'); // a byte the formatters never write, so a stray write shows
  return p;
}

// Compares the whole probe buffers, not only up to the terminator.
void check_stepper_same(const Row &row, int32_t raw, std::size_t size) {
  Probe want = fresh();
  Probe got = fresh();
  legacy::stepper_format(fmt_test::make_spec<legacy::StepperSpec>(row), raw, want.data(), size);
  sut::stepper_format(row, raw, got.data(), size);
  const Where where("stepper_format vs legacy", row.decimals, row.min_value, raw, size);
  TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(want.data(), got.data(), want.size(), where.c_str());
}

void check_seat_same(const Row &row, int32_t raw, std::size_t size) {
  Probe want = fresh();
  Probe got = fresh();
  legacy::seat_format(fmt_test::make_spec<legacy::StepperSpec>(row), raw, want.data(), size);
  sut::seat_format(row, raw, got.data(), size);
  const Where where("seat_format vs legacy", row.decimals, row.min_value, raw, size);
  TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(want.data(), got.data(), want.size(), where.c_str());

  char want_text[32];
  char want_footer[40];
  char got_text[32];
  char got_footer[40];
  std::memset(want_text, '#', sizeof(want_text));
  std::memset(want_footer, '#', sizeof(want_footer));
  std::memset(got_text, '#', sizeof(got_text));
  std::memset(got_footer, '#', sizeof(got_footer));
  legacy::seat_angle_texts(fmt_test::make_spec<legacy::StepperSpec>(row), raw, want_text,
                           want_footer);
  sut::seat_angle_texts(row, raw, got_text, got_footer);
  TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(want_text, got_text, sizeof(want_text), where.c_str());
  TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(want_footer, got_footer, sizeof(want_footer), where.c_str());
}

template <std::size_t N> void sweep_rows(const std::array<Row, N> &rows) {
  constexpr int32_t MARGIN = 1000;
  for (const Row &row : rows) {
    for (int32_t raw = row.min_value - MARGIN; raw <= row.max_value + MARGIN; ++raw) {
      check_stepper_same(row, raw, fmt_test::SETTING_TEXT_SIZE);
      check_seat_same(row, raw, fmt_test::SEAT_TEXT_SIZE);
    }
  }
}

} // namespace

TEST_CASE("FMT-015 every row formats every raw value within 1000 of its range as before the move",
          "[hmi_format][stepper][seat]") {
  sweep_rows(fmt_test::SETTINGS_ROWS);
  sweep_rows(fmt_test::ACTUATOR_ROWS);
  sweep_rows(fmt_test::SEAT_ROWS);
  sweep_rows(fmt_test::DIAG_ROWS);
}

TEST_CASE("FMT-016 decimals 0 to 9 format raw values across the int32 range as before the move",
          "[hmi_format][stepper]") {
  constexpr int64_t STRIDE = 104729; // a prime, so the last digits vary
  for (const Row &row : fmt_test::DECIMALS_ROWS) {
    for (int64_t raw = INT32_MIN + 1; raw <= INT32_MAX; raw += STRIDE) {
      check_stepper_same(row, static_cast<int32_t>(raw), fmt_test::SETTING_TEXT_SIZE);
    }
    check_stepper_same(row, INT32_MAX, fmt_test::SETTING_TEXT_SIZE);
  }
}

TEST_CASE("FMT-017 a text is cut at every buffer size exactly as LVGL's printf cut it",
          "[hmi_format][stepper][seat]") {
  for (std::size_t size = 0; size <= CUT_SIZE_MAX; ++size) {
    for (const Row &row : fmt_test::SETTINGS_ROWS) {
      for (const int32_t raw : {row.min_value - 1, row.min_value, row.max_value, INT32_MAX}) {
        check_stepper_same(row, raw, size);
      }
    }
    for (const Row &row : fmt_test::SEAT_ROWS) {
      for (const int32_t raw : {INT32_MIN, row.min_value, row.max_value, INT32_MIN + 1}) {
        check_seat_same(row, raw, size);
      }
    }
    for (const Row &row : fmt_test::DECIMALS_ROWS) {
      check_stepper_same(row, INT32_MIN + 1, size);
    }
  }
}

TEST_CASE("FMT-018 every 4096th float bit pattern gives the same speed as before the move",
          "[hmi_format][speed]") {
  constexpr uint64_t STRIDE = 4096;
  for (uint64_t bits = 0; bits <= UINT32_MAX; bits += STRIDE) {
    float mps = 0.0f;
    const auto pattern = static_cast<uint32_t>(bits);
    std::memcpy(&mps, &pattern, sizeof(mps));
    const Where where("speed_display_tenths vs legacy", 0, 0, static_cast<long long>(bits), 0);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(legacy::speed_display_tenths(mps),
                                    sut::speed_display_tenths(mps), where.c_str());
  }
}

TEST_CASE("FMT-019 the speed, rate and clock texts read as before the move over a wide sweep",
          "[hmi_format][speed][diag][clock]") {
  for (int32_t v = -20000; v <= 20000; ++v) {
    Probe want = fresh();
    Probe got = fresh();
    legacy::speed_label_text(v, want.data(), want.size());
    sut::speed_text(v, got.data(), got.size());
    const Where where("speed_text vs legacy", 0, 0, v, want.size());
    TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(want.data(), got.data(), want.size(), where.c_str());

    want = fresh();
    got = fresh();
    legacy::diag_rate_text(v, want.data(), want.size());
    sut::diag_rate_text(v, got.data(), got.size());
    TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(want.data(), got.data(), want.size(), where.c_str());
  }
  for (int hour = -150; hour <= 150; ++hour) {
    for (int minute = -150; minute <= 150; ++minute) {
      std::tm t{};
      t.tm_hour = hour;
      t.tm_min = minute;
      char want[8];
      char got[8];
      std::memset(want, '#', sizeof(want));
      std::memset(got, '#', sizeof(got));
      legacy::clock_text(t, want);
      sut::clock_text(t, got);
      const Where where("clock_text vs legacy", hour, minute, 0, sizeof(want));
      TEST_ASSERT_EQUAL_CHAR_ARRAY_MESSAGE(want, got, sizeof(want), where.c_str());
    }
  }
}

TEST_CASE("FMT-020 clock_plausible and link_text agree with the pre-move code for every input "
          "tried",
          "[hmi_format][clock][link]") {
  for (int year = -3000; year <= 3000; ++year) {
    std::tm t{};
    t.tm_year = year;
    TEST_ASSERT_EQUAL(legacy::clock_plausible(t), sut::clock_plausible(t));
  }
  for (unsigned v = 0; v <= UINT8_MAX; ++v) {
    const auto link = static_cast<uint8_t>(v);
    TEST_ASSERT_EQUAL_STRING(legacy::link_text(static_cast<legacy::NetLink>(link)),
                             sut::link_text(link));
  }
}
