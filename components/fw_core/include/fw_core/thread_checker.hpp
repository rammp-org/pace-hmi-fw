#pragma once

/// @file thread_checker.hpp
/// @brief The run-time ownership check (CS-OWN-11).

#include <array>
#include <atomic>
#include <cstdlib>
#include <source_location>
#include <string_view>

#include "fw_core/config.hpp"
#include "fw_core/port/port.hpp"

namespace hmi::fw {

/// @brief What a failed ownership check reports.
struct OwnershipFailure {
  std::source_location where;   ///< The call that broke the rule.
  std::string_view caller_task; ///< The task that made the call ("ISR" from an interrupt).
  std::string_view owner_task;  ///< The task that owns the object ("?" if unknown).
  bool in_isr;                  ///< True when the call came from an interrupt handler.
};

/// @brief Called when an ownership check fails. Runs on the offending task or ISR.
using failure_fn = void (*)(const OwnershipFailure &failure);

/// @brief Logs an ownership failure: function, file:line, caller task and owner task.
/// @param failure What happened.
void log_ownership_failure(const OwnershipFailure &failure) noexcept;

/// @brief The default failure handler: logs, then aborts when @p ABORT is true.
/// @tparam ABORT true in test builds (abort), false in release builds (log only).
/// @param failure What happened.
template <bool ABORT> void default_failure_handler(const OwnershipFailure &failure) noexcept {
  log_ownership_failure(failure);
  if constexpr (ABORT) {
    std::abort();
  }
}

/// @brief Checks that only the owning task touches an object (CS-OWN-11).
/// @details Binds to the first task that calls check() or bind_to_current_task(). After that,
///          check() from any other task, or from an ISR, calls the failure handler and returns
///          false. Test builds abort in the handler; release builds log, and the caller goes to
///          its safe state (OWNERSHIP_CHECKS_ABORT). check() itself never allocates.
class ThreadChecker {
public:
  /// @brief Configuration.
  struct Config {
    failure_fn on_failure{
        &default_failure_handler<OWNERSHIP_CHECKS_ABORT>}; ///< Called on a failed check.
  };

  /// @brief An unbound checker with the default failure handler.
  ThreadChecker() noexcept
      : ThreadChecker(Config{}) {}

  /// @brief An unbound checker.
  /// @param config The configuration; a null handler falls back to the default.
  explicit ThreadChecker(const Config &config) noexcept
      : on_failure_(config.on_failure != nullptr
                        ? config.on_failure
                        : &default_failure_handler<OWNERSHIP_CHECKS_ABORT>) {}

  ThreadChecker(const ThreadChecker &) = delete;
  ThreadChecker &operator=(const ThreadChecker &) = delete;
  ThreadChecker(ThreadChecker &&) = delete;
  ThreadChecker &operator=(ThreadChecker &&) = delete;
  ~ThreadChecker() = default;

  /// @brief Binds the checker to the calling task, if it is not bound yet.
  /// @param where The caller's location; leave it defaulted.
  /// @return true if the caller now owns the checker; false (after reporting) from an ISR or
  ///         when another task already owns it.
  [[nodiscard]] bool
  bind_to_current_task(std::source_location where = std::source_location::current()) noexcept {
    return check(where);
  }

  /// @brief Checks that the caller is the owner, binding on first use.
  /// @param where The caller's location; leave it defaulted.
  /// @return true on the owner task; false (after calling the failure handler) otherwise.
  [[nodiscard]] bool check(std::source_location where = std::source_location::current()) noexcept;

  /// @brief Whether a task owns the checker yet.
  /// @return true once bound.
  [[nodiscard]] bool is_bound() const noexcept {
    return owner_.load(std::memory_order_acquire) != port::NO_TASK;
  }

private:
  void fail(const std::source_location &where, bool from_isr) const noexcept;
  void store_owner_name() noexcept;

  failure_fn on_failure_;
  std::atomic<port::TaskId> owner_{port::NO_TASK};
  // Written once by the binding task, read only when reporting a failure from another task.
  std::array<std::atomic<char>, TASK_NAME_SIZE> owner_name_{};
  std::atomic<bool> owner_name_ready_{false};
};

} // namespace hmi::fw
