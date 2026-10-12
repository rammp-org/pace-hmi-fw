#pragma once
// hmi::stick::MonitorCycle: the stick monitor as the ADC task runs it (hazard fix C2,
// docs/plans/hazard-c2-spec.md §3.2, §3.7; REQ-CTL-15, REQ-STK-24, REQ-STK-27). Once per cycle,
// valid or not, before the pipeline: one step of StickMonitor on the cycle's one `calibrating`,
// with the high limits of the calibration in use; the stick health C1's output permit reads
// (conditions 6 and 7); whether the stick's keys are live; and the monitor's lock-free
// telemetry for the UI, the self test and the bench (MonitorTelemetry).
//
// Pure C++ (the standard library and this component): no ESP-IDF, no allocation, no logging, no
// lock. The ADC task owns the MonitorCycle; the telemetry has one writer (that task).

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stick/permit_types.hpp"
#include "stick/stick_monitor.hpp"
#include "stick/stick_pipeline.hpp"

namespace hmi::stick {

/// The monitor's observability (REQ-STK-24), written by the ADC task only, every cycle, with
/// relaxed stores: each value is a diagnostic on its own. What the permit acts on, the stick
/// health, goes through PermitHooks::stick_health (release/acquire), not through these.
struct MonitorTelemetry {
  std::atomic<std::uint8_t> state{static_cast<std::uint8_t>(MonitorState::INIT)}; ///< MonitorState
  std::atomic<std::uint8_t> last_kind{0};       ///< SampleFault of the last implausible sample
  std::atomic<std::uint8_t> last_axis{0};       ///< its StickAxis
  std::atomic<std::uint8_t> fault_kind{0};      ///< SampleFault the latch stored
  std::atomic<std::uint8_t> fault_axis{0};      ///< its StickAxis
  std::atomic<std::uint32_t> suspect_onsets{0}; ///< OK -> SUSPECT transitions since boot
  std::atomic<std::uint32_t> faults{0};         ///< entries into FAULT since boot
  std::array<std::atomic<std::uint32_t>, kSampleFaultCount> implausible{}; ///< per SampleFault
  std::atomic<std::uint32_t> xy_age_max_ms{0}; ///< the oldest X/Y window age seen, ms
  /// The largest window or oneshot maximum seen since boot, whole mV, horizontal, vertical,
  /// twist (C2 M-1: compare with the calibrated ends); -1 before the first.
  std::array<std::atomic<std::int32_t>, 3> max_mv{{{-1}, {-1}, {-1}}};
};
static_assert(std::atomic<std::uint8_t>::is_always_lock_free &&
                  std::atomic<std::uint32_t>::is_always_lock_free &&
                  std::atomic<std::int32_t>::is_always_lock_free,
              "REQ-STK-25: every shared value is lock-free");

/// Each axis's high limit from a calibration (P4): min(HIGH_RAIL_MV, max + CAL_OVERSHOOT_MV).
[[nodiscard]] constexpr HighLimitsMv limits_of(const CalibrationMv &cal) noexcept {
  return {.horizontal = high_limit_mv(cal.horizontal.max_mv),
          .vertical = high_limit_mv(cal.vertical.max_mv),
          .twist = high_limit_mv(cal.twist.max_mv)};
}

/// The monitor's health as C1's StickHealth (REQ-STK-27): OK only in OK, FAULT in FAULT, CHECK
/// otherwise. NOT_MONITORED is never written once the monitor runs.
[[nodiscard]] constexpr StickHealth stick_health(MonitorHealth health) noexcept {
  switch (health) {
  case MonitorHealth::OK:
    return StickHealth::OK;
  case MonitorHealth::FAULT:
    return StickHealth::FAULT;
  case MonitorHealth::CHECK:
    return StickHealth::CHECK;
  }
  return StickHealth::CHECK;
}

/// A monitor state's name (logs, the bench's STATE line); "?" outside the enum.
[[nodiscard]] constexpr std::string_view to_string(MonitorState state) noexcept {
  switch (state) {
  case MonitorState::INIT:
    return "INIT";
  case MonitorState::OK:
    return "OK";
  case MonitorState::SUSPECT:
    return "SUSPECT";
  case MonitorState::RECOVERING:
    return "RECOVERING";
  case MonitorState::FAULT:
    return "FAULT";
  case MonitorState::CALIBRATING:
    return "CALIBRATING";
  }
  return "?";
}

/// A sample fault's name as the spec writes it (NAN for NAN_VALUE); "?" outside the enum.
[[nodiscard]] constexpr std::string_view to_string(SampleFault kind) noexcept {
  switch (kind) {
  case SampleFault::NONE:
    return "NONE";
  case SampleFault::MISSING:
    return "MISSING";
  case SampleFault::NAN_VALUE:
    return "NAN";
  case SampleFault::NEGATIVE:
    return "NEGATIVE";
  case SampleFault::HIGH:
    return "HIGH";
  case SampleFault::STALE:
    return "STALE";
  case SampleFault::TWIST_PARTIAL:
    return "TWIST_PARTIAL";
  case SampleFault::NO_SAMPLES:
    return "NO_SAMPLES";
  case SampleFault::INTERNAL:
    return "INTERNAL";
  }
  return "?";
}

/// An axis's short name: "H", "V", "TWIST", or "" for none (or outside the enum).
[[nodiscard]] constexpr std::string_view to_string(StickAxis axis) noexcept {
  switch (axis) {
  case StickAxis::HORIZONTAL:
    return "H";
  case StickAxis::VERTICAL:
    return "V";
  case StickAxis::TWIST:
    return "TWIST";
  case StickAxis::NONE:
    return "";
  }
  return "";
}

/// The monitor as the ADC task runs it. See the file comment.
class MonitorCycle {
public:
  /// Starts on the ideal calibration's limits (3150 mV each); use_calibration() before the
  /// first cycle gives the calibration in use.
  constexpr MonitorCycle() noexcept = default;

