#pragma once

/// @file owned.hpp
/// @brief Owned<T>: an object that only its owner task may touch (CS-OWN-11).

#include <source_location>
#include <utility>

#include "fw_core/context.hpp"
#include "fw_core/thread_checker.hpp"

namespace hmi::fw {

/// @brief Holds a T that cannot take a context token, and checks the task on every access.
/// @details The owner is the first task that calls access() (CS-OWN-11). Access also needs a
///          context token, so it is reachable only from inside an island (CS-OWN-07).
/// @code
///   fw::Owned<Codec> codec{{}};
///   bool ok = codec.access(ctx, [](Codec &c) { c.reset(); });
/// @endcode
/// @tparam T The held type.
template <class T> class Owned {
public:
  /// @brief Configuration.
  struct Config {
    ThreadChecker::Config checker{}; ///< The ownership check's failure handler.
  };

  /// @brief Builds the T in place.
  /// @param config The configuration.
  /// @param args Arguments for T's constructor.
  template <class... Args>
  explicit Owned(const Config &config, Args &&...args)
      : checker_(config.checker)
      , value_(std::forward<Args>(args)...) {}

  Owned(const Owned &) = delete;
  Owned &operator=(const Owned &) = delete;
  Owned(Owned &&) = delete;
  Owned &operator=(Owned &&) = delete;
  ~Owned() = default;

  /// @brief Runs @p fn on the held T if the caller is the owner.
  /// @param ctx The caller's context token.
  /// @param fn Called as fn(T &); must not keep the reference.
  /// @param where The caller's location; leave it defaulted.
  /// @return true if @p fn ran; false (after the failure handler) on another task or an ISR.
  template <class Fn>
  [[nodiscard]] bool access([[maybe_unused]] const ContextBase &ctx, Fn &&fn,
                            std::source_location where = std::source_location::current()) {
    if (!checker_.check(where)) {
      return false;
    }
    std::forward<Fn>(fn)(value_);
    return true;
  }

  /// @brief Runs @p fn on the held T, read-only, if the caller is the owner.
  /// @param ctx The caller's context token.
  /// @param fn Called as fn(const T &); must not keep the reference.
  /// @param where The caller's location; leave it defaulted.
  /// @return true if @p fn ran; false (after the failure handler) on another task or an ISR.
  template <class Fn>
  [[nodiscard]] bool access([[maybe_unused]] const ContextBase &ctx, Fn &&fn,
                            std::source_location where = std::source_location::current()) const {
    if (!checker_.check(where)) {
      return false;
    }
    std::forward<Fn>(fn)(value_);
    return true;
  }

private:
  // mutable: the checker binds on first use, also through a const Owned.
  mutable ThreadChecker checker_;
  T value_;
};

} // namespace hmi::fw
