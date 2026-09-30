#pragma once

// The network link (W5500 SPI Ethernet, or WiFi through the Tab5's ESP32-C6) + the HMI's
// RTPS participant. Messages: messages/joystick_message.hpp (the shared rammp-rtps spec)
// plus this HMI's hmi_rtps_spec.hpp.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "hmi_rtps_spec.hpp"

/// What RTPS runs over: UI Settings -> Network, picked at boot (a change restarts the HMI).
enum class NetLink : uint8_t {
  ETHERNET, ///< the W5500 on the M5-Bus header
  WIFI,     ///< the ESP32-C6 over SDIO, joined to CONFIG_HMI_WIFI_SSID
};
const char *rtps_comms_net_link_name(NetLink link); ///< "Ethernet" / "WiFi"
/// An SSID is built in (CONFIG_HMI_WIFI_SSID): without one, WiFi falls back to Ethernet.
bool rtps_comms_wifi_configured();
/// The link in use, from rtps_comms_start() on. Any task.
NetLink rtps_comms_net_link();
/// Signal at the access point, dBm. nullopt on Ethernet or while not associated. Any task.
std::optional<int> rtps_comms_wifi_rssi();

/// The link to the MCB, worst to best (the TopBar RTPS indicator colours).
enum class RtpsLinkState {
  NET_FAILED, ///< the link's hardware (W5500 / ESP32-C6) failed to start
  LINK_DOWN,  ///< no Ethernet link / not associated with the WiFi network
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
// and a peer has matched.
bool rtps_comms_publish_adc(float x, float y, float twist, rammp::Buttons buttons);
/// Ask the MIB to enable or disable driving, with the profile to drive with.
bool rtps_comms_publish_drive(rammp::DriveRequest request, MIB::DriveProfile profile);
/// Ask the MIB to put one seat axis at `target` (absolute, in the axis' whole units).
bool rtps_comms_publish_seat(rammp::SeatAxis axis, float target);
bool rtps_comms_publish_selftest_ping(uint16_t seq);
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

/// Brings up `wanted` (WiFi only with an SSID built in, else Ethernet), then starts RTPS
/// in the background once DHCP gives an IP (no timeout, so a cable plugged in or an
/// access point switched on later works). False = no W5500; the HMI runs on. WiFi comes
/// up in the background, so its failure shows as NET_FAILED rather than here.
bool rtps_comms_start(NetLink wanted);
