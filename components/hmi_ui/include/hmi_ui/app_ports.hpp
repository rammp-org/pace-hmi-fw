#pragma once
// What the UI island needs from the rest of the firmware, as ports by concern. Each port is a
// table of plain functions that main fills once (a constexpr table) by delegating to what it
// owns: rtps_comms, the drive adapter, the cues, the self test, the board and the screens whose
// adapters stay in main (main/*_ui.cpp, log_view, joystick_cal). UiApp reaches them only through
// its Config (CS-CMP-02); nothing here includes main.

#include <cstdint>

#include "lvgl.h"

#include "messages/joystick_message.hpp"
#include "messages/mib_message.hpp"

#include "drive_session.hpp"
#include "drive_ui/link_state.hpp"

namespace hmi::ui {

/// The RTPS link (main: rtps_comms).
struct LinkPort {
  LinkState (*state)();          ///< the link now (rtps_comms_link_state). UI task.
  void (*seen)(LinkState state); ///< every poll, before the subject is set (main logs a change)
  bool (*wifi)();                ///< the link runs over Wi-Fi (rtps_comms_net_link). UI task.
  /// The TopBar's link label for the link the network setting chose (read after settings_load,
  /// before RTPS starts; main corrects it once RTPS has brought a link up).
  const char *(*setting_text)();
  /// The diagnostics' arrival: the latest sample's esp_timer time (0 = never) and the rate.
  void (*diag_stats)(int64_t *last_us, int32_t *rate_tenths_hz);
  /// The DriveCommand (rtps_comms_publish_drive): safety-relevant. Result ignored (H6).
  bool (*publish_drive)(rammp::DriveRequest request, MIB::DriveProfile profile);
  /// One SeatCommand: an absolute target in the axis' units (rtps_comms_publish_seat).
  bool (*publish_seat)(rammp::SeatAxis axis, float target);
};

/// The drive session's inputs: main's one DriveAdapter, made over this UI's DriveUi
/// (UiApp::drive_ui). Safety-relevant; every call on the UI task with lvgl_mutex held.
struct DriveInputs {
  bool (*input)(hmi::drive_session::Input input); ///< one input; true when its row acted
  void (*tick)();                                 ///< the 250 ms tick
  void (*unlock_hold_done)();                     ///< the unlock hold completed
  void (*exit_hold_done)();                       ///< the drive exit hold completed
};

/// The user's haptic and sound cues (main: hmi::feedback::Feedback). UI task, or under
/// lvgl_mutex. None blocks: one short I2C burst, one stream-buffer send with a zero timeout.
struct CuesPort {
  void (*click)();               ///< the click; silent with Settings "Sounds" off
  void (*refusal)(bool warning); ///< "can't do that", heard; a warning sounds even with Sounds off
  void (*refused)();             ///< a refused press: the DRV2605's double click
  void (*haptic_click)();        ///< the STRONG_CLICK: a hold completing, the unlock landing
  void (*haptic_test)();         ///< the HAPTIC TEST tile's buzz
};

/// The self test (main: selftest). UI task.
struct SelfTestPort {
  bool (*overlay_visible)(); ///< its overlay is up: it owns the stick
  void (*dismiss)();         ///< the stick button closes the overlay once the run has finished
  void (*run)();             ///< a run from the Skunk Works tile (SelfTestTrigger::LOCAL)
};

/// The board and the system (main: the Tab5 BSP, esp_restart).
struct BoardPort {
  void (*backlight)(float percent);      ///< any task
  void (*present)(const uint8_t *frame); ///< the vsync-gated swap (DisplayFlip's flush)
  void (*restart)();                     ///< the Restart HMI tile: logs, then restarts
};

/// The screens whose adapters stay in main (main/*_ui.cpp, log_view, joystick_cal's view: worker
/// threads, the network, flash, the calibration run). Their wiring at build time and their
/// arrivals. UI task, or app_main while it builds the UI.
struct MainScreens {
  void (*log_init)();         ///< the Log screen, its escape down to the burger key
  lv_group_t *(*log_group)(); ///< the Log screen's group; null without memory for its text
  void (*log_on_load)();
  void (*internet_init)();
  void (*internet_on_load)();
  void (*about_init)();
  void (*about_on_load)();
  void (*update_init)(); ///< the Update screen, then the updated image's confirm timer
  void (*update_on_load)();
  void (*calibration_init)();   ///< the Joystick screen's calibration view
  void (*calibration_toggle)(); ///< starts a calibration run, or cancels one
};

} // namespace hmi::ui
