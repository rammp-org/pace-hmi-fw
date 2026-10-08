#pragma once
// The board's side of hmi_ui's BoardPort (app_ports.hpp): the backlight and the panel swap,
// on the BSP. Moved from main.cpp's port table; main still fills the table.

#include <cstdint>

namespace hmi::board {

/// @brief The backlight (M5StackTab5::brightness). Any task.
/// @param percent 0..100.
void backlight(float percent);

/// @brief The board's swap, for the flush: present_frame waits for vsync. Its result was
/// never used; a missed swap shows as one late frame. The LVGL task (DisplayFlip's flush).
/// @param frame The frame buffer to show.
void present(const uint8_t *frame);

} // namespace hmi::board
