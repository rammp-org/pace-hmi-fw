#pragma once

/// @file permit_types.hpp
/// @brief The stick output permit's hook types: the POST gate, stick health and the hold
///        reason (hazard-c1-spec.md §3.2, §3.3). Plain one-byte enums and their helpers, so
///        any component can name them; the channels that carry them are permit_hooks.hpp.
/// @details Pure: the C++ standard library only (no ESP-IDF, no fw_core).

#include <cstdint>
#include <string_view>

namespace hmi::stick {

/// @brief The POST gate (hazard-c1-spec.md §3.2, hazard-c3-spec.md §2.3). Not a drive phase:
///        one value the ADC task (stick output) and the drive table (Env guard POST_OK) read.
/// @details NOT_RUN at boot; the POST runner stores PENDING at its first tick, then PASS or
///          FAIL, and never moves back. Only PASS lets the stick drive; any other byte holds
///          it, a value outside the enum included.
enum class PostGate : std::uint8_t {
  NOT_RUN = 0, ///< the runner has not run (C1's value until C3 wires POST)
  PENDING = 1, ///< POST is gathering, or a live check (stick at rest) is not met yet
  PASS = 2,    ///< every required check passed; holds until the next reset
  FAIL = 3,    ///< a latched check failed; holds until the next reset
};

/// @brief The stick's health as C2's plausibility monitor sees it (hazard-c1-spec.md §3.2).
/// @details NOT_MONITORED and OK let the stick drive; CHECK and FAULT hold it (REQ-STK-14).
///          NOT_MONITORED is the value until C2 lands (no detector, decision D2).
enum class StickHealth : std::uint8_t {
  NOT_MONITORED = 0, ///< no monitor fitted (before C2): passes
  OK = 1,            ///< the monitor sees a plausible stick
  CHECK = 2,         ///< the monitor is starting, or a sample was implausible: holds
  FAULT = 3,         ///< a latched stick fault: holds until a recalibration or a reboot
};

/// @brief Why the stick is held, the first failing permit condition in this order
///        (hazard-c1-spec.md §3.3, REQ-STK-13). The one hold-reason list of C1, C3, C4, C2.
enum class HoldReason : std::uint8_t {
  NONE = 0,            ///< the stick drives
  GATE_SHUT = 1,       ///< 1: the gate (`stick_drives`) is shut
  MOTION_GUARD = 2,    ///< 2: C4's motion guard is not OK
  CALIBRATING = 3,     ///< 3: a calibration run owns the stick
  NOT_CALIBRATED = 4,  ///< 4: no measured calibration in use (G5)
  POST_NOT_PASSED = 5, ///< 5: the POST gate is not PASS (G3)
  STICK_FAULT = 6,     ///< 6: stick health FAULT (C2)
  STICK_CHECK = 7,     ///< 7: stick health CHECK (C2)
  CENTRE_FIRST = 8,    ///< 8: the neutral latch is not set (G1)
};

/// @brief A gate value's name, for logs and the bench's STATE line.
/// @param gate a gate value
/// @return "NOT_RUN", "PENDING", "PASS" or "FAIL"; "?" for a value outside the enum
[[nodiscard]] constexpr std::string_view to_string(PostGate gate) noexcept {
  switch (gate) {
  case PostGate::NOT_RUN:
    return "NOT_RUN";
  case PostGate::PENDING:
    return "PENDING";
  case PostGate::PASS:
    return "PASS";
  case PostGate::FAIL:
    return "FAIL";
  }
  return "?";
}

/// @brief A stick health value's name, for logs and the bench's STATE line.
/// @param health a health value
/// @return its enumerator's name; "?" for a value outside the enum
[[nodiscard]] constexpr std::string_view to_string(StickHealth health) noexcept {
  switch (health) {
  case StickHealth::NOT_MONITORED:
    return "NOT_MONITORED";
  case StickHealth::OK:
    return "OK";
  case StickHealth::CHECK:
    return "CHECK";
  case StickHealth::FAULT:
    return "FAULT";
  }
  return "?";
}

/// @brief A hold reason's name, for logs and the bench's STATE line.
/// @param reason a hold reason
/// @return its enumerator's name; "?" for a value outside the enum
[[nodiscard]] constexpr std::string_view to_string(HoldReason reason) noexcept {
  switch (reason) {
  case HoldReason::NONE:
    return "NONE";
  case HoldReason::GATE_SHUT:
    return "GATE_SHUT";
  case HoldReason::MOTION_GUARD:
    return "MOTION_GUARD";
  case HoldReason::CALIBRATING:
    return "CALIBRATING";
  case HoldReason::NOT_CALIBRATED:
    return "NOT_CALIBRATED";
  case HoldReason::POST_NOT_PASSED:
    return "POST_NOT_PASSED";
  case HoldReason::STICK_FAULT:
    return "STICK_FAULT";
  case HoldReason::STICK_CHECK:
    return "STICK_CHECK";
  case HoldReason::CENTRE_FIRST:
    return "CENTRE_FIRST";
  }
  return "?";
}

/// @brief Whether the POST gate lets the stick drive: PASS only (REQ-STK-14). Any other byte,
///        a value outside the enum included, holds it.
/// @param gate the gate as loaded
/// @return true for PASS
[[nodiscard]] constexpr bool post_passed(PostGate gate) noexcept { return gate == PostGate::PASS; }

/// @brief Whether stick health lets the stick drive: NOT_MONITORED and OK (REQ-STK-14).
/// @param health the health as loaded
/// @return true for NOT_MONITORED and OK
[[nodiscard]] constexpr bool health_allows(StickHealth health) noexcept {
  return health == StickHealth::NOT_MONITORED || health == StickHealth::OK;
}

/// @brief One cycle's verdict of the output permit (stick/output_permit.hpp), as the stick
///        pipeline's Io hands it back.
struct Permit {
  bool output;       ///< the command may carry the stick; else it is a literal 0
  bool button;       ///< the stick button bit may be sent as it reads; else released
  HoldReason reason; ///< NONE when `output`, else the first failing condition
};

} // namespace hmi::stick
