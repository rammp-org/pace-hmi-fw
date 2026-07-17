#pragma once

/**
 * @file rtps_comms.hpp
 * @brief W5500 SPI Ethernet bring-up + RTPS participant for the Tab5 HMI.
 *
 * Publishes an incrementing std_msgs/UInt32 counter on
 * `espp/rtps_example/request` and subscribes to `espp/rtps_example/response`,
 * matching the espp python harness (rtps_host.py) defaults.
 */

/// Bring up Ethernet, then start the RTPS participant + publish task in the
/// background as soon as DHCP assigns an IP (no timeout — also covers a cable
/// plugged in after boot). Returns quickly; progress is logged.
/// @return true if Ethernet bring-up succeeded, false otherwise —
///         the HMI keeps running without comms in that case.
bool rtps_comms_start();
