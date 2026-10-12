#pragma once
// Which request was refused, and how long its banner stays up: the value of RefusalView's
// `refused` subject and the dwell each refusal is shown for. No LVGL, so the drive port's
// goldens (tests/host/drive_golden) read the same values.

#include <cstdint>

namespace hmi::ui {

/// Which request was refused: the value of the `refused` subject.
enum Refused : int32_t {
  REFUSED_NONE = 0,
  REFUSED_DRIVE = 1, ///< a push barred before it was sent; the cause is read live
  REFUSED_SEAT = 2,
  // These three are the MIB's own doing, so they stay up for their whole window rather than
  // clearing the moment the MCB is ready again: the chair being fine again is exactly what
  // makes them worth reading.
  REFUSED_DRIVE_NOT_GRANTED = 3, ///< asked to drive, never got ENABLED
  REFUSED_DRIVE_STOPPED = 4,     ///< was driving, the MIB stopped it
  REFUSED_EXIT = 5,              ///< asked to stop, the MIB is still driving
  REFUSED_DRIVE_LOST = 6,        ///< was driving, then the link went; cause read live
  /// Drive picked from the menu while the MCB could not drive. Like REFUSED_SEAT: no push
  /// holds it up, so it stays its window unless the cause clears.
  REFUSED_DRIVE_MENU = 7,
  /// C3: driving asked for (the unlock hold, a push on the Locked screen, the menu's DRIVE row)
  /// before the start-up check passed. Stays its window, or until the check passes.
  REFUSED_POST = 8,
  /// C2b: the drive table stopped because the stick is in FAULT (rows 55-56). Stays its window.
  REFUSED_STICK_FAULT = 9,
  /// C2b: driving asked for (the unlock hold, a push on the Locked screen, the menu's DRIVE row)
  /// with the stick in FAULT (rows 57-59). Stays its window, or until the fault clears.
  REFUSED_STICK = 10,
  /// The seat gate (hazard-seat-spec.md §2.8): a press on an axis whose value is not known.
  /// Stays its window, or until that axis is known again.
  REFUSED_SEAT_UNKNOWN = 11,
  /// The seat gate: the SeatCommand's publish failed (nothing reached the wire, no re-send).
  /// Stays its window.
  REFUSED_SEAT_NOT_SENT = 12,
};

/// @brief A `refused` value's name, for logs and the bench's STATE line ("NONE" for
///        REFUSED_NONE, "?" for a value that is none of them).
[[nodiscard]] constexpr const char *refused_name(int32_t refused) {
  switch (refused) {
  case REFUSED_NONE:
    return "NONE";
  case REFUSED_DRIVE:
    return "REFUSED_DRIVE";
  case REFUSED_SEAT:
    return "REFUSED_SEAT";
  case REFUSED_DRIVE_NOT_GRANTED:
    return "NOT_GRANTED";
  case REFUSED_DRIVE_STOPPED:
    return "DRIVE_STOPPED";
  case REFUSED_EXIT:
    return "EXIT_REFUSED";
  case REFUSED_DRIVE_LOST:
    return "DRIVE_LOST";
  case REFUSED_DRIVE_MENU:
    return "REFUSED_DRIVE_MENU";
  case REFUSED_POST:
    return "REFUSED_POST";
  case REFUSED_STICK_FAULT:
    return "STICK_FAULT";
  case REFUSED_STICK:
    return "REFUSED_STICK";
  case REFUSED_SEAT_UNKNOWN:
    return "REFUSED_SEAT_UNKNOWN";
  case REFUSED_SEAT_NOT_SENT:
    return "REFUSED_SEAT_NOT_SENT";
  default:
    return "?";
  }
}

/// How long one refusal stays up (ms).
inline constexpr uint32_t DRIVE_REFUSED_SHOW_MS = 3000;
/// A refused exit gets its own, shorter dwell (ms).
inline constexpr uint32_t EXIT_REFUSED_SHOW_MS = 2000;

} // namespace hmi::ui
