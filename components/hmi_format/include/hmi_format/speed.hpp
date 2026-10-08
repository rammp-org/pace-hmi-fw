#pragma once
// The drive band's speed: MibStatus.speed in m/s on the wire, mph to one decimal on the label.
// Plain C++, no LVGL (CS-UI-03); any task may call it.

#include <cstdint>
#include <span>

namespace hmi::format {

/// mph in one m/s (the same value as rammp::kMphPerMps, components/hmi_rtps_spec).
inline constexpr float MPH_PER_MPS = 2.236936f;
/// The fastest the label shows, in tenths of a mph: 9.9 mph, the widest it fits (the same value
/// as rammp::kSpeedMaxTenths).
inline constexpr int32_t SPEED_MAX_TENTHS = 99;

/// @brief A speed in m/s as the tenths of a mph the label shows, rounded to nearest and clamped
/// to 0..SPEED_MAX_TENTHS. NaN, infinities and anything <= 0 read 0.
/// @param mps the speed, metres per second
/// @return tenths of a mph, 0..SPEED_MAX_TENTHS
[[nodiscard]] int32_t speed_display_tenths(float mps) noexcept;

/// @brief The speed label's text, "N.N", from tenths of a mph clamped to 0..SPEED_MAX_TENTHS.
/// @param tenths tenths of a mph
/// @param out the buffer the text goes into, always terminated (unless it is empty)
void speed_text(int32_t tenths, std::span<char> out) noexcept;

} // namespace hmi::format
