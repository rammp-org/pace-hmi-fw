#pragma once

// W5500 SPI Ethernet + the HMI's RTPS participant. Messages: messages/joystick_message.hpp
// (the shared rammp-rtps spec) plus this HMI's hmi_rtps_spec.hpp.

#include <cstdint>
#include <functional>
#include <string>

#include "hmi_rtps_spec.hpp"

/// The link to the MCB, worst to best (the TopBar RTPS indicator colours).
enum class RtpsLinkState {
  ETH_FAILED, ///< W5500 bring-up failed at boot
  LINK_DOWN,  ///< no Ethernet link
  NO_IP,      ///< link up, no DHCP lease
  NO_PEER,    ///< no SystemState within rammp::kMcbStatusTimeout (or never)
  CONNECTED,  ///< McbStatus arriving
};
RtpsLinkState rtps_comms_link_state();                          ///< any task
const char *rtps_comms_link_state_name(RtpsLinkState state);    ///< "NO_PEER"
std::string rtps_comms_link_state_meaning(RtpsLinkState state); ///< the likely cause

// Handlers: register before rtps_comms_start(). They run on the RTPS receive task,
// so they reach the UI only through subjects, under lvgl_mutex.
void rtps_comms_on_brightness(std::function<void(float percent)> handler);
void rtps_comms_on_system_state(std::function<void(const rammp::SystemState &)> handler);
void rtps_comms_on_seat_state(std::function<void(const rammp::SeatState &)> handler);
void rtps_comms_on_diagnostics(std::function<void(const rammp::Diagnostics &)> handler);
void rtps_comms_on_selftest_run(std::function<void(uint8_t run_id)> handler);
/// `peer_rx`: the peer's count of pings received; -1 when the pong was a plain echo.
void rtps_comms_on_selftest_pong(std::function<void(uint16_t seq, int peer_rx)> handler);

// Publishers: any task. They return false, quietly, until the participant is up
// and a peer has matched.
bool rtps_comms_publish_adc(float x, float y, float twist, rammp::Buttons buttons);
/// Ask the MCB to enable or disable driving, with the profile to drive with.
bool rtps_comms_publish_drive(rammp::DriveRequest request, rammp::DriveProfile profile);
/// Ask the MCB to put one seat axis at `target` (absolute, the axis' raw units).
bool rtps_comms_publish_seat(rammp::SeatAxis axis, int32_t target);
bool rtps_comms_publish_selftest_ping(uint16_t seq);
bool rtps_comms_publish_selftest_report(const rammp::SelfTestReport &report);

/// Diagnostics arrivals, for the DiagnosticsScreen. Any task.
struct RtpsDiagStats {
  int64_t last_us = 0;        ///< esp_timer time of the latest sample; 0 = never
  int32_t rate_tenths_hz = 0; ///< over the last 4 s; 0 with fewer than 2 samples
};
RtpsDiagStats rtps_comms_diag_stats();

/// SystemState arrivals since the last reset, for the self test.
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
bool rtps_comms_start();
