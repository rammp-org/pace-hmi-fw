#pragma once
// The three button grids the firmware walks with the joystick, as test data. Copied from where
// app_main builds them (main/main.cpp at dev_refactor ad152dd); test MOD-001 checks
// this table against the grids the verbatim legacy code builds, so a copy error cannot hide.

#include <cstdint>

namespace models_test {

inline constexpr int MAX_ROWS = 4; // kGridMaxRows, main/frag_hold_poll.inc:105
inline constexpr int MAX_COLS = 3; // kGridMaxCols, main/frag_hold_poll.inc:106

enum class GridId : uint8_t { SEAT_FUNCTIONS, SEAT_ADJUST, PIN_PAD };
inline constexpr GridId GRID_IDS[] = {GridId::SEAT_FUNCTIONS, GridId::SEAT_ADJUST, GridId::PIN_PAD};

/// The four arrow keys grid_key_cb moves on (LV_KEY_UP/DOWN/LEFT/RIGHT).
enum class Key : uint8_t { UP, DOWN, LEFT, RIGHT };
inline constexpr Key KEYS[] = {Key::UP, Key::DOWN, Key::LEFT, Key::RIGHT};

/// What a key did: moved the cursor, or ran an edge handler instead.
enum class Move : uint8_t {
  MOVED,      ///< the cursor moved (or stayed, clamped)
  OFF_BOTTOM, ///< down from the bottom row: nav_to_key(), the burger key
  OFF_LEFT,   ///< left from the first column of the adjustment page: seat_show_buttons_page()
};

struct Shape {
  int rows;
  int cols[MAX_ROWS];               // buttons in each row
  bool present[MAX_ROWS][MAX_COLS]; // false = a hole (a null cell)
  bool left_edge_is_back;           // grid_key_cb's `g == &seat_adjust_grid`
};

// main/main.cpp:827-836, seat_buttons_grid: three rows of two.
inline constexpr Shape SEAT_FUNCTIONS_SHAPE{
    .rows = 3,
    .cols = {2, 2, 2, 0},
    .present = {{true, true, false}, {true, true, false}, {true, true, false}, {}},
    .left_edge_is_back = false,
};

// main/main.cpp:839-848, seat_adjust_grid: the back button, "-" and "+", three presets.
inline constexpr Shape SEAT_ADJUST_SHAPE{
    .rows = 3,
    .cols = {1, 2, 3, 0},
    .present = {{true, false, false}, {true, true, false}, {true, true, true}, {}},
    .left_edge_is_back = true,
};

// main/main.cpp:1101-1121, rd_grid: 1-9 in three rows, then a hole, 0 and backspace.
inline constexpr Shape PIN_PAD_SHAPE{
    .rows = 4,
    .cols = {3, 3, 3, 3},
    .present = {{true, true, true}, {true, true, true}, {true, true, true}, {false, true, true}},
    .left_edge_is_back = false,
};

constexpr const Shape &shape_of(GridId id) {
  switch (id) {
  case GridId::SEAT_FUNCTIONS:
    return SEAT_FUNCTIONS_SHAPE;
  case GridId::SEAT_ADJUST:
    return SEAT_ADJUST_SHAPE;
  case GridId::PIN_PAD:
    return PIN_PAD_SHAPE;
  }
  return SEAT_FUNCTIONS_SHAPE;
}

} // namespace models_test
