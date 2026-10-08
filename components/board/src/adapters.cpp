#include "board/adapters.hpp"

namespace hmi::board {

TouchClick::TouchClick(const Config &config)
    : tab5_(config.tab5)
    , logger_(config.log)
    , click_(config.click) {}

void TouchClick::operator()(const espp::TouchpadData &touch) {
  // The first touch only sets the reference, as the function-local static did.
  if (!previous_touchpad_data_) {
    previous_touchpad_data_ = tab5_.touchpad_convert(touch);
  }
  auto touchpad_data = tab5_.touchpad_convert(touch);
  if (touchpad_data != *previous_touchpad_data_) {
    logger_.debug("Touch: {}", touchpad_data);
    previous_touchpad_data_ = touchpad_data;

    // play a click sound only on the press transition (release + re-touch
    // required before it plays again)
    bool is_pressed = touchpad_data.num_touch_points > 0;
    if (is_pressed && !was_pressed_) {
      click_();
    }
    was_pressed_ = is_pressed;
  }
}

} // namespace hmi::board
