#pragma once

/// @file mailbox.hpp
/// @brief Mailbox<T>: the one-slot channel for state (CS-OWN-03).

#include <chrono>
#include <cstdint>
#include <source_location>
#include <string_view>

#include "fw_core/handles.hpp"
#include "fw_core/message.hpp"
#include "fw_core/port/raw_queue.hpp"
#include "fw_core/thread_checker.hpp"

namespace hmi::fw {

/// @brief What Mailbox::read found.
enum class ReadStatus : std::uint8_t {
  UNCHANGED,  ///< No write since the last read; the value is the one read before.
  CHANGED,    ///< At least one write since the last read; the value is the newest.
  WRONG_TASK, ///< Called off the reader's task or from an ISR; the value was not touched.
};

/// @brief The name of a read status, for logs.
/// @param status The status.
/// @return A short UPPER_CASE name.
[[nodiscard]] constexpr std::string_view to_string(ReadStatus status) noexcept {
  switch (status) {
  case ReadStatus::UNCHANGED:
    return "UNCHANGED";
  case ReadStatus::CHANGED:
    return "CHANGED";
  case ReadStatus::WRONG_TASK:
    return "WRONG_TASK";
  }
  return "UNKNOWN";
}

/// @brief A one-slot channel for state: the writer overwrites, the reader gets the newest.
/// @details Built on a FreeRTOS queue of length 1 with xQueueOverwrite (host: a shim).
///          - The writer never blocks, from a task or an ISR.
///          - The reader gets the newest value and whether it changed since its last read:
///            CHANGED once per write (several writes between two reads give one CHANGED).
///          - One reader. The read end has a ThreadChecker bound to the first task that reads.
///          - Fan-out is one mailbox per consumer.
/// @tparam T A Message (CS-OWN-05).
template <class T> class Mailbox {
  static_assert(MessageRules<T>::OK);

public:
  using value_type = T; ///< The message type.

  /// @brief Configuration.
  struct Config {
    T initial{};                            ///< What the reader sees before the first write.
    ThreadChecker::Config reader_checker{}; ///< The read end's ownership check.
  };

  /// @brief Creates an empty mailbox holding @p config.initial.
  /// @param config The configuration.
  explicit Mailbox(const Config &config) noexcept
      : last_(config.initial)
      , reader_checker_(config.reader_checker) {}

  Mailbox(const Mailbox &) = delete;
  Mailbox &operator=(const Mailbox &) = delete;
  Mailbox(Mailbox &&) = delete;
  Mailbox &operator=(Mailbox &&) = delete;
  ~Mailbox() = default;

  /// @brief Replaces the value. Never blocks.
  /// @param value The new value.
  void write(const T &value) noexcept { slot_.overwrite(value); }

  /// @brief Replaces the value from an ISR. Never blocks.
  /// @param value The new value.
  /// @param higher_priority_task_woken Set when the ISR should yield on exit.
  void write_from_isr(const T &value, bool &higher_priority_task_woken) noexcept {
    slot_.overwrite_from_isr(value, higher_priority_task_woken);
  }

  /// @brief Reads the newest value. Never blocks.
  /// @param out Receives the newest value; untouched on WRONG_TASK.
  /// @param where The caller's location; leave it defaulted.
  /// @return CHANGED once per write, UNCHANGED otherwise, WRONG_TASK off the reader's task.
  [[nodiscard]] ReadStatus
  read(T &out, std::source_location where = std::source_location::current()) noexcept {
    if (!reader_checker_.check(where)) {
      return ReadStatus::WRONG_TASK;
    }
    T fresh = last_;
    const bool changed = slot_.receive(fresh, std::chrono::milliseconds{0});
    last_ = fresh;
    out = last_;
    return changed ? ReadStatus::CHANGED : ReadStatus::UNCHANGED;
  }

private:
  port::RawQueue<T, 1> slot_;
  T last_; // Reader-side copy: touched only after reader_checker_ passes.
  ThreadChecker reader_checker_;
};

/// @brief The write end of a Mailbox.
template <class T> class Writer<Mailbox<T>> {
public:
  /// @brief Points at @p mailbox, which must outlive the handle.
  /// @param mailbox The channel.
  explicit Writer(Mailbox<T> &mailbox) noexcept
      : mailbox_(&mailbox) {}

  /// @brief Replaces the value. Never blocks.
  /// @param value The new value.
  void write(const T &value) const noexcept { mailbox_->write(value); }

  /// @brief Replaces the value from an ISR. Never blocks.
  /// @param value The new value.
  /// @param higher_priority_task_woken Set when the ISR should yield on exit.
  void write_from_isr(const T &value, bool &higher_priority_task_woken) const noexcept {
    mailbox_->write_from_isr(value, higher_priority_task_woken);
  }

private:
  Mailbox<T> *mailbox_;
};

/// @brief The read end of a Mailbox.
template <class T> class Reader<Mailbox<T>> {
public:
  /// @brief Points at @p mailbox, which must outlive the handle.
  /// @param mailbox The channel.
  explicit Reader(Mailbox<T> &mailbox) noexcept
      : mailbox_(&mailbox) {}

  /// @brief Reads the newest value; see Mailbox::read.
  /// @param out Receives the newest value; untouched on WRONG_TASK.
  /// @param where The caller's location; leave it defaulted.
  /// @return CHANGED once per write, UNCHANGED otherwise, WRONG_TASK off the reader's task.
  [[nodiscard]] ReadStatus
  read(T &out, std::source_location where = std::source_location::current()) const noexcept {
    return mailbox_->read(out, where);
  }

private:
  Mailbox<T> *mailbox_;
};

/// @brief The write end of @p mailbox, for a producer's Config.
/// @details A free function taking the channel by non-const reference, not a member: only
///          code that can mutate the channel hands out its ends, and a const channel
///          cannot give away a write end (CS-OWN-06).
/// @param mailbox The channel; it must outlive the handle.
/// @return A handle that can only write.
template <class T> [[nodiscard]] Writer<Mailbox<T>> writer(Mailbox<T> &mailbox) noexcept {
  return Writer<Mailbox<T>>{mailbox};
}

/// @brief The read end of @p mailbox, for the consumer's Config.
/// @details A free function taking the channel by non-const reference, not a member: only
///          code that can mutate the channel hands out its ends, and a const channel
///          cannot give away a write end (CS-OWN-06).
/// @param mailbox The channel; it must outlive the handle.
/// @return A handle that can only read.
template <class T> [[nodiscard]] Reader<Mailbox<T>> reader(Mailbox<T> &mailbox) noexcept {
  return Reader<Mailbox<T>>{mailbox};
}

} // namespace hmi::fw
