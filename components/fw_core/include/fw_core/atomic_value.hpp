#pragma once

/// @file atomic_value.hpp
/// @brief AtomicValue<T>: the channel for one value on its own (CS-OWN-03).

#include <atomic>
#include <type_traits>

#include "fw_core/handles.hpp"

namespace hmi::fw {

/// @brief One lock-free value shared between tasks: a writer stores, readers load.
/// @details For a value whose newest state is all that matters (a flag, an enum verdict).
///          Release on store, acquire on load. ISR-safe, since it never locks.
///          Not a Message: a bool or an enum needs no marker, but it must be lock-free.
/// @tparam T A trivially copyable type whose std::atomic is always lock-free.
template <class T> class AtomicValue {
  static_assert(std::is_trivially_copyable_v<T>,
                "fw::AtomicValue: T must be trivially copyable (CS-OWN-03)");
  static_assert(std::atomic<T>::is_always_lock_free,
                "fw::AtomicValue: std::atomic<T> must be always lock-free (CS-OWN-03)");

public:
  using value_type = T; ///< The value type.

  /// @brief Configuration.
  struct Config {
    T initial{}; ///< The value before the first write.
  };

  /// @brief Creates the value.
  /// @param config The configuration.
  explicit AtomicValue(const Config &config) noexcept
      : value_(config.initial) {}

  AtomicValue(const AtomicValue &) = delete;
  AtomicValue &operator=(const AtomicValue &) = delete;
  AtomicValue(AtomicValue &&) = delete;
  AtomicValue &operator=(AtomicValue &&) = delete;
  ~AtomicValue() = default;

  /// @brief Stores @p value (release). Safe from a task or an ISR.
  /// @param value The new value.
  void write(T value) noexcept { value_.store(value, std::memory_order_release); }

  /// @brief Loads the newest value (acquire). Safe from a task or an ISR.
  /// @return The value.
  [[nodiscard]] T read() const noexcept { return value_.load(std::memory_order_acquire); }

  /// @brief The write end, for the producer's Config.
  /// @return A handle that can only write.
  [[nodiscard]] Writer<AtomicValue> writer() noexcept { return Writer<AtomicValue>{*this}; }

  /// @brief The read end, for a consumer's Config.
  /// @return A handle that can only read.
  [[nodiscard]] Reader<AtomicValue> reader() noexcept { return Reader<AtomicValue>{*this}; }

private:
  std::atomic<T> value_;
};

/// @brief The write end of an AtomicValue.
template <class T> class Writer<AtomicValue<T>> {
public:
  /// @brief Points at @p value, which must outlive the handle.
  /// @param value The channel.
  explicit Writer(AtomicValue<T> &value) noexcept
      : value_(&value) {}

  /// @brief See AtomicValue::write.
  /// @param value The new value.
  void write(T value) const noexcept { value_->write(value); }

private:
  AtomicValue<T> *value_;
};

/// @brief The read end of an AtomicValue.
template <class T> class Reader<AtomicValue<T>> {
public:
  /// @brief Points at @p value, which must outlive the handle.
  /// @param value The channel.
  explicit Reader(AtomicValue<T> &value) noexcept
      : value_(&value) {}

  /// @brief See AtomicValue::read.
  /// @return The value.
  [[nodiscard]] T read() const noexcept { return value_->read(); }

private:
  AtomicValue<T> *value_;
};

} // namespace hmi::fw
