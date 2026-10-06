// L1 golden tests for the joystick cursor walk of the button grids and the bench PIN entry.
// MOD-001 checks the firmware's grid shapes; MOD-002..MOD-006 walk the frozen golden tables
// (goldens.hpp, recorded from the pre-move code) through the code under test (subject.hpp);
// MOD-007 checks the oracle itself against them; MOD-008/009 compare the component with the
// oracle over every shape and long seeded key streams; MOD-010..012 pin the model's own
// interface. One behaviour per case.

#include <array>
#include <cstdint>
#include <cstdio>
#include <random>

#include "goldens.hpp"
#include "hmi_models/grid.hpp"
#include "hmi_models/pin.hpp"
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

namespace {

// Calls fn(shape) for every shape that fits the 4 x 3 array: 1..4 rows, each 1..3 long, every
// hole pattern inside each row, and both kinds of left edge.
template <typename Fn> int for_every_shape(Fn fn) {
  int shapes = 0;
  // Per row: a length 1..3 and a hole mask over it, as one index 0..13.
  constexpr int ROW_KINDS = 2 + 4 + 8;
  auto row_kind = [](int kind, int &len, unsigned &mask) {
    len = kind < 2 ? 1 : kind < 6 ? 2 : 3;
    const int first = len == 1 ? 0 : len == 2 ? 2 : 6;
    mask = static_cast<unsigned>(kind - first);
  };
  for (int rows = 1; rows <= models_test::MAX_ROWS; rows++) {
    int combos = 1;
    for (int r = 0; r < rows; r++) {
      combos *= ROW_KINDS;
    }
    for (int combo = 0; combo < combos; combo++) {
      for (const bool back : {false, true}) {
        models_test::Shape shape{};
        shape.rows = rows;
        shape.left_edge_is_back = back;
        int rest = combo;
        for (int r = 0; r < rows; r++) {
          int len = 0;
          unsigned mask = 0;
          row_kind(rest % ROW_KINDS, len, mask);
          rest /= ROW_KINDS;
          shape.cols[r] = len;
          for (int c = 0; c < len; c++) {
            shape.present[r][c] = ((mask >> static_cast<unsigned>(c)) & 1U) != 0U;
          }
        }
        fn(shape);
        ++shapes;
      }
    }
  }
  return shapes;
}

} // namespace

TEST_CASE("MOD-008 the component walks every shape up to 4 x 3 exactly as the pre-move code, "
          "from every cursor with every key",
          "[hmi_models][grid]") {
  long steps = 0;
  const int shapes = for_every_shape([&steps](const models_test::Shape &shape) {
    for (int r = 0; r < models_test::MAX_ROWS; r++) {
      for (int c = 0; c < models_test::MAX_COLS; c++) {
        for (const models_test::Key key : models_test::KEYS) {
          const legacy::KeyResult want = legacy::grid_key_on(shape, r, c, key);
          const sut::GridResult got = sut::grid_step(shape, r, c, key);
          if (static_cast<int>(want.move) != static_cast<int>(got.move) || want.row != got.row ||
              want.col != got.col || want.focused != got.focused) {
            const Where where("row/col/key/rows", r, c, static_cast<int>(key), shape.rows);
            TEST_FAIL_MESSAGE(where.c_str());
          }
          ++steps;
        }
      }
    }
  });
  // (14 + 14^2 + 14^3 + 14^4) row patterns, two kinds of left edge.
  TEST_ASSERT_EQUAL_INT(2 * (14 + 196 + 2744 + 38416), shapes);
  TEST_ASSERT_EQUAL_INT(shapes * 12 * 4, static_cast<int>(steps));
}

