// Verbatim copies of the pre-move cursor walk and PIN entry (see legacy_ui.hpp). Each body is
// the source named above it, character for character; the LVGL calls it makes resolve to the
// recording stand-ins at the top of this file, and the screen calls it makes (nav_to_key,
// seat_show_buttons_page, setting_page_open) only count that they ran.

#include "legacy_ui.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace legacy {
namespace {

// --- stand-ins for LVGL and for the rest of main ----------------------------------------------

struct lv_obj_t {
  int id;
};
struct lv_event_t {
  void *user_data;
  lv_obj_t *target;
  uint32_t key;
};
struct lv_subject_t {
  int32_t num;
  char text[32];
};

// LVGL's key codes (managed_components/lvgl__lvgl/src/core/lv_group.h:26-33).
enum : uint32_t {
  LV_KEY_UP = 17,
  LV_KEY_DOWN = 18,
  LV_KEY_RIGHT = 19,
  LV_KEY_LEFT = 20,
};

struct Recorder {
  const lv_obj_t *focused = nullptr;
  int nav_to_key = 0;
  int back = 0;
  int pages_opened = 0;
  int first_dots = -1;
};
Recorder rec;

void *lv_event_get_user_data(const lv_event_t *e) { return e->user_data; }
lv_obj_t *lv_event_get_target_obj(const lv_event_t *e) { return e->target; }
uint32_t lv_event_get_key(const lv_event_t *e) { return e->key; }
void lv_group_focus_obj(const lv_obj_t *obj) { rec.focused = obj; }
void nav_to_key() { rec.nav_to_key++; }
void seat_show_buttons_page() { rec.back++; }

int32_t lv_subject_get_int(const lv_subject_t *s) { return s->num; }
void lv_subject_set_int(lv_subject_t *s, int32_t value) {
  if (rec.first_dots < 0) {
    rec.first_dots = static_cast<int>(value);
  }
  s->num = value;
}
void lv_subject_copy_string(lv_subject_t *s, const char *text) {
  std::strncpy(s->text, text, sizeof(s->text) - 1);
  s->text[sizeof(s->text) - 1] = '\0';
}
int lv_strcmp(const char *a, const char *b) { return std::strcmp(a, b); }
constexpr int kActuatorsPage = 3; // its value is not looked at
void setting_page_open(int /*page*/) { rec.pages_opened++; }

// main/frag_hold_poll.inc:105-106
static constexpr int kGridMaxRows = 4; // the PIN pad's bottom row is the fourth
static constexpr int kGridMaxCols = 3;

// --- main/frag_seat.inc:6-15 -------------------------------------------------------------------
struct ButtonGrid {
  lv_obj_t *cell[kGridMaxRows][kGridMaxCols];
  int cols[kGridMaxRows]; // buttons in each row; rows may differ in length
  int rows;
  int row; // cursor
  int col;
};

static ButtonGrid seat_buttons_grid;
static ButtonGrid seat_adjust_grid;

// --- main/frag_seat.inc:51-106 -----------------------------------------------------------------
// Changed from the source for cppcheck (constParameterPointer): the parameter is pointer to
// const; the body is unchanged and does not write through it.
static void grid_key_cb(const lv_event_t *e) {
  auto *g = static_cast<ButtonGrid *>(lv_event_get_user_data(e));
  // The cursor is whichever cell holds focus, not wherever it was last left:
  // coming back up from the burger key, the group put focus on a cell without
  // telling the grid.
  const lv_obj_t *here = lv_event_get_target_obj(e);
  for (int r = 0; r < g->rows; r++) {
    for (int c = 0; c < g->cols[r]; c++) {
      if (g->cell[r][c] == here) {
        g->row = r;
        g->col = c;
      }
    }
  }
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    g->row--;
    break;
  case LV_KEY_DOWN:
    if (g->row + 1 >= g->rows) {
      nav_to_key(); // below the bottom row is the burger key
      return;
    }
    g->row++;
    break;
  case LV_KEY_LEFT:
    // Left off the adjustment page's left edge is "back", as "<" is.
    if (g == &seat_adjust_grid && g->col == 0) {
      seat_show_buttons_page();
      return;
    }
    g->col--;
    break;
  case LV_KEY_RIGHT:
    g->col++;
    break;
  default:
    return;
  }
  // Clamp rather than wrap. On a control surface you steer by feel, and running
  // off one edge to reappear at the opposite one is disorienting. The column is
  // re-clamped after a row change too, since rows can be different lengths.
  g->row = std::clamp(g->row, 0, g->rows - 1);
  g->col = std::clamp(g->col, 0, g->cols[g->row] - 1);
  // Step over a hole in the row, outward first and then back, so the cursor
  // lands on the nearest real button whichever edge it came from.
  while (g->cell[g->row][g->col] == nullptr && g->col < g->cols[g->row] - 1) {
    g->col++;
  }
  while (g->cell[g->row][g->col] == nullptr && g->col > 0) {
    g->col--;
  }
  if (g->cell[g->row][g->col] != nullptr) {
    lv_group_focus_obj(g->cell[g->row][g->col]);
  }
}

