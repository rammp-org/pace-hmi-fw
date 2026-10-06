#pragma once
// The bench gate's PIN entry: four digits, judged on the fourth (REQ-MOD-05..08, README).

#include <array>
#include <cstdint>
#include <string_view>

namespace hmi::ui {

/// What the line above the dots reads.
enum class PinMessage : uint8_t {
  PROMPT, ///< "Enter PIN to proceed"
  WRONG,  ///< "Incorrect PIN - try again"
};

/// What a digit key did.
enum class PinVerdict : uint8_t {
  INCOMPLETE, ///< fewer than PIN_LEN digits typed
  ACCEPTED,   ///< the fourth digit, and the entry is the PIN
  REJECTED,   ///< the fourth digit, and the entry is not the PIN
};

struct PinPress {
  int typed = 0; ///< the digits typed, this one included (1..PIN_LEN), before any verdict
  PinVerdict verdict = PinVerdict::INCOMPLETE;
};

/// The digits typed so far and the line above the dots. The PIN itself is the caller's (a
/// build constant): the model only compares against it. Not safety-relevant: the gate is a
/// "not by accident" barrier in front of a bench screen.
class PinModel {
public:
  static constexpr int PIN_LEN = 4; ///< the screen draws exactly four dots

  struct Config {
    std::string_view pin; ///< the PIN; any other length than PIN_LEN is never accepted
  };

  constexpr explicit PinModel(const Config &config) noexcept
      : pin_(config.pin) {}

  /// Empties the entry and puts the prompt back (each visit to the screen).
  void reset() noexcept;
  /// A digit key, 0..9. On the PIN_LEN-th digit the entry is judged and emptied either way; a
  /// rejection sets the WRONG line, anything else typed puts the prompt back.
  PinPress press(int digit) noexcept;
  /// Takes the last digit back. False (and nothing changes) on an empty entry. The line is
  /// left as it is, so a rejection notice survives a backspace on the entry it emptied.
  bool backspace() noexcept;

  /// The digits typed: how many dots are lit.
  [[nodiscard]] int digits() const noexcept { return length_; }
  [[nodiscard]] PinMessage message() const noexcept { return message_; }

private:
  std::string_view pin_;
  std::array<char, PIN_LEN> entry_{};
  int length_ = 0;
  PinMessage message_ = PinMessage::PROMPT;
};

} // namespace hmi::ui
