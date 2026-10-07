#pragma once
// The TopBar's two readouts: the clock and the link RTPS runs over. Plain C++, no LVGL
// (CS-UI-03).

#include <cstdint>
#include <ctime>
#include <span>

namespace hmi::format {

/// The first tm_year a real time can have: 125 is 2025 (tm_year counts from 1900). An RTC that
/// lost power reads 2000.
inline constexpr int CLOCK_MIN_TM_YEAR = 125;

/// @brief Whether a time read from the RTC or the MCB can be real (2025 or later).
/// @param t the broken-down time
/// @return true from CLOCK_MIN_TM_YEAR on
[[nodiscard]] bool clock_plausible(const std::tm &t) noexcept;

/// @brief The clock's text, "HH:MM" (printf's "%02d:%02d" of tm_hour and tm_min).
/// @param t the broken-down local time
/// @param out the buffer the text goes into, always terminated (unless it is empty)
void clock_text(const std::tm &t, std::span<char> out) noexcept;

/// What RTPS runs over, in NetLink's order (main/rtps_comms.hpp).
enum class Link : uint8_t {
  ETHERNET, ///< the W5500
  WIFI,     ///< the ESP32-C6
};

/// @brief The TopBar's link label.
/// @param link the link up
/// @return "BT · WI-FI" on WiFi, "BT · ETH" otherwise (a value outside the enum included)
[[nodiscard]] const char *link_text(Link link) noexcept;

} // namespace hmi::format
