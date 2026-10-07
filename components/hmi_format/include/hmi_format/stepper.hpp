#pragma once
// The -/+ rows (settings, DEBUG ACTUATORS), the seat values and the diagnostics readings: a raw
// integer scaled by its decimals, with its unit or one of its names. Plain C++, no LVGL
// (CS-UI-03); the caller hands the text to its label.

#include <cstdint>
#include <span>

namespace hmi::format {

/// What a value reads before it is known (an actuator the MCB has not reported yet). Not zero:
/// zero is a position an actuator can genuinely be in. seat_format and seat_reading_text draw
/// it as "--"; stepper_format must not be given it (see stepper_format).
inline constexpr int32_t VALUE_UNKNOWN = INT32_MIN;

/// The most decimals a row may have: 10^9 is the largest power of ten an int32_t holds.
inline constexpr uint8_t MAX_DECIMALS = 9;

/// @brief A row's fixed description, from settings_spec.h or the RAMMP seat axis table.
struct StepperSpec {
  const char *short_name; ///< "S1", "M1"
  const char *label;      ///< "Brightness"
  int32_t min_value;      ///< raw units
  int32_t max_value;      ///< raw units
  int32_t step;           ///< raw units per step
  uint8_t decimals;       ///< display only: 126 with 1 decimal shows "12.6"; at most MAX_DECIMALS
  const char *unit;       ///< appended to the value; nullptr = none
  /// What each value reads, min_value first, for a row that picks between named options
  /// rather than a number; nullptr = a number.
  const char *const *names = nullptr;
  bool locked_only = false; ///< refused while the chair is driving (see setting_step)
};

/// @brief Formats a raw value the way a settings row draws it: 126 with decimals = 1 reads
/// "12.6", then the unit tight against it ("75%"); a named row reads its name while the value
/// is in range, and the number otherwise. Cut to the buffer like snprintf.
/// @param spec the row; decimals at most MAX_DECIMALS
/// @param raw the value, in raw units; never VALUE_UNKNOWN (callers draw "--" for it)
/// @param out the buffer the text goes into, always terminated (unless it is empty)
void stepper_format(const StepperSpec &spec, int32_t raw, std::span<char> out) noexcept;

/// @brief A seat value with its unit after a space ("12.6 deg"), or "--" while unknown.
/// Names are ignored: a seat value is always a number. The space is why this is not
/// stepper_format alone, which puts the unit tight against the number: that suits a settings
/// row but not a number this size.
/// @param spec the seat axis' row
/// @param raw the value, in raw units, or VALUE_UNKNOWN
/// @param out the buffer the text goes into, always terminated (unless it is empty)
void seat_format(const StepperSpec &spec, int32_t raw, std::span<char> out) noexcept;

/// @brief The seat adjustment page's big number: the value, with "°" on an axis in "deg"
/// (spec 04b), or "--" while unknown.
/// @param spec the selected seat axis' row
/// @param raw the value, in raw units, or VALUE_UNKNOWN
/// @param out the buffer the text goes into, always terminated (unless it is empty)
void seat_reading_text(const StepperSpec &spec, int32_t raw, std::span<char> out) noexcept;

/// @brief The seat adjustment page's footer: how far the axis goes, "of 90.0°" on an axis in
/// "deg" and "of 250.0 mm" otherwise.
/// @param spec the selected seat axis' row
/// @param out the buffer the text goes into, always terminated (unless it is empty)
void seat_range_text(const StepperSpec &spec, std::span<char> out) noexcept;

} // namespace hmi::format