// --- main/frag_seat.inc:110-120 ----------------------------------------------------------------
// Changed from the source for cppcheck (constParameterPointer): the parameter is pointer to
// const; the body is unchanged and does not write through it.
static void grid_sync_cursor(ButtonGrid *g, const lv_obj_t *button) {
  for (int r = 0; r < g->rows; r++) {
    for (int c = 0; c < g->cols[r]; c++) {
      if (g->cell[r][c] != nullptr && g->cell[r][c] == button) {
        g->row = r;
        g->col = c;
      }
    }
  }
  lv_group_focus_obj(button);
}

// --- main/frag_bench_pin.inc:21-47 -------------------------------------------------------------
static constexpr char kRdPin[] = "1234";
static constexpr int kRdPinLen = sizeof(kRdPin) - 1;
static_assert(kRdPinLen == 4, "the bench gate draws exactly four PIN dots");

static constexpr int kRdBack = -1;
static ButtonGrid rd_grid;

static char rd_pin_entry[kRdPinLen + 1];
static lv_subject_t rd_pin_len_subject;

static constexpr char kRdPinPromptText[] = "Enter PIN to proceed";
static constexpr char kRdPinWrongText[] = "Incorrect PIN - try again";
static lv_subject_t rd_pin_message_subject;

// --- main/frag_bench_pin.inc:52-56 -------------------------------------------------------------
static void rd_pin_reset() {
  rd_pin_entry[0] = '\0';
  lv_subject_set_int(&rd_pin_len_subject, 0);
  lv_subject_copy_string(&rd_pin_message_subject, kRdPinPromptText);
}

// --- main/frag_bench_pin.inc:70-105 ------------------------------------------------------------
// Changed from the source for cppcheck (constParameterPointer): the parameter is pointer to
// const; the body is unchanged and does not write through it.
static void rd_keypad_cb(const lv_event_t *e) {
  const int digit = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  // Touch moves the cursor too, so the joystick carries on from where a finger
  // last landed rather than from where the stick left off.
  grid_sync_cursor(&rd_grid, lv_event_get_target_obj(e));
  int len = lv_subject_get_int(&rd_pin_len_subject);

  if (digit == kRdBack) {
    if (len > 0) {
      rd_pin_entry[--len] = '\0';
      lv_subject_set_int(&rd_pin_len_subject, len);
    }
    return;
  }

  rd_pin_entry[len++] = static_cast<char>('0' + digit);
  rd_pin_entry[len] = '\0';
  lv_subject_set_int(&rd_pin_len_subject, len);
  // Typing clears a previous rejection, so the line reads as a prompt for the
  // entry in progress rather than a verdict on the last one.
  lv_subject_copy_string(&rd_pin_message_subject, kRdPinPromptText);
  if (len < kRdPinLen) {
    return;
  }

  // Judged on the fourth digit rather than on an OK key: the row of dots makes
  // the length obvious, so a confirm step would add nothing. Either branch
  // empties the entry, which is what keeps `len` below kRdPinLen above.
  const bool correct = lv_strcmp(rd_pin_entry, kRdPin) == 0;
  rd_pin_reset();
  if (correct) {
    setting_page_open(kActuatorsPage);
    return;
  }
  lv_subject_copy_string(&rd_pin_message_subject, kRdPinWrongText);
}

