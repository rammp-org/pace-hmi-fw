#pragma once
// The code under test, behind one seam so the golden cases never change: the hmi_models
// component. (Before the move this seam called the verbatim pre-move copies, legacy_ui.cpp,
// which stay as the oracle of the differential cases.)

#include <cstddef>

#include "hmi_models/grid.hpp"
#include "hmi_models/pin.hpp"
#include "legacy_ui.hpp"
#include "shapes.hpp"

namespace sut {

struct GridResult {
  models_test::Move move;
  int row;
  int col;
  bool focused;
};

inline hmi::ui::GridShape grid_shape(const models_test::Shape &shape) {
  hmi::ui::GridShape out{};
  out.rows = shape.rows;
  out.left_edge_is_back = shape.left_edge_is_back;
  for (std::size_t r = 0; r < models_test::MAX_ROWS; r++) {
    out.cols.at(r) = shape.cols[r];
    for (std::size_t c = 0; c < models_test::MAX_COLS; c++) {
      out.button.at(r).at(c) = shape.present[r][c];
    }
  }
  return out;
}

inline hmi::ui::GridKey grid_key(models_test::Key key) {
  switch (key) {
  case models_test::Key::UP:
    return hmi::ui::GridKey::UP;
  case models_test::Key::DOWN:
    return hmi::ui::GridKey::DOWN;
  case models_test::Key::LEFT:
    return hmi::ui::GridKey::LEFT;
  case models_test::Key::RIGHT:
    return hmi::ui::GridKey::RIGHT;
  }
  return hmi::ui::GridKey::UP;
}

inline models_test::Move move_of(hmi::ui::GridMove move) {
  switch (move) {
  case hmi::ui::GridMove::MOVED:
    return models_test::Move::MOVED;
  case hmi::ui::GridMove::OFF_BOTTOM:
    return models_test::Move::OFF_BOTTOM;
  case hmi::ui::GridMove::OFF_LEFT:
    return models_test::Move::OFF_LEFT;
  }
  return models_test::Move::MOVED;
}

inline GridResult grid_step(const models_test::Shape &shape, int row, int col,
                            models_test::Key key) {
  const hmi::ui::GridStep s =
      hmi::ui::grid_step(grid_shape(shape), hmi::ui::GridCursor{row, col}, grid_key(key));
  return GridResult{move_of(s.move), s.cursor.row, s.cursor.col, s.on_button};
}

/// The PIN pad: reset() is a visit, key() a digit (0..9) or backspace (-1). What main's
/// rd_keypad_cb shows for each result of the model: the dots it writes first, then the state.
struct PinResult {
  int dots;
  bool wrong;
  int opened; ///< pages opened since the last reset
  int typed;  ///< the dots the key first wrote, -1 if none
};

class Pin {
public:
  explicit Pin(const char *pin = legacy::pin_constant())
      : model_({.pin = pin}) {}

  void reset() {
    model_.reset();
    opened_ = 0;
  }
  PinResult key(int digit_or_back) {
    if (digit_or_back == -1) {
      const bool changed = model_.backspace();
      return result(changed ? model_.digits() : -1);
    }
    const hmi::ui::PinPress press = model_.press(digit_or_back);
    if (press.verdict == hmi::ui::PinVerdict::ACCEPTED) {
      opened_++;
    }
    return result(press.typed);
  }
  [[nodiscard]] PinResult now() const { return result(-1); }

private:
  [[nodiscard]] PinResult result(int typed) const {
    return PinResult{model_.digits(), model_.message() == hmi::ui::PinMessage::WRONG, opened_,
                     typed};
  }
  hmi::ui::PinModel model_;
  int opened_ = 0;
};

} // namespace sut
