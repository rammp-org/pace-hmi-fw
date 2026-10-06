// The bench gate's PIN entry, moved from main/frag_bench_pin.inc's rd_pin_reset and
// rd_keypad_cb (see pin.hpp).

#include "hmi_models/pin.hpp"

#include <cstddef>
#include <string_view>

namespace hmi::ui {

void PinModel::reset() noexcept {
  length_ = 0;
  message_ = PinMessage::PROMPT;
}

PinPress PinModel::press(int digit) noexcept {
  entry_[static_cast<std::size_t>(length_)] = static_cast<char>('0' + digit);
  length_++;
  // Typing clears a previous rejection, so the line reads as a prompt for the entry in
  // progress rather than a verdict on the last one.
  message_ = PinMessage::PROMPT;
  const PinPress typed{.typed = length_, .verdict = PinVerdict::INCOMPLETE};
  if (length_ < PIN_LEN) {
    return typed;
  }
  // Judged on the fourth digit rather than on an OK key: the row of dots makes the length
  // obvious, so a confirm step would add nothing. Either verdict empties the entry, which is
  // what keeps length_ below PIN_LEN above.
  const bool correct = std::string_view(entry_.data(), entry_.size()) == pin_;
  reset();
  if (correct) {
    return PinPress{.typed = typed.typed, .verdict = PinVerdict::ACCEPTED};
  }
  message_ = PinMessage::WRONG;
  return PinPress{.typed = typed.typed, .verdict = PinVerdict::REJECTED};
}

bool PinModel::backspace() noexcept {
  if (length_ == 0) {
    return false;
  }
  length_--;
  return true;
}

} // namespace hmi::ui