// --- the grids, built as app_main builds them --------------------------------------------------

// One stand-in object per button, named after the export's widget. Their ids are not looked at.
lv_obj_t ui_SeatButton1{1}, ui_SeatButton2{2}, ui_SeatButton4{4}, ui_SeatButton7{7},
    ui_SeatButton5{5}, ui_SeatButton6{6};
lv_obj_t ui_SeatBackButton{10}, ui_SeatAdjustmentButton1{11}, ui_SeatAdjustmentButton2{12},
    ui_SeatAdjustmentButton3{13}, ui_SeatAdjustmentButton4{14}, ui_SeatAdjustmentButton5{15};
lv_obj_t ui_BenchKey1{21}, ui_BenchKey2{22}, ui_BenchKey3{23}, ui_BenchKey4{24}, ui_BenchKey5{25},
    ui_BenchKey6{26}, ui_BenchKey7{27}, ui_BenchKey8{28}, ui_BenchKey9{29}, ui_BenchKey0{20},
    ui_BenchKeyBack{30};
lv_obj_t outside_the_grid{99}; // focus somewhere else: a hole has no object to hold it

void build_grids() {
  // Static, so zero before app_main fills them in (and grid_key_on may have used one).
  seat_buttons_grid = ButtonGrid{};
  seat_adjust_grid = ButtonGrid{};
  rd_grid = ButtonGrid{};
  // main/main.cpp:827-836
  seat_buttons_grid.rows = 3;
  seat_buttons_grid.cols[0] = 2;
  seat_buttons_grid.cell[0][0] = &ui_SeatButton1; // Elevation
  seat_buttons_grid.cell[0][1] = &ui_SeatButton2; // Backseat
  seat_buttons_grid.cols[1] = 2;
  seat_buttons_grid.cell[1][0] = &ui_SeatButton4; // Side Tilt
  seat_buttons_grid.cell[1][1] = &ui_SeatButton7; // Setback
  seat_buttons_grid.cols[2] = 2;
  seat_buttons_grid.cell[2][0] = &ui_SeatButton5; // Static
  seat_buttons_grid.cell[2][1] = &ui_SeatButton6; // Dynamic

  // main/main.cpp:839-848
  seat_adjust_grid.rows = 3;
  seat_adjust_grid.cols[0] = 1;
  seat_adjust_grid.cell[0][0] = &ui_SeatBackButton;
  seat_adjust_grid.cols[1] = 2;
  seat_adjust_grid.cell[1][0] = &ui_SeatAdjustmentButton1;
  seat_adjust_grid.cell[1][1] = &ui_SeatAdjustmentButton2;
  seat_adjust_grid.cols[2] = 3;
  seat_adjust_grid.cell[2][0] = &ui_SeatAdjustmentButton3;
  seat_adjust_grid.cell[2][1] = &ui_SeatAdjustmentButton4;
  seat_adjust_grid.cell[2][2] = &ui_SeatAdjustmentButton5;

  // main/main.cpp:1101-1121 (the grid part; the LVGL wiring is not the walk's)
  rd_grid.rows = 4;
  lv_obj_t *rd_keys[4][kGridMaxCols] = {
      {&ui_BenchKey1, &ui_BenchKey2, &ui_BenchKey3},
      {&ui_BenchKey4, &ui_BenchKey5, &ui_BenchKey6},
      {&ui_BenchKey7, &ui_BenchKey8, &ui_BenchKey9},
      {nullptr, &ui_BenchKey0, &ui_BenchKeyBack},
  };
  for (int r = 0; r < rd_grid.rows; r++) {
    rd_grid.cols[r] = kGridMaxCols;
    for (int c = 0; c < kGridMaxCols; c++) {
      lv_obj_t *key = rd_keys[r][c];
      rd_grid.cell[r][c] = key;
    }
  }
}

