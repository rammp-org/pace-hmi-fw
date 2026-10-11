#pragma once

/// @file alloc_counter.hpp
/// @brief The on-chip allocation guard's counting (TS-DET-08): per watched task, how many heap
///        allocations it made once start-up was declared complete, and the size of the first.
/// @details Plain C++ with no ESP-IDF dependency, so the host app components/alloc_guard/test
///          tests it. The firmware side (alloc_guard.cpp) keeps one AllocCounter and calls
///          note() from ESP-IDF's heap hook, esp_heap_trace_alloc_hook, on every successful
///          allocation of any task, any core, any context.
///
///          A task is told apart by its stack: an allocation counts for the watched slot whose
///          stack range holds the stack pointer at the allocation. That needs no FreeRTOS call
///          (the hook may run with the flash cache disabled, so it calls nothing that is not in
///          IRAM), it cannot be fooled by a task moving to the other core between two reads,
///          and an allocation from an interrupt runs on the interrupt stack, which no watched
///          range holds, so it is never charged to the task it interrupted.
///
///          note() is lock-free: relaxed and acquire loads and one relaxed fetch_add, no call,
///          no allocation, no log. It is always inlined, so the hook's IRAM copy holds it all.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hmi::alloc_guard {

/// @brief One watched task's allocations since start-up was declared complete.
struct Count {
  /// @brief Allocations counted.
  std::uint32_t allocations = 0;
  /// @brief The size of the first one, in bytes (0 while there is none).
  std::uint32_t first_size = 0;
};

/// @brief Per-task allocation counts for up to @p N watched tasks.
/// @details Constant-initialised (constinit): every member starts at zero, nothing runs before
///          main. Not copyable or movable: the hook holds it by address for the program's life.
/// @tparam N How many tasks can be watched at once.
template <std::size_t N> class AllocCounter {
public:
  /// @brief An empty counter: no slot watched, not armed.
  constexpr AllocCounter() noexcept = default;
  /// @brief Not copyable: the hook holds the one counter by address.
  AllocCounter(const AllocCounter &) = delete;
  /// @brief Not copyable: the hook holds the one counter by address.
  /// @return never
  AllocCounter &operator=(const AllocCounter &) = delete;
  /// @brief Not movable: the hook holds the one counter by address.
  AllocCounter(AllocCounter &&) = delete;
  /// @brief Not movable: the hook holds the one counter by address.
  /// @return never
  AllocCounter &operator=(AllocCounter &&) = delete;
  /// @brief Nothing to release.
  ~AllocCounter() = default;

  /// @brief How many tasks can be watched at once.
  /// @return N
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  /// @brief Watches the stack range [@p lo, @p hi) as slot @p slot. An allocation made with the
  ///        stack pointer inside it counts there. A slot moved to a new range (its task was
  ///        made again) keeps its count. A range with lo >= hi, or a slot out of range, is
  ///        ignored.
  /// @details Not for the hook: the caller's task (app_main, the self test). While the range
  ///          is replaced, a concurrent note() can miss an allocation of either task.
  /// @param slot The slot, 0..N-1.
  /// @param lo The lowest stack address.
  /// @param hi One past the highest stack address.
  void watch(std::size_t slot, std::uintptr_t lo, std::uintptr_t hi) noexcept {
    if (slot >= N || lo >= hi) {
      return;
    }
    Slot &s = slots_[slot];
    if (s.lo.load(std::memory_order_relaxed) == lo && s.hi.load(std::memory_order_relaxed) == hi) {
      return;
    }
    s.hi.store(0, std::memory_order_release); // closed: no stack pointer is below 0
    s.lo.store(lo, std::memory_order_relaxed);
    s.hi.store(hi, std::memory_order_release);
  }

  /// @brief Declares start-up complete: allocations count from now on. Before this, note()
  ///        counts nothing.
  void arm() noexcept { armed_.store(true, std::memory_order_release); }

  /// @brief Whether arm() was called.
  /// @return true once armed.
  [[nodiscard]] bool armed() const noexcept { return armed_.load(std::memory_order_acquire); }

  /// @brief Whether slot @p slot watches a stack range.
  /// @param slot The slot; out of range is not watched.
  /// @return true when watch() gave it a range.
  [[nodiscard]] bool watched(std::size_t slot) const noexcept {
    return slot < N && slots_[slot].hi.load(std::memory_order_acquire) != 0;
  }

  /// @brief The allocations counted for slot @p slot.
  /// @param slot The slot; out of range reads as nothing counted.
  /// @return The count and the first allocation's size.
  [[nodiscard]] Count count(std::size_t slot) const noexcept {
    if (slot >= N) {
      return {};
    }
    const Slot &s = slots_[slot];
    return {.allocations = s.allocations.load(std::memory_order_relaxed),
            .first_size = s.first_size.load(std::memory_order_relaxed)};
  }

  /// @brief The hook: one allocation of @p size bytes, made with the stack pointer at @p sp.
  ///        Counts it for the watched slot whose range holds @p sp, if armed; else nothing.
  /// @details Safe in any context: no lock, no call, no allocation, no log.
  /// @param sp The stack pointer at the allocation.
  /// @param size The allocation's size, in bytes.
  [[gnu::always_inline]] inline void note(std::uintptr_t sp, std::size_t size) noexcept {
    if (!armed_.load(std::memory_order_relaxed)) {
      return;
    }
    for (Slot &s : slots_) {
      if (sp < s.hi.load(std::memory_order_acquire) && sp >= s.lo.load(std::memory_order_relaxed)) {
        if (s.allocations.fetch_add(1, std::memory_order_relaxed) == 0) {
          s.first_size.store(static_cast<std::uint32_t>(size), std::memory_order_relaxed);
        }
        return;
      }
    }
  }

private:
  /// @brief One watched task: its stack range and what it allocated.
  struct Slot {
    /// @brief The lowest stack address.
    std::atomic<std::uintptr_t> lo{0};
    /// @brief One past the highest stack address; 0 = not watched.
    std::atomic<std::uintptr_t> hi{0};
    /// @brief Allocations counted.
    std::atomic<std::uint32_t> allocations{0};
    /// @brief The first allocation's size.
    std::atomic<std::uint32_t> first_size{0};
  };

  std::array<Slot, N> slots_{};
  std::atomic<bool> armed_{false};
};

} // namespace hmi::alloc_guard
