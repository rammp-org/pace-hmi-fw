#pragma once

/// @file seat_gate.hpp
/// @brief The seat gate: one pure decision per seat press (docs/plans/hazard-seat-spec.md §2.2,
///        §2.4; REQ-SEAT-02..06).
/// @details The HMI only asks for seat motion; the MCB decides (§2.1). Each press is decided
///          alone, from inputs the caller reads live at the press, and nothing is remembered
///          between presses, so there is no table and no state here (§2.11, Q2): `decide`
///          returns ALLOW with the target, or the first failing reason in kGateOrder, then
///          AT_LIMIT. Pure and constexpr: no allocation, no LVGL, no IDF, no lock (REQ-SEAT-06).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hmi_rtps_spec.hpp"
#include "seat/seat_reading.hpp"
#include "stick/permit_types.hpp"

namespace hmi::seat {

/// @brief Why a seat press was refused, or ALLOW. The gate's reasons in their order (§2.2),
///        then NOT_SENT, which the gate never returns: the caller's publish failed (§2.5).
enum class SeatReason : std::uint8_t {
  ALLOW = 0,            ///< the gate passed: publish the target
  LINK_DOWN = 1,        ///< 1: the link is not CONNECTED (network up, MibStatus < 2 s)
  MCB_NOT_IDLE = 2,     ///< 2: the MCB's state is not IDLE (a byte outside the enum included)
  POST_NOT_PASSED = 3,  ///< 3: the POST gate is not PASS (C3's REQ-UI-20 condition)
  STICK_FAULT = 4,      ///< 4: stick health is FAULT, or a byte outside its enum (Q3 = a)
  POSITION_UNKNOWN = 5, ///< 5: that axis' value is not known (§2.3)
  AT_LIMIT = 6,         ///< 6: a step that would not move the axis in the press' direction
  NOT_SENT = 7,         ///< the publish returned false (never from the gate)
};

/// The gate's conditions in the order they are checked (REQ-SEAT-02); AT_LIMIT comes after
/// them, for a step only.
inline constexpr std::array<SeatReason, 5> kGateOrder{
    SeatReason::LINK_DOWN, SeatReason::MCB_NOT_IDLE, SeatReason::POST_NOT_PASSED,
    SeatReason::STICK_FAULT, SeatReason::POSITION_UNKNOWN};

/// @brief The link as the gate reads it: CONNECTED (network up, MibStatus < 2 s) or not. A byte
///        (not a bool), so any value but CONNECTED refuses.
enum class SeatLink : std::uint8_t {
  NOT_CONNECTED = 0, ///< any LinkState but CONNECTED
  CONNECTED = 1,     ///< LinkPort::state() == CONNECTED, now
};

/// @brief What the gate reads, at the press (§2.2's "Read from" column; the caller reads
///        each live: the link from LinkPort::state, the POST gate and stick health acquire).
struct SeatInputs {
  SeatLink link = SeatLink::NOT_CONNECTED; ///< the link, live (LinkPort::state)
  std::uint8_t mcb_state = 0;              ///< MIB::MibSystemState as last reported, as its byte
  hmi::stick::PostGate post = hmi::stick::PostGate::NOT_RUN; ///< the POST gate (acquire)
  hmi::stick::StickHealth health = hmi::stick::StickHealth::NOT_MONITORED; ///< (acquire)
  std::int32_t value = kValueUnknown; ///< the axis' raw value, or kValueUnknown
};

/// @brief What kind of press: a step from the reported value, or a preset.
enum class SeatPress : std::uint8_t {
  STEP,   ///< "-" or "+": one step from the reported value
  PRESET, ///< a preset: an absolute raw target
};
/// @brief What a press asks for: a step (-1 or +1) from the reported value, or a preset (an
///        absolute raw target).
struct SeatAsk {
  SeatPress press = SeatPress::STEP; ///< a step or a preset
  std::int32_t amount = 0; ///< STEP: the direction (> 0 up, < 0 down); PRESET: the raw target
};

/// @brief The decision: ALLOW with the raw target to send, or the reason (target 0).
struct SeatDecision {
  SeatReason reason = SeatReason::LINK_DOWN; ///< ALLOW, or why the press is refused
  std::int32_t target = 0;                   ///< the raw target to send (ALLOW only)
};

/// @brief Conditions 1-4: the link, the MCB's state, POST, the stick. The menu's Seat Functions
///        row asks only these (REQ-UI-32, Q10).
/// @param in The inputs read at the press.
/// @return ALLOW, or the first failing of LINK_DOWN, MCB_NOT_IDLE, POST_NOT_PASSED, STICK_FAULT.
[[nodiscard]] constexpr SeatReason ready(const SeatInputs &in) noexcept {
  using hmi::stick::PostGate;
  using hmi::stick::StickHealth;
  if (in.link != SeatLink::CONNECTED) {
    return SeatReason::LINK_DOWN;
  }
  if (in.mcb_state != static_cast<std::uint8_t>(MIB::MibSystemState::IDLE)) {
    return SeatReason::MCB_NOT_IDLE;
  }
  if (in.post != PostGate::PASS) {
    return SeatReason::POST_NOT_PASSED;
  }
  // NOT_MONITORED, OK and CHECK allow; FAULT and any byte outside the enum refuse.
  if (in.health != StickHealth::NOT_MONITORED && in.health != StickHealth::OK &&
      in.health != StickHealth::CHECK) {
    return SeatReason::STICK_FAULT;
  }
  return SeatReason::ALLOW;
}

/// @brief The gate's conditions 1-5 (REQ-SEAT-02): ready() and the axis value known.
/// @param in The inputs read at the press.
/// @return ALLOW, or the first failing reason in kGateOrder.
[[nodiscard]] constexpr SeatReason gate(const SeatInputs &in) noexcept {
  const SeatReason r = ready(in);
  if (r != SeatReason::ALLOW) {
    return r;
  }
  return known(in.value) ? SeatReason::ALLOW : SeatReason::POSITION_UNKNOWN;
}

/// @brief A step's target from a known value (REQ-SEAT-03): clamp(value + dir x step, min,
///        max); AT_LIMIT unless the target is strictly beyond the value in the press'
///        direction (a direction of 0 never moves). Widened to 64 bits: no overflow for any
///        int32 value.
/// @param spec The axis' row of RAMMP_SEAT_AXIS_TABLE.
/// @param value The axis' known raw value.
/// @param direction > 0 up, < 0 down.
/// @return ALLOW with the target, or AT_LIMIT (target 0).
[[nodiscard]] constexpr SeatDecision
step_target(const rammp::SeatAxisSpec &spec, std::int32_t value, std::int32_t direction) noexcept {
  const std::int64_t dir = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
  std::int64_t target = std::int64_t{value} + dir * std::int64_t{spec.step};
  if (target < spec.min_value) {
    target = spec.min_value;
  }
  if (target > spec.max_value) {
    target = spec.max_value;
  }
  const bool moves = (dir > 0 && target > value) || (dir < 0 && target < value);
  if (!moves) {
    return {SeatReason::AT_LIMIT, 0};
  }
  return {SeatReason::ALLOW, static_cast<std::int32_t>(target)};
}

/// @brief A preset's target (REQ-SEAT-04): clamp(preset, min, max), whatever the value.
/// @param spec The axis' row of RAMMP_SEAT_AXIS_TABLE.
/// @param preset The preset's raw target.
/// @return ALLOW with the clamped target.
[[nodiscard]] constexpr SeatDecision preset_target(const rammp::SeatAxisSpec &spec,
                                                   std::int32_t preset) noexcept {
  std::int32_t target = preset;
  if (target < spec.min_value) {
    target = spec.min_value;
  }
  if (target > spec.max_value) {
    target = spec.max_value;
  }
  return {SeatReason::ALLOW, target};
}

/// @brief One press, decided (REQ-SEAT-02..05): the gate (conditions 1-5), then the target. An
///        allowed target is within [min, max] of `spec`.
/// @param spec The axis' row of RAMMP_SEAT_AXIS_TABLE.
/// @param in The inputs read at the press.
/// @param ask The press: a step's direction or a preset's raw target.
/// @return ALLOW with the raw target, or the first failing reason (target 0).
[[nodiscard]] constexpr SeatDecision decide(const rammp::SeatAxisSpec &spec, const SeatInputs &in,
                                            SeatAsk ask) noexcept {
  const SeatReason r = gate(in);
  if (r != SeatReason::ALLOW) {
    return {r, 0};
  }
  return ask.press == SeatPress::STEP ? step_target(spec, in.value, ask.amount)
                                      : preset_target(spec, ask.amount);
}

/// @brief The reason's name: logs, the bench's STATE (`seat_reason`).
/// @param reason A reason.
/// @return Its enumerator's name; "?" outside the enum.
[[nodiscard]] constexpr std::string_view to_string(SeatReason reason) noexcept {
  switch (reason) {
  case SeatReason::ALLOW:
    return "ALLOW";
  case SeatReason::LINK_DOWN:
    return "LINK_DOWN";
  case SeatReason::MCB_NOT_IDLE:
    return "MCB_NOT_IDLE";
  case SeatReason::POST_NOT_PASSED:
    return "POST_NOT_PASSED";
  case SeatReason::STICK_FAULT:
    return "STICK_FAULT";
  case SeatReason::POSITION_UNKNOWN:
    return "POSITION_UNKNOWN";
  case SeatReason::AT_LIMIT:
    return "AT_LIMIT";
  case SeatReason::NOT_SENT:
    return "NOT_SENT";
  }
  return "?";
}

/// @brief Whether kGateOrder lists the gate's reasons in the enumerators' order, 1 to 5.
/// @return True when it does (static_asserted below).
[[nodiscard]] constexpr bool gate_order_is_enum_order() noexcept {
  for (std::size_t i = 0; i < kGateOrder.size(); ++i) {
    if (static_cast<std::size_t>(kGateOrder[i]) != i + 1) {
      return false;
    }
  }
  return true;
}
// REQ-SEAT-02's order, as the enumerators count it (SEAT-009).
static_assert(gate_order_is_enum_order());

} // namespace hmi::seat
