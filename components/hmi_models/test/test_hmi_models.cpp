// L1 golden tests for the joystick cursor walk of the button grids and the bench PIN entry.
// MOD-001 checks the firmware's grid shapes; MOD-002..MOD-006 walk the frozen golden tables
// (goldens.hpp, recorded from the pre-move code) through the code under test (subject.hpp);
// MOD-007 checks the oracle itself against them. One behaviour per case.

#include <array>
#include <cstdio>

#include "goldens.hpp"
#include "legacy_ui.hpp"
#include "pin_sequences.hpp"
#include "shapes.hpp"
#include "subject.hpp"
#include "test_case.hpp"

namespace {

using models_test::GridGolden;
using models_test::GridId;

// Says which golden row failed, so a failure points at its input.
struct Where {
  std::array<char, 96> text{};
  Where(const char *what, int a, int b, int c, int d) {
    std::snprintf(text.data(), text.size(), "%s %d %d %d %d", what, a, b, c, d);
  }
  [[nodiscard]] const char *c_str() const { return text.data(); }
};

void check_result(const GridGolden &g, models_test::Move move, int row, int col, bool focused) {
  const Where where("grid/row/col/key", static_cast<int>(g.grid), g.row, g.col,
                    static_cast<int>(g.key));
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(g.move), static_cast<int>(move), where.c_str());
  TEST_ASSERT_EQUAL_INT_MESSAGE(g.to_row, row, where.c_str());
  TEST_ASSERT_EQUAL_INT_MESSAGE(g.to_col, col, where.c_str());
  TEST_ASSERT_EQUAL_MESSAGE(g.focused, focused, where.c_str());
}

// Every golden row of one grid through the code under test.
void check_grid(GridId grid) {
  int checked = 0;
  for (const auto &g : models_test::GRID_GOLDEN) {
    if (g.grid != grid) {
      continue;
    }
    const sut::GridResult r = sut::grid_step(models_test::shape_of(grid), g.row, g.col, g.key);
    check_result(g, r.move, r.row, r.col, r.focused);
    ++checked;
  }
  // Every cursor in the 4 x 3 array, every arrow key.
  TEST_ASSERT_EQUAL_INT(models_test::MAX_ROWS * models_test::MAX_COLS * 4, checked);
}

} // namespace

TEST_CASE("MOD-001 the three grid shapes are the ones app_main builds: rows, lengths, holes",
          "[hmi_models][grid]") {
  for (const GridId id : models_test::GRID_IDS) {
    const models_test::Shape built = legacy::grid_shape(id);
    const models_test::Shape &table = models_test::shape_of(id);
    TEST_ASSERT_EQUAL_INT(table.rows, built.rows);
    TEST_ASSERT_EQUAL(table.left_edge_is_back, built.left_edge_is_back);
    for (int r = 0; r < models_test::MAX_ROWS; r++) {
      TEST_ASSERT_EQUAL_INT(table.cols[r], built.cols[r]);
      for (int c = 0; c < models_test::MAX_COLS; c++) {
        TEST_ASSERT_EQUAL(table.present[r][c], built.present[r][c]);
      }
    }
  }
}

TEST_CASE("MOD-002 the seat function page walks as the golden table says: clamped, down off "
          "the bottom is the burger key",
          "[hmi_models][grid]") {
  check_grid(GridId::SEAT_FUNCTIONS);
}

TEST_CASE("MOD-003 the seat adjustment page walks as the golden table says: left from the "
          "first column is back",
          "[hmi_models][grid]") {
  check_grid(GridId::SEAT_ADJUST);
}

TEST_CASE("MOD-004 the PIN pad walks as the golden table says: the hole left of 0 is stepped "
          "over",
          "[hmi_models][grid]") {
  check_grid(GridId::PIN_PAD);
}

TEST_CASE("MOD-005 every PIN key sequence shows the golden dots, line and verdict after each key",
          "[hmi_models][pin]") {
  sut::Pin pin;
  int checked = 0;
  for (const auto &g : models_test::PIN_GOLDEN) {
    sut::PinResult r{};
    if (g.key == 'R') {
      pin.reset();
      r = pin.now();
    } else {
      r = pin.key(g.key == 'B' ? -1 : g.key - '0');
    }
    const Where where("sequence/step/key", g.sequence, checked, g.key, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(g.dots, r.dots, where.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(g.wrong, r.wrong, where.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(g.opened, r.opened, where.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(g.typed, r.typed, where.c_str());
    ++checked;
  }
  TEST_ASSERT_GREATER_THAN_INT(0, checked);
}

TEST_CASE("MOD-006 the golden PIN table walks every sequence in pin_sequences.hpp, key by key",
          "[hmi_models][pin]") {
  int row = 0;
  for (int s = 0; s < static_cast<int>(std::size(models_test::PIN_SEQUENCES)); s++) {
    for (const char *p = models_test::PIN_SEQUENCES[s]; *p != '\0'; p++) {
      TEST_ASSERT_LESS_THAN_INT(static_cast<int>(std::size(models_test::PIN_GOLDEN)), row);
      const auto &g = models_test::PIN_GOLDEN[row];
      TEST_ASSERT_EQUAL_INT(s, g.sequence);
      TEST_ASSERT_EQUAL_CHAR(*p, g.key);
      ++row;
    }
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(std::size(models_test::PIN_GOLDEN)), row);
}

TEST_CASE("MOD-007 the oracle on the grids app_main builds agrees with the golden table",
          "[hmi_models][grid]") {
  for (const auto &g : models_test::GRID_GOLDEN) {
    const legacy::KeyResult r = legacy::grid_key(g.grid, g.row, g.col, g.key);
    check_result(g, r.move, r.row, r.col, r.focused);
  }
}
