#pragma once

/**
 * @file drive_timing.hpp
 * @brief Bench-only timing of the drive code on the LVGL task (CONFIG_HMI_DEBUG_DRIVE_TIMING).
 *
 * Why it exists: the self test's RTPS ping round trip (rtps.rtt_p50) read worse on one image
 * than on another in a noisy hour, and the two differ in how the drive session is coded. The
 * RTPS receive callbacks take lvgl_mutex, which the LVGL task holds for each whole
 * lv_task_handler() call, so drive code that ran long on the LVGL task could reach that
 * number. This measures how long it does run: the 250 ms drive tick, each drive input that
 * runs on the LVGL task, and, for scale, the whole lv_task_handler() call.
 *
 * Each call site wraps its call in drive_timed(path, fn, args...). With the option off (the
 * default, and never in sdkconfig.defaults: CS-LAY-09) that is the plain call, inlined, and
 * nothing else here is emitted. On, each sample goes into two tracks of its path:
 *   cpu   esp_cpu_get_cycle_count(), reported in ns at the CPU clock: sub-microsecond, but
 *         it counts the core's cycles, which may stop while the core waits for something
 *   wall  esp_timer_get_time(), in us: how long the LVGL task, and so lvgl_mutex, was held
 * each with count, min, mean, max and a histogram for p50 and p99. The histogram has two
 * buckets per octave, so a percentile is the top of its bucket: at most 1.5x the true value.
 *
 * Threads: record() runs only on the LVGL task, the one writer. json() runs on the remote UI
 * task (the DRIVETIME verb): every field is a relaxed atomic, read under a sequence counter
 * (a seqlock), so a report is one consistent snapshot without a lock. A reset is only asked
 * for (request_reset()); the writer does it at its next record(), so it stays the only
 * writer. Nothing on the recording path allocates; json() builds its string on the remote
 * UI task.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "esp_app_desc.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sdkconfig.h"

/// CONFIG_HMI_DEBUG_DRIVE_TIMING as a constexpr (CS-TYP-05). The hidden _AS_INT option is
/// always defined, so this needs no preprocessor conditional.
inline constexpr bool kDriveTiming = CONFIG_HMI_DEBUG_DRIVE_TIMING_AS_INT != 0;

/// What is timed. Every path runs on the LVGL task.
enum class TimedPath : std::uint8_t {
  TICK,          ///< drive_wait_poll(), from rtps_poll_cb every 250 ms
  UNLOCK_HOLD,   ///< the unlock hold completed (unlock_gesture)
  EXIT_HOLD,     ///< the exit hold completed (drive_exit_gesture)
  MENU_KEY,      ///< the burger key on the Drive screen while driving: asks to stop
  PROFILE_CLICK, ///< a drive profile button
  UNLOCK_TIMER,  ///< the unlock's one-second advance to the Drive screen
  ENTRY_POLL,    ///< entry_refusal_poll(), from hold_poll_cb every 33 ms
  LVGL_CYCLE,    ///< one whole lv_task_handler() call, under lvgl_mutex: for scale
};

/// How many TimedPath values there are.
inline constexpr std::size_t kTimedPaths = 8;
static_assert(static_cast<std::size_t>(TimedPath::LVGL_CYCLE) + 1 == kTimedPaths);

/// The path's key in the DRIVETIME report.
constexpr const char *timed_path_name(TimedPath path) {
  switch (path) {
  case TimedPath::TICK:
    return "tick";
  case TimedPath::UNLOCK_HOLD:
    return "unlock_hold";
  case TimedPath::EXIT_HOLD:
    return "exit_hold";
  case TimedPath::MENU_KEY:
    return "menu_key";
  case TimedPath::PROFILE_CLICK:
    return "profile_click";
  case TimedPath::UNLOCK_TIMER:
    return "unlock_timer";
  case TimedPath::ENTRY_POLL:
    return "entry_poll";
  case TimedPath::LVGL_CYCLE:
    return "lvgl_cycle";
  }
  return "unknown";
}

namespace drive_timing_detail {

// Two buckets per octave: bucket 0 holds 0, then [1], [2], [3], [4,5], [6,7], [8,11],
// [12,15], ...; the last bucket also takes everything from 2^kOctaves up (max stays exact).
// Bucket 2 is never used: the octave of 1 has only one value.
inline constexpr unsigned kOctaves = 24; // 2^24 cycles: 47 ms at 360 MHz; 2^24 us: 17 s
inline constexpr std::size_t kBuckets = 1 + 2 * kOctaves;

constexpr std::size_t bucket_of(std::uint32_t v) {
  if (v == 0) {
    return 0;
  }
  const auto octave = static_cast<unsigned>(std::bit_width(v)) - 1;
  if (octave >= kOctaves) {
    return kBuckets - 1;
  }
  const unsigned half = octave == 0 ? 0U : (v >> (octave - 1)) & 1U;
  return 1 + 2 * octave + half;
}

/// The largest value bucket `b` holds.
constexpr std::uint64_t bucket_top(std::size_t b) {
  if (b == 0) {
    return 0;
  }
  if (b >= kBuckets - 1) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  const auto octave = static_cast<unsigned>((b - 1) / 2);
  if (octave == 0) {
    return 1;
  }
  const std::uint64_t step = std::uint64_t{1} << (octave - 1);
  return (std::uint64_t{1} << octave) + ((b - 1) % 2) * step + step - 1;
}

static_assert(bucket_of(1) == 1 && bucket_of(2) == 3 && bucket_of(3) == 4 && bucket_of(4) == 5 &&
              bucket_of(5) == 5 && bucket_of(6) == 6 && bucket_of(7) == 6);
static_assert(bucket_top(bucket_of(2)) == 2 && bucket_top(bucket_of(5)) == 5 &&
              bucket_top(bucket_of(6)) == 7 && bucket_top(bucket_of(48)) == 63 &&
              bucket_top(bucket_of(0xFFFFFFFFU)) == 0xFFFFFFFFU);

/// One track's numbers, copied out of the atomics.
struct TrackValues {
  std::uint32_t count = 0;
  std::uint32_t min = 0;
  std::uint32_t max = 0;
  std::uint64_t sum = 0;
  std::array<std::uint32_t, kBuckets> hist{};

  /// The top of the bucket that holds the sample at `per_mille`, kept within [min, max].
  std::uint64_t percentile(std::uint32_t per_mille) const {
    if (count == 0) {
      return 0;
    }
    const std::uint64_t want = (std::uint64_t{count} * per_mille + 999) / 1000;
    std::uint64_t seen = 0;
    for (std::size_t b = 0; b < kBuckets; ++b) {
      seen += hist[b];
      if (seen >= want) {
        return std::clamp<std::uint64_t>(bucket_top(b), min, max);
      }
    }
    return max;
  }

  /// {"min":..,"mean":..,"p50":..,"p99":..,"max":..,"total_us":..}. Each value is shown times
  /// `mul` over `div` (cycles to ns: 1000 / MHz; us as they are: 1 / 1); total_us is the sum
  /// over `per_us` (cycles: MHz; us: 1).
  std::string json(std::uint64_t mul, std::uint64_t div, std::uint64_t per_us) const {
    const auto shown = [&](std::uint64_t v) { return std::to_string(v * mul / div); };
    const std::uint64_t mean = count == 0 ? 0 : sum * mul / div / count;
    return "{\"min\":" + shown(count == 0 ? 0 : min) + ",\"mean\":" + std::to_string(mean) +
           ",\"p50\":" + shown(percentile(500)) + ",\"p99\":" + shown(percentile(990)) +
           ",\"max\":" + shown(max) + ",\"total_us\":" + std::to_string(sum / per_us) + "}";
  }
};

/// One track's numbers as relaxed atomics. Written by one task only, so add() and clear() use
/// plain loads and stores, no read-modify-write.
struct Track {
  std::atomic<std::uint32_t> count{0};
  std::atomic<std::uint32_t> min{std::numeric_limits<std::uint32_t>::max()};
  std::atomic<std::uint32_t> max{0};
  std::atomic<std::uint32_t> sum_lo{0};
  std::atomic<std::uint32_t> sum_hi{0};
  std::array<std::atomic<std::uint32_t>, kBuckets> hist{};

  void add(std::uint32_t v) {
    constexpr auto r = std::memory_order_relaxed;
    count.store(count.load(r) + 1, r);
    min.store(std::min(min.load(r), v), r);
    max.store(std::max(max.load(r), v), r);
    const std::uint32_t lo = sum_lo.load(r) + v;
    if (lo < v) {
      sum_hi.store(sum_hi.load(r) + 1, r); // the low word wrapped
    }
    sum_lo.store(lo, r);
    std::atomic<std::uint32_t> &bucket = hist[bucket_of(v)];
    bucket.store(bucket.load(r) + 1, r);
  }

  void clear() {
    constexpr auto r = std::memory_order_relaxed;
    count.store(0, r);
    min.store(std::numeric_limits<std::uint32_t>::max(), r);
    max.store(0, r);
    sum_lo.store(0, r);
    sum_hi.store(0, r);
    for (std::atomic<std::uint32_t> &bucket : hist) {
      bucket.store(0, r);
    }
  }

  void copy_to(TrackValues &out) const {
    constexpr auto r = std::memory_order_relaxed;
    out.count = count.load(r);
    out.min = min.load(r);
    out.max = max.load(r);
    out.sum = (std::uint64_t{sum_hi.load(r)} << 32) | sum_lo.load(r);
    for (std::size_t b = 0; b < kBuckets; ++b) {
      out.hist[b] = hist[b].load(r);
    }
  }
};

struct Path {
  Track cpu;
  Track wall;
};

struct PathValues {
  TrackValues cpu;
  TrackValues wall;
};

struct Snapshot {
  std::array<PathValues, kTimedPaths> paths{};
  std::uint32_t window_start_ms = 0;
  std::uint32_t resets = 0;
};

} // namespace drive_timing_detail

/// The samples of every TimedPath, one set per image. Written by the LVGL task only.
class DriveTiming {
public:
  /// One sample of `path`: `cycles` CPU cycles and `wall_us` microseconds. LVGL task only.
  static void record(TimedPath path, std::uint32_t cycles, std::uint32_t wall_us) {
    if (reset_requested_.load(std::memory_order_relaxed) &&
        reset_requested_.exchange(false, std::memory_order_acquire)) {
      reset();
    }
    drive_timing_detail::Path &p = paths_[static_cast<std::size_t>(path)];
    write_begin();
    p.cpu.add(cycles);
    p.wall.add(wall_us);
    write_end();
  }

  /// Start the counts again, from the LVGL task's next sample. Any task.
  static void request_reset() { reset_requested_.store(true, std::memory_order_release); }

  /// Every path as one JSON object on one line. Any task: see the file comment.
  static std::string json() {
    auto snap = std::make_unique<drive_timing_detail::Snapshot>();
    const bool consistent = read(*snap);
    const std::uint32_t mhz = std::max<std::uint32_t>(esp_rom_get_cpu_ticks_per_us(), 1);
    const auto now_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    std::string out = "{\"app\":\"";
    out += esp_app_get_description()->version;
    out += "\",\"cpu_mhz\":" + std::to_string(mhz) +
           ",\"window_ms\":" + std::to_string(now_ms - snap->window_start_ms) +
           ",\"resets\":" + std::to_string(snap->resets) +
           ",\"consistent\":" + (consistent ? "true" : "false") + ",\"paths\":{";
    for (std::size_t i = 0; i < kTimedPaths; ++i) {
      const drive_timing_detail::PathValues &p = snap->paths[i];
      out += std::string(i > 0 ? "," : "") + "\"" + timed_path_name(static_cast<TimedPath>(i)) +
             "\":{\"n\":" + std::to_string(p.cpu.count) +
             ",\"cpu_ns\":" + p.cpu.json(1000, mhz, mhz) + ",\"wall_us\":" + p.wall.json(1, 1, 1) +
             "}";
    }
    out += "}}";
    return out;
  }

private:
  static void write_begin() {
    seq_.store(seq_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
  }

  static void write_end() {
    seq_.store(seq_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
  }

  // LVGL task only, from record().
  static void reset() {
    write_begin();
    for (drive_timing_detail::Path &p : paths_) {
      p.cpu.clear();
      p.wall.clear();
    }
    window_start_ms_.store(static_cast<std::uint32_t>(esp_timer_get_time() / 1000),
                           std::memory_order_relaxed);
    resets_.store(resets_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    write_end();
  }

  // A seqlock read: an even counter that did not move while the copy was taken. The writer
  // holds it odd for well under a microsecond, so a few tries do; false if they ran out.
  static bool read(drive_timing_detail::Snapshot &out) {
    constexpr int kTries = 1000;
    for (int i = 0; i < kTries; ++i) {
      const std::uint32_t before = seq_.load(std::memory_order_acquire);
      if ((before & 1U) != 0) {
        continue;
      }
      for (std::size_t p = 0; p < kTimedPaths; ++p) {
        paths_[p].cpu.copy_to(out.paths[p].cpu);
        paths_[p].wall.copy_to(out.paths[p].wall);
      }
      out.window_start_ms = window_start_ms_.load(std::memory_order_relaxed);
      out.resets = resets_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) == before) {
        return true;
      }
    }
    return false;
  }

  static constinit inline std::array<drive_timing_detail::Path, kTimedPaths> paths_{};
  static constinit inline std::atomic<std::uint32_t> seq_{0};
  static constinit inline std::atomic<std::uint32_t> window_start_ms_{0};
  static constinit inline std::atomic<std::uint32_t> resets_{0};
  static constinit inline std::atomic<bool> reset_requested_{false};
};

/// Calls fn(args...) and, with CONFIG_HMI_DEBUG_DRIVE_TIMING, records how long it took as one
/// sample of `path`. Off, it is the plain call: always inlined, nothing recorded or emitted.
/// LVGL task only. `On` is for the type check below; call sites leave it at its default.
template <bool On = kDriveTiming, class F, class... A>
[[gnu::always_inline]] inline decltype(auto) drive_timed(TimedPath path, F &&fn, A &&...args) {
  if constexpr (On) {
    const std::int64_t t0 = esp_timer_get_time();
    const std::uint32_t c0 = esp_cpu_get_cycle_count();
    if constexpr (std::is_void_v<std::invoke_result_t<F, A...>>) {
      std::forward<F>(fn)(std::forward<A>(args)...);
      const std::uint32_t cycles = esp_cpu_get_cycle_count() - c0;
      DriveTiming::record(path, cycles, static_cast<std::uint32_t>(esp_timer_get_time() - t0));
    } else {
      auto result = std::forward<F>(fn)(std::forward<A>(args)...);
      const std::uint32_t cycles = esp_cpu_get_cycle_count() - c0;
      DriveTiming::record(path, cycles, static_cast<std::uint32_t>(esp_timer_get_time() - t0));
      return result;
    }
  } else {
    (void)path;
    return std::forward<F>(fn)(std::forward<A>(args)...);
  }
}

// The timing arm is type-checked in every build, the option on or off: deducing the return
// type instantiates the body, in an unevaluated operand, so nothing is emitted (CS-LAY-09).
static_assert(std::is_void_v<decltype(drive_timed<true>(TimedPath::TICK, [] {}))>);
static_assert(std::is_same_v<decltype(drive_timed<true>(TimedPath::TICK, [] { return 1; })), int>);
