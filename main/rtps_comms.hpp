#pragma once

// W5500 SPI Ethernet + the HMI's RTPS participant. Messages: messages/joystick_message.hpp
// (the shared rammp-rtps spec) plus this HMI's hmi_rtps_spec.hpp.

#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include "hmi_rtps_spec.hpp"

/// The link to the MCB, worst to best (the TopBar RTPS indicator colours).
enum class RtpsLinkState {
  ETH_FAILED, ///< W5500 bring-up failed at boot
  LINK_DOWN,  ///< no Ethernet link
  NO_IP,      ///< link up, no DHCP lease
  NO_PEER,    ///< no MibStatus within rammp::kMibStatusTimeout (or never)
  CONNECTED,  ///< McbStatus arriving
};
RtpsLinkState rtps_comms_link_state();                          ///< any task
const char *rtps_comms_link_state_name(RtpsLinkState state);    ///< "NO_PEER"
std::string rtps_comms_link_state_meaning(RtpsLinkState state); ///< the likely cause

// Handlers: register before rtps_comms_start(). They run on the RTPS receive task,
// so they reach the UI only through subjects, under lvgl_mutex.
void rtps_comms_on_brightness(std::function<void(float percent)> handler);
/// The MIB's whole state: what the chair is doing, where the seat is, the clock.
void rtps_comms_on_mib_status(std::function<void(const MIB::MibStatus &)> handler);
void rtps_comms_on_diagnostics(std::function<void(const rammp::Diagnostics &)> handler);
void rtps_comms_on_selftest_run(std::function<void(uint8_t run_id)> handler);
/// `peer_rx`: the peer's count of pings received; -1 when the pong was a plain echo.
void rtps_comms_on_selftest_pong(std::function<void(uint16_t seq, int peer_rx)> handler);

// Publishers: any task. They return false, quietly, until the participant is up
// and a peer has matched. While estop_active(): the stick goes out centred, a drive
// ENABLE goes out as DISABLE (and one DISABLE goes out the moment the stop engages,
// from the stick's task), and seat requests are not sent.
bool rtps_comms_publish_adc(float x, float y, float twist, rammp::Buttons buttons);
/// Ask the MIB to enable or disable driving, with the profile to drive with.
bool rtps_comms_publish_drive(rammp::DriveRequest request, MIB::DriveProfile profile);
/// Ask the MIB to put one seat axis at `target` (absolute, in the axis' whole units).
bool rtps_comms_publish_seat(rammp::SeatAxis axis, float target);
bool rtps_comms_publish_selftest_ping(uint16_t seq);

// E-stop (estop.cpp encodes and decodes; this is only the transport). The EStop
// reader hands every sample to estop_on_sample(). Publishing does not wait for a
// matched peer, and with a topic name short enough for std::string's in-place buffer
// it does not allocate on this side of espp.
/// False until the participant is up and the EStop writer exists. Any task.
bool rtps_comms_estop_ready();
/// One encoded rammp::EStop (encapsulation header included). False = not sent.
bool rtps_comms_publish_estop(std::span<const uint8_t> cdr_payload);
bool rtps_comms_publish_selftest_report(const rammp::SelfTestReport &report);

/// Diagnostics arrivals, for the DiagnosticsScreen. Any task.
struct RtpsDiagStats {
  int64_t last_us = 0;        ///< esp_timer time of the latest sample; 0 = never
  int32_t rate_tenths_hz = 0; ///< over the last 4 s; 0 with fewer than 2 samples
};
RtpsDiagStats rtps_comms_diag_stats();

/// MibStatus arrivals since the last reset, for the self test.
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
