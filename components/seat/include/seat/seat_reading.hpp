#pragma once

/// @file seat_reading.hpp
/// @brief One MibStatus seat field in, the axis' raw value or "unknown" out (hazard H8;
///        docs/plans/hazard-seat-spec.md §2.3, REQ-SEAT-01).
/// @details Pure and constexpr: no allocation, no LVGL, no IDF, no lock (REQ-SEAT-06). The
///          only firmware caller of hmi_rtps_spec's `seat_raw`, whose arithmetic is undefined
///          for a NaN, an infinity or a value too large for int32_t (RTPS-009..013): every
///          value is checked here before it gets there (SEAT-011 guards the call sites).

#include <cstdint>

#include "hmi_format/stepper.hpp"
#include "hmi_rtps_spec.hpp"

namespace hmi::seat {

/// What an axis reads while its value is not known: hmi_format's VALUE_UNKNOWN ("--").
inline constexpr int32_t kValueUnknown = hmi::format::VALUE_UNKNOWN;

/// |value x scale| must stay below 2^23: below it a float holds every integer exactly, so the
/// conversion to raw is exact and defined (RTPS-006, the seat spec §3).
inline constexpr float kConversionBound = 8388608.0f; // 2^23

/// How far outside an axis' range a reported value is still taken as known: one step (Q4 = a,
/// approved 2026-10-11; revisit with the MCB team's answer S5).
[[nodiscard]] constexpr int32_t range_tolerance(const rammp::SeatAxisSpec &spec) noexcept {
  return spec.step;
}

/// @brief One reported seat value, checked (REQ-SEAT-01).
/// @param spec The axis' row of RAMMP_SEAT_AXIS_TABLE.
/// @param value The MibStatus field, in whole units (degrees, millimetres).
/// @return The raw value (rounded as seat_raw rounds) when `value` is finite,
///         |value x scale| < 2^23 and the raw value is within [min - step, max + step];
///         otherwise kValueUnknown. Defined for every bit pattern of `value`.
[[nodiscard]] constexpr int32_t seat_reading(const rammp::SeatAxisSpec &spec,
                                             float value) noexcept {
  const float scaled = value * static_cast<float>(rammp::scale_of(spec));
  // One comparison does the finite and the bound checks: every comparison with a NaN is
  // false, and an infinity (or a finite value that overflowed to one) is not inside the bound.
  if (!(scaled > -kConversionBound && scaled < kConversionBound)) {
    return kValueUnknown;
  }
  const int32_t raw = rammp::seat_raw(spec, value); // defined: |scaled| < 2^23
  const int32_t tolerance = range_tolerance(spec);
  if (raw < spec.min_value - tolerance || raw > spec.max_value + tolerance) {
    return kValueUnknown;
  }
  return raw;
}

/// Whether a raw value (as seat_reading returns it) is known.
[[nodiscard]] constexpr bool known(int32_t raw) noexcept { return raw != kValueUnknown; }

} // namespace hmi::seat
