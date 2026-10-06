#pragma once

/// @file context.hpp
/// @brief Context tokens: proof that the caller runs inside an island (CS-OWN-07).

namespace hmi::fw {

/// @brief A key only @p Island can make (the passkey pattern).
/// @tparam Island The island class; it alone can construct the key.
template <class Island> class Passkey {
  friend Island;
  // Private, so only Island (a friend) can make a key. Since C++20 a class with a user-declared
  // constructor is never an aggregate, so Passkey<Island>{} cannot bypass it.
  Passkey() noexcept = default;
};

/// @brief The base of every context token: an empty object that cannot be copied or moved.
/// @details Entry points reachable from outside an island take a token, so only code that the
///          island's loop called can reach them. A token is never stored, and never captured by
///          a lambda that leaves the island.
class ContextBase {
public:
  ContextBase(const ContextBase &) = delete;
  ContextBase &operator=(const ContextBase &) = delete;
  ContextBase(ContextBase &&) = delete;
  ContextBase &operator=(ContextBase &&) = delete;

protected:
  ContextBase() noexcept = default;
  ~ContextBase() = default;
};

/// @brief The context token of @p Island. Only @p Island can create one.
/// @code
///   class UiIsland {
///     void cycle() {
///       const fw::Context<UiIsland> ctx{fw::Passkey<UiIsland>{}};
///       drain(ctx);
///     }
///   };
/// @endcode
/// @tparam Island The island class.
template <class Island> class Context final : public ContextBase {
public:
  /// @brief Creates the token.
  /// @param key The island's key; only @p Island can make one.
  explicit Context([[maybe_unused]] Passkey<Island> key) noexcept {}
};

} // namespace hmi::fw
