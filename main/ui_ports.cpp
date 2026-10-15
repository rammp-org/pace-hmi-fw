/**
 * @file ui_ports.cpp
 * @brief What the UiApp's ports point at (ui_ports.hpp). Moved from main.cpp unchanged.
 */

#include "ui_ports.hpp"

#include <cmath>
#include <cstdint>

#include "app_instances.hpp"
#include "cues.hpp"
#include "esp_system.h"
#include "hmi_format/topbar.hpp"
#include "hmi_ui/app_state.hpp"
#include "logger.hpp"
#include "ui.h"

// rtps_comms' RtpsLinkState is what the UI's link subject carries, as hmi::ui::LinkState.
static constexpr bool same_link_state(RtpsLinkState a, hmi::ui::LinkState b) {
  return static_cast<int32_t>(a) == static_cast<int32_t>(b);
}
static_assert(same_link_state(RtpsLinkState::NET_FAILED, hmi::ui::LinkState::NET_FAILED));
static_assert(same_link_state(RtpsLinkState::LINK_DOWN, hmi::ui::LinkState::LINK_DOWN));
static_assert(same_link_state(RtpsLinkState::NO_IP, hmi::ui::LinkState::NO_IP));
static_assert(same_link_state(RtpsLinkState::NO_PEER, hmi::ui::LinkState::NO_PEER));
static_assert(same_link_state(RtpsLinkState::CONNECTED, hmi::ui::LinkState::CONNECTED));

// The TopBar's link label: what RTPS runs over. The design's "BT · WI-FI" is only
// a placeholder; it is set from the setting before rtps_comms_start, then from the
// link it brought up (WiFi with no network known runs Ethernet).
// NetLink -> hmi_format's Link (REQ-FMT-07): anything but WiFi reads Ethernet.
const char *link_text(NetLink link) {
  return hmi::format::link_text(link == NetLink::WIFI ? hmi::format::Link::WIFI
                                                      : hmi::format::Link::ETHERNET);
}

// The Restart HMI tile.
void action_restart_hmi() {
  static espp::Logger action_logger({.tag = "actions", .level = espp::Logger::Verbosity::INFO});
  action_logger.warn("restart requested from the actions screen");
  esp_restart();
}

// The POST's calibration fact, from joystick_cal.
hmi::post::Calibration post_calibration() {
  const JoystickCal cal = joystick_cal_current();
  const auto axis = [](const JoystickAxisCal &a) {
    return hmi::post::AxisCal{.min_mv = static_cast<int32_t>(std::lround(a.min_mv)),
                              .centre_mv = static_cast<int32_t>(std::lround(a.center_mv)),
                              .max_mv = static_cast<int32_t>(std::lround(a.max_mv))};
  };
  return {.saved = joystick_cal_saved(),
          .x = axis(cal[JOY_HORIZONTAL]),
          .y = axis(cal[JOY_VERTICAL]),
          .twist = axis(cal[JOY_TWIST])};
}

bool drive_session_input(hmi::drive_session::Input input) { return drive_adapter.input(input); }
void drive_wait_poll() { drive_adapter.tick(); }
void drive_unlock_hold_done() { drive_adapter.unlock_hold_done(); }
// Ask only. The session's relock (TICK_FOLLOW) closes the screen once the MIB actually stops
// driving.
void drive_exit_hold_done() { drive_adapter.exit_hold_done(); }
// The hold poll: the stick's hold reason between two ticks, for the Drive notice (C1 §2.7).
void drive_refresh_notice(hmi::stick::HoldReason hold) { drive_adapter.refresh_notice(hold); }

// The Log screen: TextArea1 shows what log_capture has kept. Down at the newest line leaves the
// log for the burger key.
void log_screen_init() {
  log_view_init();
  log_view_set_escape([] { ui_app.nav().to_key(); });
}

// The Joystick screen's calibration (joystick_cal): its prompts blink on the RTPS indicator's
// phase, and each captured step clicks.
void calibration_screen_init() {
  joystick_cal_init_ui({.screen = ui_JoystickScreen,
                        .button_label = ui_CalibrateButtonLabel,
                        .instructions = ui_JoystickHint,
                        .blink = &rtps_blink_subject,
                        .feedback = [] {
                          haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
                          play_click(espp::M5StackTab5::get());
                        }});
}
