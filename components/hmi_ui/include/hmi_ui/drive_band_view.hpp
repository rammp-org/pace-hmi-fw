#pragma once
// The Drive screen's own band: the speed readout and the three drive-profile buttons.

#include <array>
#include <cstdint>

#include "lvgl.h"

#include "hmi_ui/fn.hpp"
#include "hmi_ui/nav_port.hpp"

namespace hmi::ui {

/// One instance for the Drive screen. The profile buttons are touch-only (the stick is busy
/// driving here): a tap asks the MIB for that profile, and the button the MIB reports is drawn
/// CHECKED. Owns its two subjects (CS-UI-05): the profile the MIB reports and the speed in
/// tenths of mph. main's MibStatus handler writes them, and app_main initialises each before
/// its binds (V7).
class DriveBandView {
public:
  struct Config {
    /// The MIB::DriveProfile values of the three buttons, in bind order: Manual, Assist, Auto.
    std::array<int32_t, 3> profiles;
    /// Mirrors a profile out to the ADC task (app_state's drive_profile_published). Any context.
    void (*store_profile)(int32_t profile);
    /// The drive session's PROFILE_CLICK input (re-publishes the request with the new profile).
    /// UI task, lvgl_mutex held.
    Fn<void()> profile_clicked;
    /// Navigation: `mirror_states` makes a button's label follow its pressed and
    /// checked states.
    const NavPort *nav;
  };

  constexpr explicit DriveBandView(const Config &config) noexcept
      : config_(config) {}

  /// int: the MIB::DriveProfile the MIB reports (main initialises it, its MibStatus handler
  /// writes it under lvgl_mutex).
  [[nodiscard]] lv_subject_t *profile_subject() { return &profile_; }
  /// int: the speed in tenths of mph (hmi_format), written the same way.
  [[nodiscard]] lv_subject_t *speed_subject() { return &speed_tenths_; }

  /// @brief Binds the speed label to `speed_tenths` ("N.N", object-bound observer).
  /// app_main, before lv_task starts.
  void bind_speed(lv_obj_t *label);

  /// @brief Binds the three profile buttons, in this order: Manual, Assist, Auto (the
  ///        `profiles` order). Each gets its click callback, nav's state mirroring and an
  ///        object-bound observer on `profile`; a null button is skipped.
  /// app_main, before lv_task starts.
  void bind_profile_buttons(lv_obj_t *manual, lv_obj_t *assist, lv_obj_t *automatic);

  /// @brief Mirrors every reported profile out to the ADC task (an observer on `profile`, not
  ///        object-bound). It does not publish: the subject is fed from MibStatus.
  /// app_main, before lv_task starts.
  void bind_profile_mirror();

private:
  /// What one button's callbacks get as user data: the view and the button's profile.
  struct ProfileButton {
    const DriveBandView *view;
    int32_t profile;
  };

  void bind_profile_button(lv_obj_t *button, ProfileButton *slot);
  // UI task (LVGL event / observers; lvgl_mutex held, or the setter's task under it).
  static void profile_click_cb(lv_event_t *e);
  static void profile_button_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void profile_mirror_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void speed_label_observer(lv_observer_t *observer, lv_subject_t *subject);

  Config config_;
  lv_subject_t profile_{};      ///< int: MIB::DriveProfile
  lv_subject_t speed_tenths_{}; ///< int: tenths of mph
  std::array<ProfileButton, 3> buttons_{};
};

} // namespace hmi::ui
