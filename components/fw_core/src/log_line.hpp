#pragma once

// Internal to fw_core's sources: formats one log line into a fixed buffer, with no heap.

#include <array>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

#include "fw_core/port/port.hpp"

namespace hmi::fw::detail {

/// @brief The longest log line fw_core writes; longer lines are cut.
inline constexpr std::size_t LOG_LINE_SIZE = 256;

/// @brief The file name without its directories, to keep log lines short.
/// @param path A path, as std::source_location::file_name() gives it.
/// @return The part after the last '/' or '\'.
[[nodiscard]] constexpr std::string_view base_name(std::string_view path) noexcept {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

/// @brief Formats a line into a stack buffer and logs it at ERROR level.
/// @param format The format string.
/// @param args The arguments.
template <class... Args>
void log_error_line(std::format_string<Args...> format, Args &&...args) noexcept {
  std::array<char, LOG_LINE_SIZE> line{};
  const auto result = std::format_to_n(line.data(), static_cast<std::ptrdiff_t>(line.size()),
                                       format, std::forward<Args>(args)...);
  const auto length = static_cast<std::size_t>(result.out - line.data());
  port::log_error(std::string_view{line.data(), length});
}

} // namespace hmi::fw::detail
