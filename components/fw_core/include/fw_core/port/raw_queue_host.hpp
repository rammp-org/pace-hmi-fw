#pragma once

/// @file raw_queue_host.hpp
/// @brief Host shim for a FreeRTOS queue, for the L1 tests (TS-UNIT-01).
/// @details Same semantics as the FreeRTOS calls it stands in for: xQueueSendToBack,
///          xQueueOverwrite, xQueueReceive and their FromISR forms. The mutex and condition
///          variable are allowed here because this is a channel helper (CS-OWN-08).

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace hmi::fw::port {

/// @brief A bounded FIFO of DEPTH items of T, with FreeRTOS queue semantics.
/// @tparam T A trivially copyable item.
/// @tparam DEPTH The capacity, at least 1.
template <class T, std::size_t DEPTH> class RawQueue {
  static_assert(DEPTH >= 1, "fw::RawQueue: DEPTH must be at least 1");

public:
  RawQueue() = default;
  RawQueue(const RawQueue &) = delete;
  RawQueue &operator=(const RawQueue &) = delete;
  RawQueue(RawQueue &&) = delete;
  RawQueue &operator=(RawQueue &&) = delete;
  ~RawQueue() = default;

  /// @brief Appends @p item, waiting up to @p timeout for room.
  /// @param item The item to copy in.
  /// @param timeout The longest wait; zero means do not wait.
  /// @return true if the item was queued.
  [[nodiscard]] bool send(const T &item, std::chrono::milliseconds timeout) noexcept {
    std::unique_lock lock(mutex_);
    if (!not_full_.wait_for(lock, timeout, [this] { return count_ < DEPTH; })) {
      return false;
    }
    push_locked(item);
    lock.unlock();
    not_empty_.notify_one();
    return true;
  }

  /// @brief Appends @p item without waiting (the ISR form).
  /// @param item The item to copy in.
  /// @param higher_priority_task_woken Set to false; the host has no scheduler to yield to.
  /// @return true if the item was queued.
  [[nodiscard]] bool send_from_isr(const T &item, bool &higher_priority_task_woken) noexcept {
    higher_priority_task_woken = false;
    return send(item, std::chrono::milliseconds{0});
  }

  /// @brief Replaces the content of a one-slot queue with @p item; never waits.
  /// @param item The item to copy in.
  void overwrite(const T &item) noexcept {
    static_assert(DEPTH == 1, "fw::RawQueue: overwrite needs a queue of length 1");
    {
      const std::lock_guard lock(mutex_);
      count_ = 0;
      push_locked(item);
    }
    not_empty_.notify_one();
  }

  /// @brief overwrite() from an ISR.
  /// @param item The item to copy in.
  /// @param higher_priority_task_woken Set to false; the host has no scheduler to yield to.
  void overwrite_from_isr(const T &item, bool &higher_priority_task_woken) noexcept {
    higher_priority_task_woken = false;
    overwrite(item);
  }

  /// @brief Takes the oldest item, waiting up to @p timeout for one.
  /// @param out Receives the item; untouched when nothing arrives.
  /// @param timeout The longest wait; zero means do not wait.
  /// @return true if an item was taken.
  [[nodiscard]] bool receive(T &out, std::chrono::milliseconds timeout) noexcept {
    std::unique_lock lock(mutex_);
    if (!not_empty_.wait_for(lock, timeout, [this] { return count_ > 0; })) {
      return false;
    }
    out = items_[head_];
    head_ = (head_ + 1) % DEPTH;
    --count_;
    lock.unlock();
    not_full_.notify_one();
    return true;
  }

  /// @brief The number of items waiting.
  /// @return A snapshot; it may change as soon as the call returns.
  [[nodiscard]] std::size_t size() const noexcept {
    const std::lock_guard lock(mutex_);
    return count_;
  }

private:
  void push_locked(const T &item) noexcept {
    items_[(head_ + count_) % DEPTH] = item;
    ++count_;
  }

  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::array<T, DEPTH> items_{};
  std::size_t head_{0};
  std::size_t count_{0};
};

} // namespace hmi::fw::port
