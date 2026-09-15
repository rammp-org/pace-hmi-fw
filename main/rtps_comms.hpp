#pragma once

// W5500 SPI Ethernet + the HMI's RTPS participant. Topics and messages: rammp_rtps_spec.h.

#include <cstdint>
#include <functional>
#include <string>

#include "rammp_rtps_spec.h"

/// The link to the MCB, worst to best (the TopBar RTPS indicator colours).
enum class RtpsLinkState {
  ETH_FAILED, ///< W5500 bring-up failed at boot
  LINK_DOWN,  ///< no Ethernet link
  NO_IP,      ///< link up, no DHCP lease
  NO_PEER,    ///< no McbStatus within RAMMP_MCB_STATUS_TIMEOUT_MS (or never)
  CONNECTED,  ///< McbStatus arriving
};
RtpsLinkState rtps_comms_link_state();                          ///< any task
const char *rtps_comms_link_state_name(RtpsLinkState state);    ///< "NO_PEER"
std::string rtps_comms_link_state_meaning(RtpsLinkState state); ///< the likely cause

// Handlers: register before rtps_comms_start(). They run on the RTPS receive task,
// so they reach the UI only through subjects, under lvgl_mutex.
void rtps_comms_on_brightness(std::function<void(float percent)> handler);
void rtps_comms_on_mcb_status(std::function<void(const rammp::McbStatus &)> handler);
void rtps_comms_on_actuator_state(std::function<void(const rammp::ActuatorState &)> handler);
void rtps_comms_on_diagnostics(std::function<void(const rammp::Diagnostics &)> handler);
void rtps_comms_on_selftest_run(std::function<void(uint8_t run_id)> handler);
/// `peer_rx`: the peer's count of pings received; -1 when the pong was a plain echo.
void rtps_comms_on_selftest_pong(std::function<void(uint16_t seq, int peer_rx)> handler);

// Publishers: any task. They return false, quietly, until the participant is up
// and a peer has matched.
bool rtps_comms_publish_adc(float x, float y, float twist, uint32_t buttons, uint32_t drive_mode);
bool rtps_comms_publish_actuator_command(uint8_t req_id, uint8_t actuator_id, int8_t steps);
bool rtps_comms_publish_selftest_ping(uint16_t seq);
bool rtps_comms_publish_selftest_report(const rammp::SelfTestReport &report);

/// Diagnostics arrivals, for the DiagnosticsScreen. Any task.
struct RtpsDiagStats {
  int64_t last_us = 0;        ///< esp_timer time of the latest sample; 0 = never
  int32_t rate_tenths_hz = 0; ///< over the last 4 s; 0 with fewer than 2 samples
};
RtpsDiagStats rtps_comms_diag_stats();

<<<<<<< HEAD
/// Publish one normalized joystick snapshot on RAMMP_TOPIC_JOYSTICK_XY_TWIST.
/// Axis values are calibrated -1.0..1.0. Safe to call from any task; returns
/// false (without logging) until the participant is running and a subscriber on
/// the topic has been discovered.
bool rtps_comms_publish_xy_twist(float x, float y, float twist, uint32_t buttons,
                                 uint32_t drive_mode);

/// Bring up Ethernet, then start the RTPS participant + publish task in the
/// background as soon as DHCP assigns an IP (no timeout — also covers a cable
/// plugged in after boot). Returns quickly; progress is logged.
/// @return true if Ethernet bring-up succeeded, false otherwise —
///         the HMI keeps running without comms in that case.
=======
/// McbStatus arrivals since the last reset, for the self test.
struct RtpsMcbStats {
  uint32_t samples = 0;   ///< samples decoded
  uint32_t lost = 0;      ///< gaps in `seq`
  int64_t first_us = 0;   ///< esp_timer time of the first sample
  int64_t last_us = 0;    ///< esp_timer time of the latest sample
  int64_t max_gap_us = 0; ///< longest time between two samples
};
void rtps_comms_mcb_stats_reset();
RtpsMcbStats rtps_comms_mcb_stats();

/// Brings up Ethernet, then starts RTPS in the background once DHCP gives an IP
/// (no timeout, so a cable plugged in later works). False = no W5500; the HMI runs on.
>>>>>>> main
bool rtps_comms_start();
