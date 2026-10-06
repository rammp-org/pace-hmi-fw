#pragma once

/// @file raw_queue_freertos.hpp
/// @brief A statically allocated FreeRTOS queue, the storage under every fw_core channel.
/// @details No heap: the queue's control block and item storage live inside the object
///          (xQueueCreateStatic), so a channel can be built before start-up ends and never
///          allocates afterwards (CS-MEM-02, CS-SAF-04).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace hmi::fw::port {

/// @brief Converts a timeout to FreeRTOS ticks, rounding up so a short wait is never zero.
/// @param timeout The timeout; negative counts as zero.
/// @return The ticks, capped below portMAX_DELAY so every wait still ends (CS-FLW-02).
[[nodiscard]] inline TickType_t to_ticks(std::chrono::milliseconds timeout) noexcept {
  if (timeout.count() <= 0) {
    return 0;
  }
  const auto ms = static_cast<std::uint64_t>(timeout.count());
  const std::uint64_t ticks = (ms * configTICK_RATE_HZ + 999U) / 1000U;
  constexpr std::uint64_t MAX_TICKS = static_cast<std::uint64_t>(portMAX_DELAY) - 1U;
  return static_cast<TickType_t>(ticks < MAX_TICKS ? ticks : MAX_TICKS);
}

/// @brief A bounded FIFO of DEPTH items of T on a FreeRTOS queue.
/// @tparam T A trivially copyable item.
/// @tparam DEPTH The capacity, at least 1.
template <class T, std::size_t DEPTH> class RawQueue {
  static_assert(DEPTH >= 1, "fw::RawQueue: DEPTH must be at least 1");

public:
  RawQueue() noexcept
      : handle_(xQueueCreateStatic(static_cast<UBaseType_t>(DEPTH), sizeof(T), storage_.data(),
                                   &control_)) {}
  RawQueue(const RawQueue &) = delete;
  RawQueue &operator=(const RawQueue &) = delete;
  RawQueue(RawQueue &&) = delete;
  RawQueue &operator=(RawQueue &&) = delete;
  ~RawQueue() { vQueueDelete(handle_); }

  /// @brief Appends @p item, waiting up to @p timeout for room (xQueueSendToBack).
  /// @param item The item to copy in.
  /// @param timeout The longest wait; zero means do not wait.
  /// @return true if the item was queued.
  [[nodiscard]] bool send(const T &item, std::chrono::milliseconds timeout) noexcept {
    return xQueueSendToBack(handle_, &item, to_ticks(timeout)) == pdTRUE;
  }

  /// @brief Appends @p item without waiting (xQueueSendToBackFromISR).
  /// @param item The item to copy in.
  /// @param higher_priority_task_woken Set when the ISR should yield on exit.
  /// @return true if the item was queued.
  [[nodiscard]] bool send_from_isr(const T &item, bool &higher_priority_task_woken) noexcept {
    BaseType_t woken = pdFALSE;
    const bool sent = xQueueSendToBackFromISR(handle_, &item, &woken) == pdTRUE;
    higher_priority_task_woken = woken == pdTRUE;
    return sent;
  }

  /// @brief Replaces the content of a one-slot queue with @p item (xQueueOverwrite).
  /// @param item The item to copy in.
  void overwrite(const T &item) noexcept {
    static_assert(DEPTH == 1, "fw::RawQueue: overwrite needs a queue of length 1");
    // xQueueOverwrite always returns pdPASS on a queue of length 1.
    (void)xQueueOverwrite(handle_, &item);
  }

  /// @brief overwrite() from an ISR (xQueueOverwriteFromISR).
  /// @param item The item to copy in.
  /// @param higher_priority_task_woken Set when the ISR should yield on exit.
  void overwrite_from_isr(const T &item, bool &higher_priority_task_woken) noexcept {
    static_assert(DEPTH == 1, "fw::RawQueue: overwrite needs a queue of length 1");
    BaseType_t woken = pdFALSE;
    // xQueueOverwriteFromISR always returns pdPASS on a queue of length 1.
    (void)xQueueOverwriteFromISR(handle_, &item, &woken);
    higher_priority_task_woken = woken == pdTRUE;
  }

  /// @brief Takes the oldest item, waiting up to @p timeout for one (xQueueReceive).
  /// @param out Receives the item; untouched when nothing arrives.
  /// @param timeout The longest wait; zero means do not wait.
  /// @return true if an item was taken.
  [[nodiscard]] bool receive(T &out, std::chrono::milliseconds timeout) noexcept {
    return xQueueReceive(handle_, &out, to_ticks(timeout)) == pdTRUE;
  }

  /// @brief The number of items waiting.
  /// @return A snapshot; it may change as soon as the call returns.
  [[nodiscard]] std::size_t size() const noexcept {
    return static_cast<std::size_t>(uxQueueMessagesWaiting(handle_));
  }

private:
  StaticQueue_t control_{};
  std::array<std::uint8_t, DEPTH * sizeof(T)> storage_{};
  QueueHandle_t handle_;
};

} // namespace hmi::fw::port
