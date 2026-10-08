#pragma once

/// @file drive_notice.hpp
/// @brief The Drive screen's notice (hazard-c1-spec.md §2.7, §3.3): what the drive adapter hands
///        its port's `show_notice` after every input and tick. Its own header so that a port
///        (drive_ui's DrivePort, the goldens' recording Ui) can name it without the adapter.
/// @details Pure: the C++ standard library, the drive table's types and the stick's hold reason.

#include <cstdint>

#include "drive_session_types.hpp"
#include "stick/permit_types.hpp"

namespace hmi::drive_adapter {

/// @brief What the Drive screen says in its notice slot (hazard-c1-spec.md §2.7, §3.3): the
///        user's stop first, then why the stick is held, in the hold reasons' order. GATE_SHUT
///        and CALIBRATING have no text (the Drive screen is not up, or the calibration screen
///        is): they read NONE.
enum class DriveNotice : std::uint8_t {
  NONE,
  MCB_DID_NOT_STOP, ///< stop_notice: the stop fault
  STOPPING,         ///< stop_notice: an exit phase without the fault
  MOTION_GUARD,     ///< hold reason MOTION_GUARD (C4)
  NOT_CALIBRATED,   ///< hold reason NOT_CALIBRATED (G5)
  POST_NOT_PASSED,  ///< hold reason POST_NOT_PASSED (G3, C3)
  STICK_FAULT,      ///< hold reason STICK_FAULT (C2)
  STICK_CHECK,      ///< hold reason STICK_CHECK (C2)
  CENTRE_FIRST,     ///< hold reason CENTRE_FIRST (G1)
};

/// @brief The Drive notice from the stop notice and the hold reason (hazard-c1-spec.md §2.7):
///        MCB did not stop > Stopping > the hold reason in §3.3's order > none.
/// @param stop the session's stop_notice
/// @param hold the stick's hold reason
/// @return the notice to show
[[nodiscard]] constexpr DriveNotice drive_notice(hmi::drive_session::StopNotice stop,
                                                 hmi::stick::HoldReason hold) noexcept {
  using hmi::drive_session::StopNotice;
  using hmi::stick::HoldReason;
  if (stop == StopNotice::MCB_DID_NOT_STOP) {
    return DriveNotice::MCB_DID_NOT_STOP;
  }
  if (stop == StopNotice::STOPPING) {
    return DriveNotice::STOPPING;
  }
  switch (hold) {
  case HoldReason::MOTION_GUARD:
    return DriveNotice::MOTION_GUARD;
  case HoldReason::NOT_CALIBRATED:
    return DriveNotice::NOT_CALIBRATED;
  case HoldReason::POST_NOT_PASSED:
    return DriveNotice::POST_NOT_PASSED;
  case HoldReason::STICK_FAULT:
    return DriveNotice::STICK_FAULT;
  case HoldReason::STICK_CHECK:
    return DriveNotice::STICK_CHECK;
  case HoldReason::CENTRE_FIRST:
    return DriveNotice::CENTRE_FIRST;
  case HoldReason::NONE:
  case HoldReason::GATE_SHUT:
  case HoldReason::CALIBRATING:
    return DriveNotice::NONE;
  }
  return DriveNotice::NONE; // not a hold reason: nothing to say
}

} // namespace hmi::drive_adapter