TEST_CASE("MOD-009 the component shows what the pre-move code showed over a seeded stream of "
          "50000 digit, backspace and visit keys",
          "[hmi_models][pin]") {
  std::mt19937 rng(20261006U); // fixed: the same stream every run (TS-DET)
  std::uniform_int_distribution<int> pick(0, 19);
  int accepted = 0;
  int rejected = 0;
  sut::Pin pin;
  pin.reset();
  legacy::pin_reset();
  for (int i = 0; i < 50000; i++) {
    // 0..9 a digit, 10..13 backspace (often enough to empty the entry), 14 a visit, and 15..19
    // the PIN's own next digit, so acceptances happen too.
    const int k = pick(rng);
    sut::PinResult got{};
    legacy::PinState want{};
    if (k == 14) {
      pin.reset();
      legacy::pin_reset();
      got = pin.now();
      want = legacy::pin_state();
    } else {
      int digit = k; // 0..9
      if (k >= 10 && k < 14) {
        digit = -1;
      } else if (k >= 15) {
        digit = legacy::pin_constant()[pin.now().dots] - '0';
      }
      const int opened_before = pin.now().opened;
      got = pin.key(digit);
      want = legacy::pin_key(digit);
      accepted += got.opened > opened_before ? 1 : 0;
      rejected += got.wrong && got.typed == 4 ? 1 : 0;
    }
    const Where where("key/index/dots", k, i, got.dots, want.dots);
    TEST_ASSERT_EQUAL_INT_MESSAGE(want.dots, got.dots, where.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(want.wrong, got.wrong, where.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(want.pages_opened, got.opened, where.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(want.first_dots, got.typed, where.c_str());
  }
  // The stream reached both verdicts, many times.
  TEST_ASSERT_GREATER_THAN_INT(20, accepted);
  TEST_ASSERT_GREATER_THAN_INT(20, rejected);
}

TEST_CASE("MOD-010 the PIN comes from the Config: another PIN is accepted, and a PIN of another "
          "length never is",
          "[hmi_models][pin]") {
  hmi::ui::PinModel zeros({.pin = "0000"});
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL(hmi::ui::PinVerdict::INCOMPLETE, zeros.press(0).verdict);
  }
  TEST_ASSERT_EQUAL(hmi::ui::PinVerdict::ACCEPTED, zeros.press(0).verdict);
  for (const char *digit = "1234"; *digit != '\0'; digit++) {
    (void)zeros.press(*digit - '0');
  }
  TEST_ASSERT_EQUAL(hmi::ui::PinMessage::WRONG, zeros.message());
  for (const char *pin : {"123", "12345", ""}) {
    hmi::ui::PinModel odd({.pin = pin});
    hmi::ui::PinPress last{};
    for (const char *digit = "1234"; *digit != '\0'; digit++) {
      last = odd.press(*digit - '0');
    }
    TEST_ASSERT_EQUAL(hmi::ui::PinVerdict::REJECTED, last.verdict);
  }
}

TEST_CASE("MOD-011 a press reports the digits typed before its verdict, and a verdict empties "
          "the entry",
          "[hmi_models][pin]") {
  hmi::ui::PinModel pin({.pin = "1234"});
  for (int n = 1; n <= 3; n++) {
    const hmi::ui::PinPress p = pin.press(9);
    TEST_ASSERT_EQUAL_INT(n, p.typed);
    TEST_ASSERT_EQUAL(hmi::ui::PinVerdict::INCOMPLETE, p.verdict);
    TEST_ASSERT_EQUAL_INT(n, pin.digits());
  }
  const hmi::ui::PinPress p = pin.press(9);
  TEST_ASSERT_EQUAL_INT(4, p.typed);
  TEST_ASSERT_EQUAL(hmi::ui::PinVerdict::REJECTED, p.verdict);
  TEST_ASSERT_EQUAL_INT(0, pin.digits());
  TEST_ASSERT_EQUAL(hmi::ui::PinMessage::WRONG, pin.message());
}

TEST_CASE("MOD-012 backspace reports false on an empty entry and leaves the line as it is",
          "[hmi_models][pin]") {
  hmi::ui::PinModel pin({.pin = "1234"});
  TEST_ASSERT_FALSE(pin.backspace());
  for (int i = 0; i < 4; i++) {
    (void)pin.press(5);
  }
  TEST_ASSERT_EQUAL(hmi::ui::PinMessage::WRONG, pin.message());
  TEST_ASSERT_FALSE(pin.backspace());
  TEST_ASSERT_EQUAL(hmi::ui::PinMessage::WRONG, pin.message());
  (void)pin.press(1);
  TEST_ASSERT_TRUE(pin.backspace());
  TEST_ASSERT_EQUAL_INT(0, pin.digits());
  TEST_ASSERT_EQUAL(hmi::ui::PinMessage::PROMPT, pin.message());
  pin.reset();
  TEST_ASSERT_EQUAL(hmi::ui::PinMessage::PROMPT, pin.message());
}
