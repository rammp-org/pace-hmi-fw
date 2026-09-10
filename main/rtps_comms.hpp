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

/// Register the handler invoked on every actuator state sample from the MCB
/// (RAMMP_TOPIC_ACTUATOR_STATE). Call before rtps_comms_start(). Runs on the
/// RTPS receive task, so the same rule applies as for the status handler:
/// reach the UI only through a subject, holding the LVGL mutex.
void rtps_comms_on_actuator_state(std::function<void(const rammp_actuator_state_t &)> handler);

/// Ask the MCB to move one actuator by `steps` of its spec'd step size.
///
/// The HMI never moves an actuator itself — this is a request, and the answer
/// arrives asynchronously as an actuator-state sample carrying the same
/// `req_id`. Returns false (quietly) if the participant is not up or no peer
/// has been discovered, which is indistinguishable from a request the MCB
/// chose to ignore: either way the displayed value simply does not move.
bool rtps_comms_publish_actuator_command(uint8_t req_id, uint8_t actuator_id, int8_t steps);

/// McbStatus arrivals since the last rtps_comms_mcb_stats_reset(), for the
/// self test's period, gap and loss checks.
struct RtpsMcbStats {
  uint32_t samples = 0;   ///< samples decoded
  uint32_t lost = 0;      ///< samples missing from the seq sequence
  int64_t first_us = 0;   ///< esp_timer time of the first sample
  int64_t last_us = 0;    ///< esp_timer time of the latest sample
  int64_t max_gap_us = 0; ///< longest time between two consecutive samples
};
void rtps_comms_mcb_stats_reset();
RtpsMcbStats rtps_comms_mcb_stats();

/// Self-test hooks (see "Self test" in rammp_rtps_spec.h). Register before
/// rtps_comms_start(); the handlers run on the RTPS receive task.
void rtps_comms_on_selftest_run(std::function<void(uint8_t run_id)> handler);
/// `peer_rx` is the peer's count of pings received, or -1 when the pong was a
/// plain echo of the ping and carries no count.
void rtps_comms_on_selftest_pong(std::function<void(uint16_t seq, int peer_rx)> handler);
/// Both return false, quietly, until the participant is up and a peer matched.
bool rtps_comms_publish_selftest_ping(uint16_t seq);
bool rtps_comms_publish_selftest_report(const rammp_selftest_report_t &report);

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
