#pragma once

/// @file queue.hpp
/// @brief Queue<T, DEPTH, POLICY>: the bounded FIFO channel for events (CS-OWN-03, CS-OWN-04).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <system_error>
#include <utility>

#include "fw_core/error.hpp"
#include "fw_core/handles.hpp"
#include "fw_core/message.hpp"
#include "fw_core/port/raw_queue.hpp"
#include "fw_core/thread_checker.hpp"

namespace hmi::fw {

/// @brief What a full queue does with one more message (CS-OWN-04).
enum class FullPolicy : std::uint8_t {
  DROP_NEWEST_COUNT, ///< Drop the new message, count the drop, return QUEUE_FULL.
  BLOCK_TIMEOUT,     ///< Wait up to Config::send_timeout, then return SEND_TIMEOUT. Never on a
                     ///< safety path.
  RAISE_FAULT,       ///< Drop the new message, set the fault flag, log, return QUEUE_FAULT.
};

/// @brief The name of a full policy, for logs.
/// @param policy The policy.
/// @return A short UPPER_CASE name.
[[nodiscard]] constexpr std::string_view to_string(FullPolicy policy) noexcept {
  switch (policy) {
  case FullPolicy::DROP_NEWEST_COUNT:
    return "DROP_NEWEST_COUNT";
  case FullPolicy::BLOCK_TIMEOUT:
    return "BLOCK_TIMEOUT";
  case FullPolicy::RAISE_FAULT:
    return "RAISE_FAULT";
  }
  return "UNKNOWN";
}

/// @brief The deepest queue fw_core builds; a bound for drain loops (CS-FLW-02).
inline constexpr std::size_t MAX_QUEUE_DEPTH = 64;

namespace detail {
/// @brief Logs that a RAISE_FAULT queue overflowed (once per fault).
/// @param name The queue's name.
void log_queue_fault(std::string_view name) noexcept;
} // namespace detail

/// @brief A bounded FIFO channel for events: none are merged, order is kept.
/// @details Built on a FreeRTOS queue (host: a shim). Any number of senders, one receiver; the
///          receive end has a ThreadChecker bound to the first task that receives. The full
///          policy is part of the type, so it is declared where the queue is.
///          From an ISR, send_from_isr never waits: BLOCK_TIMEOUT acts as a zero timeout.
/// @tparam T A Message (CS-OWN-05).
/// @tparam DEPTH The capacity, 1..MAX_QUEUE_DEPTH.
/// @tparam POLICY What a full queue does.
template <class T, std::size_t DEPTH, FullPolicy POLICY> class Queue {
  static_assert(MessageRules<T>::OK);
  static_assert(DEPTH >= 1 && DEPTH <= MAX_QUEUE_DEPTH,
                "fw::Queue: DEPTH must be 1..MAX_QUEUE_DEPTH (CS-FLW-02)");

public:
  using value_type = T;                             ///< The message type.
  static constexpr std::size_t CAPACITY = DEPTH;    ///< The depth.
  static constexpr FullPolicy FULL_POLICY = POLICY; ///< The full policy.

  /// @brief Configuration.
  struct Config {
    std::string_view name{"queue"}; ///< For logs; must outlive the queue (use a literal).
    std::chrono::milliseconds send_timeout{0}; ///< BLOCK_TIMEOUT only: the longest send wait.
    ThreadChecker::Config receiver_checker{};  ///< The receive end's ownership check.
  };

  /// @brief Creates an empty queue.
  /// @param config The configuration.
  explicit Queue(const Config &config) noexcept
      : name_(config.name)
      , send_timeout_(config.send_timeout)
      , receiver_checker_(config.receiver_checker) {}

  Queue(const Queue &) = delete;
  Queue &operator=(const Queue &) = delete;
  Queue(Queue &&) = delete;
  Queue &operator=(Queue &&) = delete;
  ~Queue() = default;

  /// @brief Appends @p message; a full queue acts on POLICY.
  /// @param message The message.
  /// @param ec Set to the policy's Error when the message was not queued.
  /// @return true if queued.
  [[nodiscard]] bool send(const T &message, std::error_code &ec) noexcept {
    const auto wait =
        POLICY == FullPolicy::BLOCK_TIMEOUT ? send_timeout_ : std::chrono::milliseconds{0};
    if (items_.send(message, wait)) {
      return true;
    }
    on_full(ec);
    return false;
  }

  /// @brief Appends @p message from an ISR; never waits. A full queue acts on POLICY.
  /// @param message The message.
  /// @param higher_priority_task_woken Set when the ISR should yield on exit.
  /// @return true if queued. On false, the drop counter or the fault flag says why.
  [[nodiscard]] bool send_from_isr(const T &message, bool &higher_priority_task_woken) noexcept {
    if (items_.send_from_isr(message, higher_priority_task_woken)) {
      return true;
    }
    if constexpr (POLICY == FullPolicy::DROP_NEWEST_COUNT) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
    } else if constexpr (POLICY == FullPolicy::RAISE_FAULT) {
      // No logging from an ISR (CS-CON-04): the receiver sees faulted().
      fault_.store(true, std::memory_order_release);
    }
    return false;
  }

  /// @brief Takes the oldest message, waiting up to @p timeout.
  /// @param out Receives the message; untouched when none arrives.
  /// @param timeout The longest wait; zero means do not wait.
  /// @param where The caller's location; leave it defaulted.
  /// @return true if a message was taken; false when none arrived or off the receiver's task.
  [[nodiscard]] bool
  receive(T &out, std::chrono::milliseconds timeout = std::chrono::milliseconds{0},
          std::source_location where = std::source_location::current()) noexcept {
    if (!receiver_checker_.check(where)) {
      return false;
    }
    return items_.receive(out, timeout);
  }

