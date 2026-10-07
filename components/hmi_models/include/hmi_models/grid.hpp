#pragma once
// The joystick cursor on a page of buttons laid out as a grid (REQ-MOD-01..04, README).

#include <array>
#include <cstddef>
#include <cstdint>

namespace hmi::ui {

inline constexpr int GRID_MAX_ROWS = 4; // the PIN pad's bottom row is the fourth
inline constexpr int GRID_MAX_COLS = 3;

/// A page's buttons in visual order. Rows may differ in length, and a row may have holes:
/// cells inside its length with no button (the PIN pad's bottom row starts at the middle
/// column).
struct GridShape {
  int rows = 0;                          ///< 1..GRID_MAX_ROWS
  std::array<int, GRID_MAX_ROWS> cols{}; ///< buttons in each row, 1..GRID_MAX_COLS
  std::array<std::array<bool, GRID_MAX_COLS>, GRID_MAX_ROWS> button{}; ///< false = a hole
  bool left_edge_is_back = false; ///< left from the first column leaves the page
};

struct GridCursor {
  int row = 0;
  int col = 0;
  friend constexpr bool operator==(const GridCursor &, const GridCursor &) = default;
};

/// The four arrow keys the cursor moves on.
enum class GridKey : uint8_t { UP, DOWN, LEFT, RIGHT };

/// What a key did.
enum class GridMove : uint8_t {
  MOVED,      ///< the cursor moved, or stayed where a clamp held it
  OFF_BOTTOM, ///< down from the bottom row: the cursor leaves the grid (to the burger key)
  OFF_LEFT,   ///< left from the first column of a grid whose left edge is "back"
};

struct GridStep {
  GridMove move = GridMove::MOVED;
  GridCursor cursor;      ///< where the cursor is now; unchanged unless move is MOVED
  bool on_button = false; ///< MOVED and the cursor is on a button, which takes focus
};

/// One arrow key from `from`. UP/DOWN/LEFT/RIGHT step one cell, then the cursor is clamped
/// (never wrapped) into the shape, the column after the row since rows differ in length, and
/// a hole is stepped over: outward to the right first, then back to the left.
/// Preconditions: the shape's rows and the lengths of its rows are at least 1 and at most the
/// maximum; `from` is any cell of the GRID_MAX_ROWS x GRID_MAX_COLS array.
[[nodiscard]] GridStep grid_step(const GridShape &shape, GridCursor from, GridKey key) noexcept;

} // namespace hmi::ui
