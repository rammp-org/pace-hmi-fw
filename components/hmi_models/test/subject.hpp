#pragma once
// The code under test, behind one seam so the golden cases never change. Before the move this
// seam calls the verbatim pre-move copies (legacy_ui.cpp); the move points it at the
// hmi_models component.

#include "legacy_ui.hpp"
#include "shapes.hpp"

namespace sut {

struct GridResult {
  models_test::Move move;
  int row;
  int col;
  bool focused;
};

inline GridResult grid_step(const models_test::Shape &shape, int row, int col,
                            models_test::Key key) {
  const legacy::KeyResult r = legacy::grid_key_on(shape, row, col, key);
  return GridResult{r.move, r.row, r.col, r.focused};
}

/// The PIN pad: reset() is a visit, key() a digit (0..9) or backspace (-1).
struct PinResult {
  int dots;
  bool wrong;
  int opened; ///< pages opened since the last reset
  int typed;  ///< the dots the key first wrote, -1 if none
};

class Pin {
public:
  void reset() { legacy::pin_reset(); }
  PinResult key(int digit_or_back) {
    const legacy::PinState s = legacy::pin_key(digit_or_back);
    return PinResult{s.dots, s.wrong, s.pages_opened, s.first_dots};
  }
  [[nodiscard]] PinResult now() const {
    const legacy::PinState s = legacy::pin_state();
    return PinResult{s.dots, s.wrong, s.pages_opened, -1};
  }
};

} // namespace sut
