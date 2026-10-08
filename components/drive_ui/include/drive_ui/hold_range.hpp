#pragma once
// The range a hold fills (hmi_ui's hold_gesture.hpp), which is also the padlock ring's: the ring
// follows the unlock hold's fill (DriveUi::bind_ring) and closes at its end
// (DrivePort::lock_open_visual).
// No LVGL, so the drive port's goldens (tests/host/drive_golden) read the same value.

#include <cstdint>

namespace hmi::ui {

/// The fill's range (LVGL's default for an arc or bar).
inline constexpr int32_t HOLD_MAX = 100;

} // namespace hmi::ui
