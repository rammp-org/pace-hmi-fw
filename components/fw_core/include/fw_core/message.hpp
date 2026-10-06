#pragma once

/// @file message.hpp
/// @brief The Message rules: what a channel may carry (CS-OWN-05).

#include <concepts>
#include <type_traits>

#include "fw_core/config.hpp"

namespace hmi::fw {

/// @brief True when T opts in as a message with `static constexpr bool IS_MESSAGE = true;`.
template <class T>
concept MarkedMessage = requires {
  { T::IS_MESSAGE } -> std::convertible_to<bool>;
  requires T::IS_MESSAGE;
};

/// @brief A type a channel may carry (CS-OWN-05).
/// @details Trivially copyable, at most MAX_MESSAGE_SIZE bytes, and explicitly marked with
///          `static constexpr bool IS_MESSAGE = true;`. A message also holds no pointers,
///          references or handles to island objects; C++ cannot check that, so review does.
///          Adapters convert wire types that hold std::string or std::vector into fixed fields.
template <class T>
concept Message = std::is_trivially_copyable_v<T> &&
                  sizeof(T) <= MAX_MESSAGE_SIZE &&MarkedMessage<T>;

/// @brief Checks the Message rules one by one, so a broken rule gets its own error text.
/// @details Channels instantiate this rather than constraining on Message, so that the
///          compiler names the rule (a must-not-compile test matches the text, TS-UNIT-06).
template <class T> struct MessageRules {
  static_assert(std::is_trivially_copyable_v<T>,
                "fw::Message: a message must be trivially copyable (CS-OWN-05)");
  static_assert(sizeof(T) <= MAX_MESSAGE_SIZE,
                "fw::Message: a message must be at most 128 bytes (CS-OWN-05)");
  static_assert(MarkedMessage<T>,
                "fw::Message: a message must be marked with IS_MESSAGE = true (CS-OWN-05)");
  /// @brief True once every rule above holds.
  static constexpr bool OK = true;
};

} // namespace hmi::fw
