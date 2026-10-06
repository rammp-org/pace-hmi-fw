#pragma once

/// @file check.hpp
/// @brief The recovering check (CS-FLW-03).

#include <source_location>
#include <string_view>

namespace hmi::fw {

namespace detail {
/// @brief Logs a failed check() once: what failed, and where.
/// @param what The condition in words; may be empty.
/// @param where The caller's location.
void report_check_failed(std::string_view what, const std::source_location &where) noexcept;
} // namespace detail

/// @brief A recovering check: logs a failed condition and returns false (CS-FLW-03).
/// @details Always compiled in; never asserts or aborts. The caller returns an error and its
///          island goes to the safe state. A macro could log the condition's text; the rules
///          allow no function-like macros (CS-TYP-04), so pass it in words as @p what.
/// @code
///   if (!fw::check(speed_mm_s <= MAX_SPEED_MM_S, "speed within limit")) {
///     return go_safe(ec);
///   }
/// @endcode
/// @param condition The condition that must hold.
/// @param what The condition in words, for the log.
/// @param where The caller's location; leave it defaulted.
/// @return @p condition.
[[nodiscard]] inline bool
check(bool condition, std::string_view what = {},
      std::source_location where = std::source_location::current()) noexcept {
  if (condition) [[likely]] {
    return true;
  }
  detail::report_check_failed(what, where);
  return false;
}

} // namespace hmi::fw
