#pragma once

#include <functional>

/**
 * @file rtps_comms.hpp
 * @brief W5500 SPI Ethernet bring-up + RTPS participant for the Tab5 HMI.
 *
 * Publishes an incrementing std_msgs/UInt32 counter on
 * `espp/rtps_example/request` and subscribes to `espp/rtps_example/response`,
 * matching the espp python harness (rtps_host.py) defaults. Also subscribes
 * to `espp/rtps_example/brightness` (UInt32, 0-100) for remote LCD brightness
 * control (see rtps_brightness.py).
 */

/// Register the handler invoked when a brightness command (percent, clamped
/// to 0-100) arrives on the brightness topic. Call before rtps_comms_start().
/// The handler runs on the RTPS receive task, not the LVGL thread.
void rtps_comms_on_brightness(std::function<void(float percent)> handler);

/// Bring up Ethernet, then start the RTPS participant + publish task in the
/// background as soon as DHCP assigns an IP (no timeout — also covers a cable
/// plugged in after boot). Returns quickly; progress is logged.
/// @return true if Ethernet bring-up succeeded, false otherwise —
///         the HMI keeps running without comms in that case.
bool rtps_comms_start();