ButtonGrid &grid_of(models_test::GridId id) {
  build_grids();
  switch (id) {
  case models_test::GridId::SEAT_FUNCTIONS:
    return seat_buttons_grid;
  case models_test::GridId::SEAT_ADJUST:
    return seat_adjust_grid;
  case models_test::GridId::PIN_PAD:
    return rd_grid;
  }
  return seat_buttons_grid;
}

uint32_t lv_key_of(models_test::Key key) {
  switch (key) {
  case models_test::Key::UP:
    return LV_KEY_UP;
  case models_test::Key::DOWN:
    return LV_KEY_DOWN;
  case models_test::Key::LEFT:
    return LV_KEY_LEFT;
  case models_test::Key::RIGHT:
    return LV_KEY_RIGHT;
  }
  return 0;
}

KeyResult press(ButtonGrid &g, int row, int col, models_test::Key key) {
  g.row = row;
  g.col = col;
  lv_obj_t *here = g.cell[row][col] != nullptr ? g.cell[row][col] : &outside_the_grid;
  rec = Recorder{};
  lv_event_t e{&g, here, lv_key_of(key)};
  grid_key_cb(&e);
  KeyResult out{models_test::Move::MOVED, g.row, g.col, rec.focused != nullptr};
  if (rec.nav_to_key > 0) {
    out.move = models_test::Move::OFF_BOTTOM;
  } else if (rec.back > 0) {
    out.move = models_test::Move::OFF_LEFT;
  }
  // When focus moved, it moved to the cursor's cell.
  if (out.focused && rec.focused != g.cell[g.row][g.col]) {
    out.focused = false;
    out.row = -1;
  }
  return out;
}

// Stand-ins for any shape's present cells (grid_key_on).
lv_obj_t any_cell[kGridMaxRows][kGridMaxCols];
ButtonGrid other_grid;

PinState state_now() {
  return PinState{static_cast<int>(rd_pin_len_subject.num),
                  std::strcmp(rd_pin_message_subject.text, kRdPinWrongText) == 0, rec.pages_opened,
                  rec.first_dots};
}

} // namespace

KeyResult grid_key(models_test::GridId grid, int row, int col, models_test::Key key) {
  return press(grid_of(grid), row, col, key);
}

models_test::Shape grid_shape(models_test::GridId grid) {
  const ButtonGrid &g = grid_of(grid);
  models_test::Shape shape{};
  shape.rows = g.rows;
  for (int r = 0; r < kGridMaxRows; r++) {
    shape.cols[r] = g.cols[r];
    for (int c = 0; c < kGridMaxCols; c++) {
      shape.present[r][c] = g.cell[r][c] != nullptr;
    }
  }
  shape.left_edge_is_back = &g == &seat_adjust_grid;
  return shape;
}

KeyResult grid_key_on(const models_test::Shape &shape, int row, int col, models_test::Key key) {
  ButtonGrid &g = shape.left_edge_is_back ? seat_adjust_grid : other_grid;
  g = ButtonGrid{};
  g.rows = shape.rows;
  for (int r = 0; r < kGridMaxRows; r++) {
    g.cols[r] = shape.cols[r];
    for (int c = 0; c < kGridMaxCols; c++) {
      g.cell[r][c] = shape.present[r][c] ? &any_cell[r][c] : nullptr;
    }
  }
  return press(g, row, col, key);
}

void pin_reset() {
  build_grids();
  rec = Recorder{};
  rd_pin_reset();
}

PinState pin_key(int digit_or_back) {
  const int pages = rec.pages_opened;
  rec = Recorder{};
  rec.pages_opened = pages;
  // The key's own button: any pad key will do, the cursor sync is not under test here.
  lv_event_t e{reinterpret_cast<void *>(static_cast<intptr_t>(digit_or_back)), &ui_BenchKey5, 0};
  rd_keypad_cb(&e);
  return state_now();
}

PinState pin_state() {
  PinState s = state_now();
  s.first_dots = -1;
  return s;
}

const char *pin_constant() { return kRdPin; }

} // namespace legacy