  /// The calibration in use is now @p cal (at start, and whenever the pipeline takes a new one):
  /// its high limits apply from the next step.
  /// @param cal the calibration in use, mV per axis
  constexpr void use_calibration(const CalibrationMv &cal) noexcept { limits_ = limits_of(cal); }

  /// One cycle, valid or not, before the pipeline (REQ-CTL-15): the monitor's input from
  /// @p calibrating (§3.2), one step on @p sample, the maxima and the telemetry updated.
  /// @param calibrating joystick_cal_running(), sampled once for this cycle
  /// @param sample this cycle's raw sample (after the bench injection)
  /// @param cal_generation joystick_cal_generation() (REQ-CAL-10)
  /// @param now_ms the ADC-side clock (uint32 ms)
  /// @param out the telemetry to publish into
  /// @return the stick health for the permit's conditions 6 and 7
  StickHealth step(bool calibrating, const RawSample &sample, std::uint32_t cal_generation,
                   std::uint32_t now_ms, MonitorTelemetry &out) noexcept {
    monitor_.step(select_input(calibrating, monitor_.state()), sample, limits_, cal_generation,
                  now_ms);
    note_maxima(sample);
    publish(out);
    return stick_health(monitor_.health());
  }

  /// Whether the stick's keys are live (§3.1: OK and RECOVERING; D11).
  /// @return true in OK and RECOVERING
  [[nodiscard]] constexpr bool keys_live() const noexcept {
    return stick::keys_live(monitor_.state());
  }
  /// The monitor (tests, and what the telemetry is taken from).
  /// @return the state machine this cycle steps
  [[nodiscard]] constexpr const StickMonitor &monitor() const noexcept { return monitor_; }
  /// The limits in use (tests).
  /// @return the high limits from the calibration in use
  [[nodiscard]] constexpr const HighLimitsMv &limits() const noexcept { return limits_; }

private:
  // The largest finite maximum of each axis with a read this cycle.
  void note_maxima(const RawSample &s) noexcept {
    const std::array<bool, 3> read{s.horizontal.mean_mv.has_value(), s.vertical.mean_mv.has_value(),
                                   s.twist_reads > 0};
    const std::array<float, 3> max{s.horizontal.max_mv, s.vertical.max_mv, s.twist_max_mv};
    for (std::size_t i = 0; i < max.size(); ++i) {
      if (read[i] && std::isfinite(max[i]) && max[i] > max_seen_[i]) {
        max_seen_[i] = max[i];
      }
    }
  }
  void publish(MonitorTelemetry &out) const noexcept {
    constexpr auto relaxed = std::memory_order_relaxed;
    const auto byte = [](auto e) { return static_cast<std::uint8_t>(e); };
    const StickMonitor::Counters &c = monitor_.counters();
    out.state.store(byte(monitor_.state()), relaxed);
    out.last_kind.store(byte(monitor_.last_reason().kind), relaxed);
    out.last_axis.store(byte(monitor_.last_reason().axis), relaxed);
    out.fault_kind.store(byte(monitor_.fault_reason().kind), relaxed);
    out.fault_axis.store(byte(monitor_.fault_reason().axis), relaxed);
    out.suspect_onsets.store(c.suspect_onsets, relaxed);
    out.faults.store(c.faults, relaxed);
    for (std::size_t k = 0; k < kSampleFaultCount; ++k) {
      out.implausible.at(k).store(c.implausible.at(k), relaxed);
    }
    out.xy_age_max_ms.store(c.xy_age_max_ms, relaxed);
    for (std::size_t i = 0; i < max_seen_.size(); ++i) {
      out.max_mv.at(i).store(
          max_seen_[i] < 0.0f ? -1 : static_cast<std::int32_t>(std::lround(max_seen_[i])), relaxed);
    }
  }

  StickMonitor monitor_{};
  HighLimitsMv limits_{.horizontal = HIGH_RAIL_MV, .vertical = HIGH_RAIL_MV, .twist = HIGH_RAIL_MV};
  std::array<float, 3> max_seen_{-1.0f, -1.0f, -1.0f};
};

} // namespace hmi::stick
