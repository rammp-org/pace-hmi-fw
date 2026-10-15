#pragma once

/**
 * @file ui_ports.hpp
 * @brief The ports the UiApp reaches the rest of the firmware through (hmi_ui/app_ports.hpp),
 *        filled from what main and the components own: the link, the drive session's inputs,
 *        the self test, the board, the screens main keeps, the POST's facts. Composition. The
 *        tables are inline constexpr here because the UiApp's constexpr constructor reads them
 *        (main.cpp's constinit ui_app); the functions they point at are in ui_ports.cpp, or in
 *        main.cpp for the two screens that take the LVGL lock. Moved from main.cpp unchanged.
 */

#include <cstdint>

#include "about_ui.hpp"
#include "bench_hooks.hpp"
#include "board/board_port.hpp"
#include "drive_session.hpp"
#include "hmi_ui/app_ports.hpp"
#include "hmi_ui/post_stage.hpp"
#include "internet_ui.hpp"
#include "joystick_cal.hpp"
#include "log_view.hpp"
#include "post_facts/post_facts.hpp"
#include "rtps_comms.hpp"
#include "selftest.hpp"
#include "settings.hpp"
#include "stick/permit_types.hpp"
#include "stick_inject.hpp"
#include "update_ui.hpp"

/// @brief The TopBar's link label: what RTPS runs over. The design's "BT · WI-FI" is only a
/// placeholder; it is set from the setting before rtps_comms_start, then from the link it
/// brought up (WiFi with no network known runs Ethernet). NetLink -> hmi_format's Link
/// (REQ-FMT-07): anything but WiFi reads Ethernet.
/// @param link The link.
/// @return The label's text.
const char *link_text(NetLink link);

/// @brief The Restart HMI tile: logs (tag "actions"), then esp_restart. The UI task.
void action_restart_hmi();

/// @brief The calibration in use as the POST judges it (saved, and each axis in whole mV).
/// The UI task (the POST runner).
/// @return The calibration fact.
hmi::post::Calibration post_calibration();

/// @brief A drive-session input, to the one DriveAdapter. The UI task.
/// @param input The input.
/// @return Whether the session acted on it.
bool drive_session_input(hmi::drive_session::Input input);
/// @brief The drive session's tick, from the 250 ms UI poll.
void drive_wait_poll();
/// @brief The unlock hold completed.
void drive_unlock_hold_done();
/// @brief The drive-exit hold completed. Ask only: the session's relock (TICK_FOLLOW) closes
/// the screen once the MIB actually stops driving.
void drive_exit_hold_done();
/// @brief The hold poll: the stick's hold reason between two ticks, for the Drive notice
/// (C1 §2.7).
/// @param hold The stick's hold reason now.
void drive_refresh_notice(hmi::stick::HoldReason hold);

/// @brief The Log screen: TextArea1 shows what log_capture has kept; down at the newest line
/// leaves the log for the burger key. The UI build (app_main, before lv_task).
void log_screen_init();
/// @brief The Joystick screen's calibration (joystick_cal): its prompts blink on the RTPS
/// indicator's phase, and each captured step clicks. The UI build.
void calibration_screen_init();
/// @brief The joystick's LVGL keypad read: UiApp's. The LVGL task (espp's KeypadInput calls it).
/// @param up Set while the key is down; likewise the rest.
/// @param down See up.
/// @param left See up.
/// @param right See up.
/// @param enter See up.
/// @param escape See up.
void joystick_keypad_read(bool *up, bool *down, bool *left, bool *right, bool *enter, bool *escape);
/// @brief The InternetScreen: Ethernet or WiFi, the network list and the password page.
/// Defined in main.cpp (it hands the screen the LVGL lock). The UI build.
void internet_screen_init();
/// @brief The UpdateScreen: the GitHub releases, one release, an install running; then the
/// OTA image's confirm timer. Defined in main.cpp (the LVGL lock, an LVGL timer). The UI build.
void update_screen_init();

/// The UiApp's link port: net_link's state, Wi-Fi and diagnostics, the DriveCommand and
/// SeatCommand publishers.
inline constexpr hmi::ui::LinkPort kLinkPort{
    .state = [] { return static_cast<hmi::ui::LinkState>(rtps_comms_link_state()); },
    .seen =
        [](hmi::ui::LinkState state) {
          rtps_comms_log_link_change(static_cast<RtpsLinkState>(state));
        },
    .wifi = [] { return rtps_comms_net_link() == NetLink::WIFI; },
    .setting_text =
        [] { return link_text(static_cast<NetLink>(settings_get(SETTINGS_PARAM_NETWORK))); },
    .diag_stats =
        [](int64_t *last_us, int32_t *rate_tenths_hz) {
          const RtpsDiagStats stats = rtps_comms_diag_stats();
          *last_us = stats.last_us;
          *rate_tenths_hz = stats.rate_tenths_hz;
        },
    .publish_drive = rtps_comms_publish_drive,
    .drive_writer_matched = rtps_comms_drive_writer_matched,
    // SEATFAIL (REQ-RUI-09) in a stick-injection build; the release ELF has none of it.
    .publish_seat = BENCH_STICK_INJECT ? bench_verb_seat_publish : rtps_comms_publish_seat,
};

/// The drive session's inputs: the one DriveAdapter (main.cpp; CS-SAF-08: these are its only
/// targets).
inline constexpr hmi::ui::DriveInputs kDriveInputs{
    .input = drive_session_input,
    .tick = drive_wait_poll,
    .unlock_hold_done = drive_unlock_hold_done,
    .exit_hold_done = drive_exit_hold_done,
    .refresh_notice = drive_refresh_notice,
};

/// The UiApp's self-test port: the overlay, its dismissal, a local run.
inline constexpr hmi::ui::SelfTestPort kSelfTestPort{
    .overlay_visible = selftest_ui_visible,
    .dismiss = selftest_ui_dismiss,
    .run = [] { (void)selftest_request(SelfTestTrigger::LOCAL, 0); },
};

/// The UiApp's board port: the backlight, the panel swap, the Restart HMI tile.
inline constexpr hmi::ui::BoardPort kBoardPort{
    .backlight = hmi::board::backlight,
    .present = hmi::board::present,
    .restart = action_restart_hmi,
};

/// The screens whose adapters stay in main: Log, Internet, About, Update, the calibration.
inline constexpr hmi::ui::MainScreens kMainScreens{
    .log_init = log_screen_init,
    .log_group = log_view_group,
    .log_on_load = log_view_on_load,
    .internet_init = internet_screen_init,
    .internet_on_load = internet_ui_on_load,
    .about_init = about_ui_init,
    .about_on_load = about_ui_on_load,
    .update_init = update_screen_init,
    .update_on_load = update_ui_on_load,
    .calibration_init = calibration_screen_init,
    .calibration_toggle = joystick_cal_toggle,
    .calibrating = joystick_cal_running,
    .calibration_note = joystick_cal_set_note,
};

/// The quick POST's facts: ESP-IDF's from post_facts, the calibration's from joystick_cal.
inline constexpr hmi::ui::PostPort kPostPort{
    .now_ms = hmi::post_facts::now_ms,
    .reset_reason = hmi::post_facts::reset_reason,
    .image = hmi::post_facts::image,
    .calibration = post_calibration,
    .memory = hmi::post_facts::memory,
    .stacks = hmi::post_facts::stacks,
    .calibration_saved_now = joystick_cal_saved,
    .stick_task_running = hmi::post_facts::stick_task_running,
};
