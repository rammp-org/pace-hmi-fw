#pragma once
// The bench verb STALL's requests (hazard fix C4, hazard-c4-spec.md §8 B5i/B5j, REQ-RUI-07):
// bench builds only. The firmware takes a request only inside `if constexpr
// (BENCH_STICK_INJECT)` (main), so a release build makes none; the object costs a few bytes.
//
// A stall is a stuck task: the stalled task busy-waits on its own cycle (no yield, no heartbeat,
// no watchdog reset), so the motion guard and the task watchdog see exactly what a hang does.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hmi::control {

/// What STALL stalls (C4 §8): the UI task, or the stick task.
enum class StallTask : std::uint8_t { UI, ADC };

/// One pending stall per task, in ms; 0 = none. Any task requests; the stalled task takes it.
class BenchStall {
public:
  /// Asks @p task to stall for @p ms at its next cycle (the remote UI's task).
  void request(StallTask task, std::uint32_t ms) noexcept {
    ms_[index(task)].store(ms, std::memory_order_release);
  }
  /// The stall asked of @p task, cleared (the task itself, once per cycle).
  [[nodiscard]] std::uint32_t take(StallTask task) noexcept {
    return ms_[index(task)].exchange(0, std::memory_order_acq_rel);
  }

private:
  static constexpr std::size_t index(StallTask task) noexcept {
    return static_cast<std::size_t>(task);
  }
  std::array<std::atomic<std::uint32_t>, 2> ms_{};
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "CS-OWN-03");
};

/// Busy-waits @p ms on @p clock (uint32 ms): the stalled task neither yields nor resets its
/// watchdog.
template <typename Clock> void spin_for_ms(std::uint32_t ms, Clock clock) noexcept {
  const std::uint32_t start = clock();
  while (clock() - start < ms) {
  }
}

} // namespace hmi::control
