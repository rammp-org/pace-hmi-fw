#pragma once
/// @file selftest_platform.hpp
/// @brief The board probes the self test reads (SelfTestPlatform), filled from the Tab5 BSP,
///        the haptics and the joystick calibration. Moved from main.cpp's app_main wiring.

#include <cstdint>
#include <vector>

#include "feedback/feedback.hpp"
#include "selftest.hpp"

/// @brief Every SelfTestPlatform probe except the LVGL lock, which stays the caller's to set.
/// @param feedback The haptic and sound cues (the DRV2605 status and click); must outlive the
///        self test, as app_main's does.
/// @param found_addresses The boot I2C probe's list (also whether the DA7280 answered).
/// @param direct_render Whether LVGL came up in DIRECT render mode.
/// @return The platform, for selftest_init. app_main, before selftest_init.
SelfTestPlatform selftest_platform(hmi::feedback::Feedback *feedback,
                                   const std::vector<uint8_t> &found_addresses, bool direct_render);
