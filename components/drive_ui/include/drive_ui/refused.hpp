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
};

/// How long one refusal stays up (ms).
inline constexpr uint32_t DRIVE_REFUSED_SHOW_MS = 3000;
/// A refused exit gets its own, shorter dwell (ms).
inline constexpr uint32_t EXIT_REFUSED_SHOW_MS = 2000;

} // namespace hmi::ui
