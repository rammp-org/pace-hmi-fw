#pragma once

/// @file error.hpp
/// @brief fw_core's error codes and their std::error_category (CS-ERR-01).

#include <cstdint>
#include <string_view>
#include <system_error>

namespace hmi::fw {

/// @brief Why a channel call failed.
enum class Error : std::uint8_t {
  QUEUE_FULL = 1, ///< A DROP_NEWEST_COUNT queue was full; the message was dropped and counted.
  SEND_TIMEOUT,   ///< A BLOCK_TIMEOUT queue stayed full for the whole timeout.
  QUEUE_FAULT,    ///< A RAISE_FAULT queue was full; the queue's fault flag is set.
};

/// @brief The name of an error, for logs.
/// @param error The error.
/// @return A short UPPER_CASE name.
[[nodiscard]] constexpr std::string_view to_string(Error error) noexcept {
  switch (error) {
  case Error::QUEUE_FULL:
    return "QUEUE_FULL";
  case Error::SEND_TIMEOUT:
    return "SEND_TIMEOUT";
  case Error::QUEUE_FAULT:
    return "QUEUE_FAULT";
  }
  return "UNKNOWN";
}

/// @brief The category of fw_core's error codes.
/// @return The one category object.
[[nodiscard]] const std::error_category &error_category() noexcept;

/// @brief Makes a std::error_code from an fw_core error.
/// @param error The error.
/// @return The code, in error_category().
[[nodiscard]] inline std::error_code make_error_code(Error error) noexcept {
  return {static_cast<int>(error), error_category()};
}

} // namespace hmi::fw

/// @brief Lets an hmi::fw::Error convert to std::error_code.
template <> struct std::is_error_code_enum<hmi::fw::Error> : std::true_type {};