  /// @brief Takes every waiting message, oldest first, and calls @p handle on each.
  /// @details Bounded by DEPTH, so messages sent while draining wait for the next cycle.
  /// @param handle Called as handle(const T &).
  /// @param where The caller's location; leave it defaulted.
  /// @return How many messages were handled; 0 off the receiver's task.
  template <class Fn>
  std::size_t drain(Fn &&handle, std::source_location where = std::source_location::current()) {
    if (!receiver_checker_.check(where)) {
      return 0;
    }
    std::size_t handled = 0;
    for (std::size_t i = 0; i < DEPTH; ++i) {
      T message{};
      if (!items_.receive(message, std::chrono::milliseconds{0})) {
        break;
      }
      handle(std::as_const(message));
      ++handled;
    }
    return handled;
  }

  /// @brief How many messages a DROP_NEWEST_COUNT queue has dropped. Never goes down.
  /// @return The count; always 0 for the other policies.
  [[nodiscard]] std::uint32_t dropped() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }

  /// @brief Whether a RAISE_FAULT queue has overflowed since the last clear_fault().
  /// @return The fault flag; always false for the other policies.
  [[nodiscard]] bool faulted() const noexcept { return fault_.load(std::memory_order_acquire); }

  /// @brief Clears the fault flag, once the receiver has handled the fault.
  void clear_fault() noexcept { fault_.store(false, std::memory_order_release); }

  /// @brief The number of messages waiting.
  /// @return A snapshot; it may change as soon as the call returns.
  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

  /// @brief The send end, for a producer's Config.
  /// @return A handle that can only send.
  [[nodiscard]] Sender<Queue> sender() noexcept { return Sender<Queue>{*this}; }

  /// @brief The receive end, for the consumer's Config.
  /// @return A handle that can only receive.
  [[nodiscard]] Receiver<Queue> receiver() noexcept { return Receiver<Queue>{*this}; }

private:
  void on_full(std::error_code &ec) noexcept {
    if constexpr (POLICY == FullPolicy::DROP_NEWEST_COUNT) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      ec = make_error_code(Error::QUEUE_FULL);
    } else if constexpr (POLICY == FullPolicy::BLOCK_TIMEOUT) {
      ec = make_error_code(Error::SEND_TIMEOUT);
    } else {
      if (!fault_.exchange(true, std::memory_order_acq_rel)) {
        detail::log_queue_fault(name_);
      }
      ec = make_error_code(Error::QUEUE_FAULT);
    }
  }

  std::string_view name_;
  std::chrono::milliseconds send_timeout_;
  port::RawQueue<T, DEPTH> items_;
  std::atomic<std::uint32_t> dropped_{0};
  std::atomic<bool> fault_{false};
  ThreadChecker receiver_checker_;
};

/// @brief The send end of a Queue.
template <class T, std::size_t DEPTH, FullPolicy POLICY> class Sender<Queue<T, DEPTH, POLICY>> {
public:
  using Channel = Queue<T, DEPTH, POLICY>; ///< The queue type.

  /// @brief Points at @p queue, which must outlive the handle.
  /// @param queue The channel.
  explicit Sender(Channel &queue) noexcept
      : queue_(&queue) {}

  /// @brief See Queue::send.
  /// @param message The message.
  /// @param ec Set to the policy's Error when the message was not queued.
  /// @return true if queued.
  [[nodiscard]] bool send(const T &message, std::error_code &ec) const noexcept {
    return queue_->send(message, ec);
  }

  /// @brief See Queue::send_from_isr.
  /// @param message The message.
  /// @param higher_priority_task_woken Set when the ISR should yield on exit.
  /// @return true if queued.
  [[nodiscard]] bool send_from_isr(const T &message,
                                   bool &higher_priority_task_woken) const noexcept {
    return queue_->send_from_isr(message, higher_priority_task_woken);
  }

private:
  Channel *queue_;
};

/// @brief The receive end of a Queue.
template <class T, std::size_t DEPTH, FullPolicy POLICY> class Receiver<Queue<T, DEPTH, POLICY>> {
public:
  using Channel = Queue<T, DEPTH, POLICY>; ///< The queue type.

  /// @brief Points at @p queue, which must outlive the handle.
  /// @param queue The channel.
  explicit Receiver(Channel &queue) noexcept
      : queue_(&queue) {}

  /// @brief See Queue::receive.
  /// @param out Receives the message; untouched when none arrives.
  /// @param timeout The longest wait; zero means do not wait.
  /// @param where The caller's location; leave it defaulted.
  /// @return true if a message was taken.
  [[nodiscard]] bool
  receive(T &out, std::chrono::milliseconds timeout = std::chrono::milliseconds{0},
          std::source_location where = std::source_location::current()) const noexcept {
    return queue_->receive(out, timeout, where);
  }

  /// @brief See Queue::drain.
  /// @param handle Called as handle(const T &).
  /// @param where The caller's location; leave it defaulted.
  /// @return How many messages were handled.
  template <class Fn>
  std::size_t drain(Fn &&handle,
                    std::source_location where = std::source_location::current()) const {
    return queue_->drain(std::forward<Fn>(handle), where);
  }

  /// @brief See Queue::dropped.
  /// @return The drop count.
  [[nodiscard]] std::uint32_t dropped() const noexcept { return queue_->dropped(); }

  /// @brief See Queue::faulted.
  /// @return The fault flag.
  [[nodiscard]] bool faulted() const noexcept { return queue_->faulted(); }

  /// @brief See Queue::clear_fault.
  void clear_fault() const noexcept { queue_->clear_fault(); }

private:
  Channel *queue_;
};

} // namespace hmi::fw
