#include "fw_core/thread_checker.hpp"

#include <cstddef>

#include "log_line.hpp"

namespace hmi::fw {

void log_ownership_failure(const OwnershipFailure &failure) noexcept {
  detail::log_error_line(
      "ownership check failed in {} at {}:{}: caller task '{}'{}, owner task '{}'",
      failure.where.function_name(), detail::base_name(failure.where.file_name()),
      failure.where.line(), failure.caller_task, failure.in_isr ? " (ISR)" : "",
      failure.owner_task);
}

bool ThreadChecker::check(std::source_location where) noexcept {
  if (port::in_isr()) {
    fail(where, true);
    return false;
  }
  const port::TaskId self = port::current_task();
  port::TaskId owner = port::NO_TASK;
  if (owner_.compare_exchange_strong(owner, self, std::memory_order_acq_rel)) {
    store_owner_name();
    return true;
  }
  if (owner == self) {
    return true;
  }
  fail(where, false);
  return false;
}

void ThreadChecker::store_owner_name() noexcept {
  std::array<char, TASK_NAME_SIZE> name{};
  port::current_task_name(name);
  for (std::size_t i = 0; i < name.size(); ++i) {
    owner_name_[i].store(name[i], std::memory_order_relaxed);
  }
  owner_name_ready_.store(true, std::memory_order_release);
}

void ThreadChecker::fail(const std::source_location &where, bool from_isr) const noexcept {
  std::array<char, TASK_NAME_SIZE> caller{};
  if (from_isr) {
    constexpr std::string_view ISR = "ISR";
    ISR.copy(caller.data(), ISR.size());
  } else {
    port::current_task_name(caller);
  }
  std::array<char, TASK_NAME_SIZE> owner{'?'};
  if (owner_name_ready_.load(std::memory_order_acquire)) {
    for (std::size_t i = 0; i < owner.size(); ++i) {
      owner[i] = owner_name_[i].load(std::memory_order_relaxed);
    }
  }
  // Both buffers are NUL-terminated: they are zero-filled and names are cut to fit.
  const OwnershipFailure failure{
      .where = where,
      .caller_task = std::string_view{caller.data()},
      .owner_task = std::string_view{owner.data()},
      .in_isr = from_isr,
  };
  on_failure_(failure);
}

} // namespace hmi::fw
