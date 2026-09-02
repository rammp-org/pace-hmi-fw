#pragma once

#include <cstdint>
#include <functional>

#include "rammp_rtps_spec.h"

/**
 * @file rtps_comms.hpp
 * @brief W5500 SPI Ethernet bring-up + RTPS participant for the Tab5 HMI.
 *
 * Every topic, type name and message layout comes from rammp_rtps_spec.h —
 * the shared wire spec this firmware and the MCB both build against. This
 * participant:
 *   - publishes the joystick position on RAMMP_TOPIC_JOYSTICK_ADC,
 *   - subscribes to the MCB's status on RAMMP_TOPIC_MCB_STATUS,
 *   - and keeps the bench topics (counter/command/brightness) for bring-up.
 */

/// Where the HMI's link to the MCB currently stands, worst to best. The TopBar
/// RTPS indicator maps these to colours.
enum class RtpsLinkState {
  ETH_FAILED, ///< W5500 bring-up failed at boot; will not recover without one
  LINK_DOWN,  ///< no Ethernet link (cable out, switch down)
  NO_IP,      ///< link up, no DHCP lease yet
  NO_PEER,    ///< have an IP, but no MCB status inside the timeout window
  CONNECTED,  ///< MCB status arriving
};

/// Current link state. Cheap (a few atomics and one timestamp compare), safe
/// from any task.
///
/// NO_PEER deliberately covers both "never heard from the MCB" and "the MCB
/// went quiet": espp's matched callback is a latch that never resets, and
/// matching an endpoint does not mean anyone is publishing, so live samples
/// inside RAMMP_MCB_STATUS_TIMEOUT_MS are the only honest evidence of a peer.
RtpsLinkState rtps_comms_link_state();

/// Register the handler invoked when a brightness command (percent, clamped
/// to 0-100) arrives on the brightness topic. Call before rtps_comms_start().
/// The handler runs on the RTPS receive task, not the LVGL thread.
void rtps_comms_on_brightness(std::function<void(float percent)> handler);

/// Register the handler invoked on every status sample from the MCB
/// (RAMMP_TOPIC_MCB_STATUS). Call before rtps_comms_start(). The handler runs
/// on the RTPS receive task, so anything it does to the UI must go through a
/// subject while holding the LVGL mutex.
void rtps_comms_on_mcb_status(std::function<void(const rammp_mcb_status_t &status)> handler);

/// Publish one joystick ADC snapshot (millivolts) on RAMMP_TOPIC_JOYSTICK_ADC.
/// Safe to call from any task; returns false (without logging) until the
/// participant is running and a subscriber on the topic has been discovered.
bool rtps_comms_publish_adc(uint32_t x_mv, uint32_t y_mv, uint32_t twist_mv, uint32_t buttons,
                            uint32_t drive_mode);

/// Bring up Ethernet, then start the RTPS participant + publish task in the
/// background as soon as DHCP assigns an IP (no timeout — also covers a cable
/// plugged in after boot). Returns quickly; progress is logged.
/// @return true if Ethernet bring-up succeeded, false otherwise —
///         the HMI keeps running without comms in that case.
bool rtps_comms_start();
