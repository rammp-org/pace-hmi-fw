#pragma once
// The INTERNET screen's signal texts. Plain C++, no LVGL (CS-UI-03).

#include <cstddef>
#include <optional>
#include <span>

namespace hmi::format {

/// signal_text's buffer: "-2147483648 dBm" plus the terminator.
inline constexpr std::size_t SIGNAL_TEXT_SIZE = 16;
/// net_row_signal_text's buffer: "open  -2147483648 dBm" plus the terminator.
inline constexpr std::size_t ROW_SIGNAL_TEXT_SIZE = 24;

/// @brief The joined network's signal: "-45 dBm", or "--" with no RSSI.
/// @param rssi_dbm the RSSI, dBm; nullopt while not joined
/// @param out the buffer, SIGNAL_TEXT_SIZE fits the text; always terminated (unless empty)
void signal_text(std::optional<int> rssi_dbm, std::span<char> out) noexcept;

/// @brief A scanned network row's signal: "-45 dBm", or "open  -45 dBm" for a network with no
/// password.
/// @param secured the network needs a password
/// @param rssi_dbm its RSSI, dBm
/// @param out the buffer, ROW_SIGNAL_TEXT_SIZE fits the text; always terminated (unless empty)
void net_row_signal_text(bool secured, int rssi_dbm, std::span<char> out) noexcept;

} // namespace hmi::format
