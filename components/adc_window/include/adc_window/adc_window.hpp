#pragma once
// hmi::adc: the continuous ADC's window bookkeeping that hazard fix C2 adds to espp's
// ContinuousAdc (docs/plans/hazard-c2-spec.md REQ-CTL-16; components/espp_adc/README.md). Our
// code, kept out of the vendored upstream files: espp's update task calls it through a few
// hook lines, and the stick task reads it lock-free.
//
// Per channel and window: espp's mean (the same arithmetic as upstream), the largest raw
// conversion through the same calibration, and a sequence number that goes up only for a
// window holding a conversion of that channel. The writer (espp's "ContinuousAdc Task")
// publishes every channel at once behind a sequence lock; a reader gets one consistent
// snapshot of the channels it asks for, or nothing after kReadTries torn attempts. No lock, no
// allocation, no logging, no ESP-IDF.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hmi::adc {

/// Channels the board tracks; a channel at a higher index has no window data (read() refuses
/// it). The HMI's continuous ADC has two (X and Y).
inline constexpr std::size_t kMaxChannels = 8;
/// Attempts a read makes before it gives up on a snapshot torn by the writer. The writer
/// publishes once per window (~128 ms) and takes microseconds, so a second attempt is rare.
inline constexpr int kReadTries = 3;

/// One channel's conversions within one window: what espp's update task sums, plus the largest.
struct AdcWindowAccumulator {
  std::uint32_t sum = 0;     ///< sum of the raw conversions
  std::uint32_t count = 0;   ///< number of raw conversions
  std::uint32_t max_raw = 0; ///< largest raw conversion

  /// Starts a new window.
  constexpr void clear() noexcept {
    sum = 0;
    count = 0;
    max_raw = 0;
  }
  /// Adds one raw conversion.
  constexpr void add(std::uint32_t data) noexcept {
    sum += data;
    ++count;
    max_raw = data > max_raw ? data : max_raw;
  }
};

/// A channel's newest window. Both values are in mV, or raw when the channel could not be
/// calibrated (as espp's get_mv()).
struct AdcWindowReading {
  float mean_mv = 0.0f;       ///< the window's mean, the value get_mv() returns
  float max_mv = 0.0f;        ///< the window's largest conversion
  std::uint32_t sequence = 0; ///< +1 for each window holding a conversion; wraps; 0 = none yet
};

/// Closes one channel's window into @p reading. With at least one conversion: the mean as espp
/// computes it (`float(sum) / float(count)`, then @p to_mv), the maximum through the same
/// @p to_mv, the sequence + 1. Without any: @p reading is left as it was.
/// @param to_mv Raw (as a float) to the reported unit.
/// @return Whether the window held a conversion of this channel.
template <typename ToMv>
constexpr bool close_window(const AdcWindowAccumulator &acc, ToMv &&to_mv,
                            AdcWindowReading &reading) noexcept {
  if (acc.count == 0) {
    return false;
  }
  const float num_samples = static_cast<float>(acc.count);
  const float sum = static_cast<float>(acc.sum);
  reading.mean_mv = to_mv(sum / num_samples);
  reading.max_mv = to_mv(static_cast<float>(acc.max_raw));
  ++reading.sequence;
  return true;
}

/// Every channel's window: accumulated and closed by the ADC's own task (one writer), read
/// lock-free by any task.
class AdcWindowBoard {
public:
  AdcWindowBoard() = default;
  AdcWindowBoard(const AdcWindowBoard &) = delete;
  AdcWindowBoard &operator=(const AdcWindowBoard &) = delete;
  AdcWindowBoard(AdcWindowBoard &&) = delete;
  AdcWindowBoard &operator=(AdcWindowBoard &&) = delete;
  ~AdcWindowBoard() = default;

  /// Writer: one raw conversion of the channel at @p index (ignored past kMaxChannels).
  void add(std::size_t index, std::uint32_t data) noexcept {
    if (index < kMaxChannels) {
      acc_[index].add(data);
    }
  }
  /// Writer: closes the first @p channels windows (close_window, with
  /// `to_mv(index, raw) -> float`), publishes them all at once, and starts the next window on
  /// every channel. The first window starts at construction.
  template <typename ToMv> void close_and_publish(std::size_t channels, ToMv &&to_mv) noexcept {
    const std::size_t n = channels < kMaxChannels ? channels : kMaxChannels;
    for (std::size_t i = 0; i < n; ++i) {
      static_cast<void>(close_window(
          acc_[i], [&to_mv, i](float raw) { return to_mv(i, raw); }, readings_[i]));
    }
    const std::uint32_t v = version_.load(std::memory_order_relaxed);
    version_.store(v + 1U, std::memory_order_relaxed); // odd: a publish is under way
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t i = 0; i < n; ++i) {
      published_[i].mean_mv.store(readings_[i].mean_mv, std::memory_order_relaxed);
      published_[i].max_mv.store(readings_[i].max_mv, std::memory_order_relaxed);
      published_[i].sequence.store(readings_[i].sequence, std::memory_order_relaxed);
    }
    version_.store(v + 2U, std::memory_order_release); // even again: done
    for (AdcWindowAccumulator &a : acc_) {
      a.clear();
    }
  }

  /// Reader (any task): the newest windows of the channels at @p indices, all from the same
  /// publish. False, @p out unspecified, when an index is past kMaxChannels or every one of
  /// kReadTries attempts overlapped a publish.
  template <std::size_t N>
  [[nodiscard]] bool read(const std::array<std::size_t, N> &indices,
                          std::array<AdcWindowReading, N> &out) const noexcept {
    for (const std::size_t index : indices) {
      if (index >= kMaxChannels) {
        return false;
      }
    }
    for (int attempt = 0; attempt < kReadTries; ++attempt) {
      if (try_read(indices, out)) {
        return true;
      }
    }
    return false;
  }

private:
  friend struct AdcWindowBoardTestPeer; // the host tests hold a publish open (a torn read)

  struct Published {
    std::atomic<float> mean_mv{0.0f};
    std::atomic<float> max_mv{0.0f};
    std::atomic<std::uint32_t> sequence{0};
  };
  static_assert(std::atomic<float>::is_always_lock_free, "CS-OWN-03");
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "CS-OWN-03");

  template <std::size_t N>
  bool try_read(const std::array<std::size_t, N> &indices,
                std::array<AdcWindowReading, N> &out) const noexcept {
    const std::uint32_t before = version_.load(std::memory_order_acquire);
    if ((before & 1U) != 0U) {
      return false;
    }
    for (std::size_t k = 0; k < N; ++k) {
      const Published &p = published_[indices[k]];
      out[k] = {p.mean_mv.load(std::memory_order_relaxed), p.max_mv.load(std::memory_order_relaxed),
                p.sequence.load(std::memory_order_relaxed)};
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    return version_.load(std::memory_order_relaxed) == before;
  }

  std::array<AdcWindowAccumulator, kMaxChannels> acc_{};  ///< writer only
  std::array<AdcWindowReading, kMaxChannels> readings_{}; ///< writer only
  std::array<Published, kMaxChannels> published_{};
  std::atomic<std::uint32_t> version_{0}; ///< even: stable; odd: a publish is under way
};

} // namespace hmi::adc
