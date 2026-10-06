// The joystick cursor walk, moved from main/frag_seat.inc's grid_key_cb (see grid.hpp).

#include "hmi_models/grid.hpp"

#include <algorithm>
#include <cstddef>

namespace hmi::ui {

namespace {

std::size_t at(int index) noexcept { return static_cast<std::size_t>(index); }

bool is_button(const GridShape &shape, GridCursor c) noexcept {
  return shape.button[at(c.row)][at(c.col)];
}

} // namespace

GridStep grid_step(const GridShape &shape, GridCursor from, GridKey key) noexcept {
  GridCursor c = from;
  switch (key) {
  case GridKey::UP:
    c.row--;
    break;
  case GridKey::DOWN:
    if (c.row + 1 >= shape.rows) {
      return GridStep{.move = GridMove::OFF_BOTTOM, .cursor = from, .on_button = false};
    }
    c.row++;
    break;
  case GridKey::LEFT:
    if (shape.left_edge_is_back && c.col == 0) {
      return GridStep{.move = GridMove::OFF_LEFT, .cursor = from, .on_button = false};
    }
    c.col--;
    break;
  case GridKey::RIGHT:
    c.col++;
    break;
  }
  // Clamp rather than wrap. On a control surface you steer by feel, and running off one edge
  // to reappear at the opposite one is disorienting. The column is re-clamped after a row
  // change too, since rows can be different lengths.
  c.row = std::clamp(c.row, 0, shape.rows - 1);
  const int last_col = shape.cols[at(c.row)] - 1;
  c.col = std::clamp(c.col, 0, last_col);
  // Step over a hole in the row, outward first and then back, so the cursor lands on the
  // nearest real button whichever edge it came from.
  while (!is_button(shape, c) && c.col < last_col) {
    c.col++;
  }
  while (!is_button(shape, c) && c.col > 0) {
    c.col--;
  }
  return GridStep{.move = GridMove::MOVED, .cursor = c, .on_button = is_button(shape, c)};
}

} // namespace hmi::ui
