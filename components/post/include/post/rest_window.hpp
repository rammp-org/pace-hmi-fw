#pragma once
// The rest-window accumulator: the ADC task's side of the quick POST (hazard-c3-spec.md §2.2,
// REQ-POST-14, REQ-POST-20). Every ADC cycle hands it the three raw reads the stick pipeline
// gets (after the bench injection) and the stick button; every WINDOW_MIN_SAMPLES cycles it
// gives one StickWindow, which the ADC task sends to the POST runner as a RestWindowMsg.
//
// Pure: no ESP-IDF, no allocation, no logging, no lock. Its state is a few counters, so it
// lives in the ADC task's own state and its call costs the task's stack only while it runs.

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "post/checks.hpp"
#include "post/facts.hpp"

namespace hmi::post {

/// @brief One rest window as a message for an fw_core Mailbox (CS-OWN-05): trivially
///        copyable, at most 128 B, marked.
struct RestWindowMsg {
  static constexpr bool IS_MESSAGE = true; ///< fw::Message marker (CS-OWN-05)
  StickWindow window;                      ///< the window
};
static_assert(sizeof(RestWindowMsg) <= 128, "a message is at most 128 bytes (CS-OWN-05)");

/// @brief The accumulator. One instance, owned by the ADC task.
/// @details The first window starts at the first cycle with all three reads valid, so the
///          continuous ADC's start-up (its DMA frames not yet filled) cannot fail `adc.valid`
///          on every boot; a dead ADC never starts one. After that every cycle counts, valid or
///          not, and a window is emitted every WINDOW_MIN_SAMPLES cycles; the next starts on
///          the following cycle. Axis mean, min and max are over the window's valid cycles,
///          rounded to whole mV (half away from zero). A NaN (or infinite) read is a failed
///          read. `button_idle` is true only when the button read released on every cycle.
class RestWindow {
public:
  /// @brief One ADC cycle.
  /// @param horizontal_mv the horizontal read; empty when it failed
  /// @param vertical_mv the vertical read; empty when it failed
  /// @param twist_mv the twist read (the 8-read average); empty when it failed
  /// @param button_pressed the stick button as the cycle read it
  /// @return the finished window on its WINDOW_MIN_SAMPLES-th cycle, else nothing
  [[nodiscard]] std::optional<StickWindow> add(std::optional<float> horizontal_mv,
                                               std::optional<float> vertical_mv,
                                               std::optional<float> twist_mv,
                                               bool button_pressed) noexcept {
    const bool valid = usable(horizontal_mv) && usable(vertical_mv) && usable(twist_mv);
    if (!started_ && !valid) {
      return std::nullopt; // waiting for the first all-valid cycle
    }
    started_ = true;
    ++cycles_;
    button_idle_ = button_idle_ && !button_pressed;
    if (valid) {
      ++valid_;
      x_.add(*horizontal_mv);
      y_.add(*vertical_mv);
      twist_.add(*twist_mv);
    }
    if (cycles_ < static_cast<std::uint32_t>(WINDOW_MIN_SAMPLES)) {
      return std::nullopt;
    }
    const StickWindow done{.cycles = cycles_,
                           .valid_cycles = valid_,
                           .x = x_.rest(valid_),
                           .y = y_.rest(valid_),
                           .twist = twist_.rest(valid_),
                           .button_idle = button_idle_};
    start_window();
    return done;
  }

  /// @brief Drops any partial window and waits again for a first all-valid cycle (the bench's
  ///        POST RERUN).
  void reset() noexcept {
    started_ = false;
    start_window();
  }

private:
  /// One axis over the window: the sum, least and most of its valid reads.
  struct Axis {
    float sum = 0.0f;
    float lo = 0.0f;
    float hi = 0.0f;
    std::uint32_t count = 0;

    void add(float mv) noexcept {
      lo = count == 0 || mv < lo ? mv : lo;
      hi = count == 0 || mv > hi ? mv : hi;
      sum += mv;
      ++count;
    }
    [[nodiscard]] AxisRest rest(std::uint32_t samples) const noexcept {
      if (count == 0) {
        return AxisRest{.mean_mv = 0, .min_mv = 0, .max_mv = 0, .samples = 0};
      }
      return AxisRest{.mean_mv = whole_mv(sum / static_cast<float>(count)),
                      .min_mv = whole_mv(lo),
                      .max_mv = whole_mv(hi),
                      .samples = samples};
    }
  };

  /// A read the window can use: present and a finite number.
  [[nodiscard]] static bool usable(std::optional<float> mv) noexcept {
    return mv.has_value() && std::isfinite(*mv);
  }

  /// Rounded to whole mV, half away from zero, saturated to int32 (never overflows).
  [[nodiscard]] static std::int32_t whole_mv(float mv) noexcept {
    constexpr float LIMIT = 2.0e9f; // inside int32 on both sides
    const float clamped = mv < -LIMIT ? -LIMIT : (mv > LIMIT ? LIMIT : mv);
    return static_cast<std::int32_t>(std::lround(clamped));
  }

  void start_window() noexcept {
    cycles_ = 0;
    valid_ = 0;
    button_idle_ = true;
    x_ = Axis{};
    y_ = Axis{};
    twist_ = Axis{};
  }

  bool started_ = false;     ///< the first all-valid cycle has been seen
  std::uint32_t cycles_ = 0; ///< cycles in this window
  std::uint32_t valid_ = 0;  ///< of those, all three reads valid
  bool button_idle_ = true;  ///< released on every cycle so far
  Axis x_{};
  Axis y_{};
  Axis twist_{};
};

} // namespace hmi::post
