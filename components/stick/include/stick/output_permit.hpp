#pragma once

/// @file output_permit.hpp
/// @brief The stick output permit: the one gate between the stick and XYTwist for the hazard
///        fixes C1, C3, C4 and C2 (hazard-c1-spec.md §3, REQ-STK-10..15).
/// @details Every valid ADC cycle the command is the mounted position times the speed scale
///          only when all of these hold, in this order; otherwise it is a literal
///          (+0.0, +0.0, +0.0):
///          1. the gate (`stick_drives`) is open;
///          2. the motion guard's verdict is OK (C4; OK until C4, decision D2);
///          3. no calibration is running;
///          4. a measured calibration is in use (G5);
///          5. the POST gate is PASS (G3, C3);
///          6. stick health is not FAULT (G3, C2);
///          7. stick health is not CHECK (C2);
///          8. the neutral latch is set (G1).
///          The first failing condition is the hold reason (HoldReason, the same order).
///
///          The neutral latch sets once every valid cycle for at least kNeutralHold (by the
///          ADC task's clock) had x = y = twist = 0.0 after mapping, that is inside the stick's
///          own dead zones; a NaN is never neutral. A non-neutral or invalid cycle before then
///          restarts the wait. Any of 1-7 failing clears the latch and the wait, so every
///          change from held to allowed needs the stick centred first (decision D1). Once set,
///          the latch stays while 1-7 hold, whatever the stick does.
///
///          The stick button bit is sent released while the POST gate is not PASS
///          (REQ-STK-15, decisions D12 a and C3 Q11); otherwise it passes, held or not (STK-017).
///
///          Pure: no ESP-IDF, no allocation, no logging, no lock, no indirect call. The clock
///          is the ADC task's `uint32_t` ms count, passed in; durations are modulo 2^32.

#include <chrono>
#include <cstdint>

#include "stick/permit_types.hpp"
#include "stick/stick_pipeline.hpp"

namespace hmi::stick {

/// G1 (decision D4): how long the stick must read neutral before a held stick may drive.
inline constexpr std::chrono::milliseconds kNeutralHold{300};

/// @brief Permit conditions 1-7 as one ADC cycle sees them (the neutral latch is the permit's
///        own state).
struct PermitInputs {
  bool gate_open;            ///< 1: `stick_drives`
  bool motion_guard_ok;      ///< 2: C4's verdict is OK (true until C4)
  bool calibrating;          ///< 3: a calibration run owns the stick
  bool calibration_measured; ///< 4: joystick_cal_measured()
  PostGate post;             ///< 5: the POST gate as loaded
  StickHealth health;        ///< 6, 7: stick health as loaded
};

/// @brief The first of conditions 1-7 that fails, in the hold-reason order (REQ-STK-13).
/// @param in the cycle's conditions
/// @return its HoldReason, NONE when 1-7 all hold
[[nodiscard]] constexpr HoldReason first_failing(const PermitInputs &in) noexcept {
  if (!in.gate_open) {
    return HoldReason::GATE_SHUT;
  }
  if (!in.motion_guard_ok) {
    return HoldReason::MOTION_GUARD;
  }
  if (in.calibrating) {
    return HoldReason::CALIBRATING;
  }
  if (!in.calibration_measured) {
    return HoldReason::NOT_CALIBRATED;
  }
  if (!post_passed(in.post)) {
    return HoldReason::POST_NOT_PASSED;
  }
  if (in.health == StickHealth::FAULT) {
    return HoldReason::STICK_FAULT;
  }
  if (!health_allows(in.health)) {
    return HoldReason::STICK_CHECK; // CHECK, or a byte outside the enum
  }
  return HoldReason::NONE;
}

/// @brief Neutral for the latch: all three axes exactly 0.0 after mapping (inside the dead
///        zones). -0.0 is 0.0; a NaN is never neutral.
/// @param p the mounted position
/// @return true when x, y and twist are all zero
[[nodiscard]] constexpr bool is_neutral(const Position &p) noexcept {
  return p.x == 0.0f && p.y == 0.0f && p.twist == 0.0f;
}

/// @brief The permit with its neutral latch. One instance, owned by the ADC task.
class OutputPermit {
public:
  /// @brief One valid ADC cycle (all three reads present).
  /// @param in conditions 1-7 as sampled this cycle
  /// @param mounted the stick as mounted, after mapping
  /// @param now_ms the ADC task's clock, ms (modulo 2^32)
  /// @return whether the stick may drive, whether the button bit passes, and the hold reason
  [[nodiscard]] Permit cycle(const PermitInputs &in, const Position &mounted,
                             std::uint32_t now_ms) noexcept {
    const bool button = post_passed(in.post); // released before POST pass (REQ-STK-15)
    const HoldReason failing = first_failing(in);
    if (failing != HoldReason::NONE) {
      latched_ = false;
      waiting_ = false;
      return {.output = false, .button = button, .reason = failing};
    }
    if (!latched_) {
      latched_ = neutral_long_enough(mounted, now_ms);
    }
    if (latched_) {
      return {.output = true, .button = button, .reason = HoldReason::NONE};
    }
    return {.output = false, .button = button, .reason = HoldReason::CENTRE_FIRST};
  }

  /// @brief An invalid ADC cycle (a read missing): restarts the neutral wait. A latch already
  ///        set stays (only conditions 1-7 clear it).
  void invalid_cycle() noexcept { waiting_ = false; }

  /// @brief Whether the neutral latch is set (for tests and the bench's STATE line).
  [[nodiscard]] bool latched() const noexcept { return latched_; }

private:
  /// @brief Advances the neutral wait by one cycle with 1-7 holding.
  /// @return true once the stick has read neutral for kNeutralHold
  [[nodiscard]] bool neutral_long_enough(const Position &mounted, std::uint32_t now_ms) noexcept {
    if (!is_neutral(mounted)) {
      waiting_ = false;
      return false;
    }
    if (!waiting_) {
      waiting_ = true;
      neutral_since_ms_ = now_ms;
    }
    const std::uint32_t waited_ms = now_ms - neutral_since_ms_; // modulo 2^32
    return waited_ms >= static_cast<std::uint32_t>(kNeutralHold.count());
  }

  bool latched_ = false;               ///< the neutral latch (G1)
  bool waiting_ = false;               ///< a run of neutral cycles is being timed
  std::uint32_t neutral_since_ms_ = 0; ///< when that run started
};

} // namespace hmi::stick
