// Moved from main/frag_settings_ui.inc (stepper_format :152, seat_format :227, and the two
// texts of seat_angle_refresh :249-267) without a change in output: the golden tests in
// test/ were recorded from the code before the move (REQ-FMT-02..05).

#include "hmi_format/stepper.hpp"

#include <array>
#include <cstddef>
#include <string_view>

#include "text_writer.hpp"

namespace hmi::format {

namespace {

using detail::TextWriter;

// The buffer the number goes through before the unit is added, as before the move: the same
// size, so the same cut.
constexpr std::size_t NUMBER_SIZE = 16;
// The fraction's digits: at most 11 (sizeof(digits) - 1 in the code before the move).
constexpr std::size_t FRACTION_DIGITS_MAX = 11;

constexpr std::string_view DEGREE_UNIT = "deg";
constexpr std::string_view DEGREE_SIGN = "°";

std::string_view unit_of(const StepperSpec &spec) noexcept {
  return spec.unit != nullptr ? std::string_view(spec.unit) : std::string_view();
}

// The number alone: "%d" of raw with no decimals; otherwise the integer part, '.', and
// `decimals` digits of fraction, the sign put back by hand so -5 with one decimal reads
// "-0.5" rather than the "0.5" an integer division would give. Built a digit at a time rather
// than with a "%.*f" style format: the value is an integer and must stay one.
void number_text(uint8_t decimals, int32_t raw, std::span<char> out) noexcept {
  TextWriter text(out);
  if (decimals == 0) {
    text.put_int(raw);
    return;
  }
  int32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) { // bounded: decimals <= MAX_DECIMALS (precondition)
    scale *= 10;
  }
  const bool negative = raw < 0;
  const int32_t magnitude = negative ? -raw : raw; // raw != INT32_MIN (precondition)
  const std::size_t count = decimals < FRACTION_DIGITS_MAX ? decimals : FRACTION_DIGITS_MAX;
  std::array<char, FRACTION_DIGITS_MAX + 1> digits{};
  int32_t frac = magnitude % scale;
  for (std::size_t i = count; i > 0; i--) {
    digits[i - 1] = static_cast<char>('0' + frac % 10);
    frac /= 10;
  }
  digits[count] = '\0';
  if (negative) {
    text.put_char('-');
  }
  text.put_int(magnitude / scale);
  text.put_char('.');
  text.put_text(std::string_view(digits.data(), count));
}

// The value with neither unit nor names: what stepper_format gives a copy of the row without
// them, which is what every seat text is built from.
std::array<char, NUMBER_SIZE> bare_number(const StepperSpec &spec, int32_t raw) noexcept {
  std::array<char, NUMBER_SIZE> number{};
  StepperSpec bare = spec;
  bare.unit = nullptr;
  bare.names = nullptr;
  stepper_format(bare, raw, number);
  return number;
}

bool is_degrees(const StepperSpec &spec) noexcept { return unit_of(spec) == DEGREE_UNIT; }

} // namespace

void stepper_format(const StepperSpec &spec, int32_t raw, std::span<char> out) noexcept {
  if (spec.names != nullptr && raw >= spec.min_value && raw <= spec.max_value) {
    TextWriter text(out);
    text.put_text(spec.names[static_cast<std::size_t>(raw - spec.min_value)]);
    return;
  }
  std::array<char, NUMBER_SIZE> number{};
  number_text(spec.decimals, raw, number);
  TextWriter text(out);
  text.put_text(number.data());
  text.put_text(unit_of(spec));
}

void seat_format(const StepperSpec &spec, int32_t raw, std::span<char> out) noexcept {
  if (raw == VALUE_UNKNOWN) {
    TextWriter text(out);
    text.put_text("--");
    return;
  }
  const std::array<char, NUMBER_SIZE> number = bare_number(spec, raw);
  TextWriter text(out);
  text.put_text(number.data());
  text.put_char(' ');
  text.put_text(unit_of(spec));
}

void seat_reading_text(const StepperSpec &spec, int32_t raw, std::span<char> out) noexcept {
  if (raw == VALUE_UNKNOWN) {
    TextWriter text(out);
    text.put_text("--");
    return;
  }
  const std::array<char, NUMBER_SIZE> number = bare_number(spec, raw);
  TextWriter text(out);
  text.put_text(number.data());
  if (is_degrees(spec)) {
    text.put_text(DEGREE_SIGN);
  }
}

void seat_range_text(const StepperSpec &spec, std::span<char> out) noexcept {
  const std::array<char, NUMBER_SIZE> number = bare_number(spec, spec.max_value);
  TextWriter text(out);
  text.put_text("of ");
  text.put_text(number.data());
  if (is_degrees(spec)) {
    text.put_text(DEGREE_SIGN);
  } else {
    text.put_char(' ');
    text.put_text(unit_of(spec));
  }
}

} // namespace hmi::format
