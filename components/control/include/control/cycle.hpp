#pragma once
// The control island's per-cycle sequence (hazard fix C4, docs/plans/hazard-c4-spec.md §3.2,
// §4.3): the motion guard, the stick pipeline, note_cycle and the task-watchdog reset, in that
// order, on every cycle, valid or not. A template with no espp or ESP-IDF type, so the host
// tests drive it on fake time with fakes for the stick, its Io and the watchdog.
//
// StickIsland (stick_island.hpp) runs it every cycle; main's AdcStickIo feeds the verdict to
// C1's output permit as its condition 2 (hazard-c4-spec.md §2.1).

#include <cstdint>

#include "control/motion_guard.hpp"

namespace hmi::control {

/// One ADC cycle after the three reads (REQ-CTL-01, REQ-CTL-12).
///
/// `Watchdog` is the task watchdog as this task sees it; main fills it with
/// `esp_task_wdt_add(NULL)` and `esp_task_wdt_reset()` (C4 §4.3), the tests with a fake:
///   bool subscribe();   // at the first cycle; false = the subscription failed
///   void reset();       // at the end of every cycle
/// `Clock` is a callable returning the ADC-side clock in uint32 ms (main injects it, C4 §3.1).
///
/// Per cycle, in this order: (first cycle only) subscribe; load the guard's inputs, stamps
/// first, then the clock (C4 §3.2 rule 2); evaluate the guard once and publish its counters;
/// hand the verdict to the Io; one `Stick::cycle`; `Io::note_cycle`; reset the watchdog.
///
/// What it asks of `Stick` and `Io`, besides what the pipeline itself asks of `Io`:
///   bool Stick::cycle(Io &, const Raw &);                 // true = XYTwist published
///   void Io::set_motion_verdict(GuardReason);             // before the pipeline runs
///   void Io::note_cycle(bool valid, float h_mv, float v_mv, float twist_mv, bool published);
/// `Raw` has optional `horizontal_mv`, `vertical_mv`, `twist_mv` (stick::RawReadsMv).
template <typename Watchdog, typename Clock> class ControlCycle {
public:
  /// @param watchdog The task watchdog port; must outlive the cycle.
  /// @param clock The ADC-side clock.
  /// @param sources What the UI and RTPS tasks write for the guard; must outlive the cycle.
  /// @param flags The link's flags and the UI's gate; must outlive the cycle.
  /// @param telemetry Where the guard's observability goes; must outlive the cycle.
  ControlCycle(Watchdog &watchdog, Clock clock, const GuardSources &sources, GuardFlags flags,
               GuardTelemetry &telemetry)
      : watchdog_(watchdog)
      , clock_(clock)
      , sources_(sources)
      , flags_(flags)
      , telemetry_(telemetry) {}

  /// Runs one cycle on @p raw. Returns whether XYTwist was published.
  template <typename Stick, typename Io, typename Raw>
  bool run(Stick &stick, Io &io, const Raw &raw) {
    if (!started_) {
      started_ = true;
      adc_wdt_ok_ = watchdog_.subscribe(); // start-up only (CS-SAF-04): the port logs a failure
      telemetry_.adc_wdt_ok.store(adc_wdt_ok_, std::memory_order_relaxed);
    }
    std::uint32_t now_ms = 0;
    const GuardInputs in = load_guard_inputs(sources_, flags_, adc_wdt_ok_, clock_, now_ms);
    const GuardReason verdict = guard_.evaluate(in, now_ms);
    guard_.publish(telemetry_);
    io.set_motion_verdict(verdict);
    const bool published = stick.cycle(io, raw);
    // Every cycle, valid or not. X is the horizontal channel.
    io.note_cycle(raw.vertical_mv && raw.horizontal_mv && raw.twist_mv,
                  raw.horizontal_mv.value_or(0.0f), raw.vertical_mv.value_or(0.0f),
                  raw.twist_mv.value_or(0.0f), published);
    watchdog_.reset();
    return published;
  }

  /// The guard (tests, and the self test's reads through the telemetry).
  [[nodiscard]] const MotionGuard &guard() const noexcept { return guard_; }
  /// Whether this task's TWDT subscription succeeded (false before the first cycle).
  [[nodiscard]] bool adc_wdt_ok() const noexcept { return adc_wdt_ok_; }

private:
  Watchdog &watchdog_;
  Clock clock_;
  const GuardSources &sources_;
  GuardFlags flags_;
  GuardTelemetry &telemetry_;
  MotionGuard guard_;
  bool started_ = false;
  bool adc_wdt_ok_ = false;
};

} // namespace hmi::control
