#pragma once
// The Drive screen's own band: the speed readout and the three drive-profile buttons.

#include <array>
#include <cstdint>

#include "lvgl.h"

namespace hmi::ui {

/// One instance for the Drive screen. The profile buttons are touch-only (the stick is busy
/// driving here): a tap asks the MIB for that profile, and the button the MIB reports is drawn
/// CHECKED. Reads the `profile` and `speed_tenths` subjects, both initialised before any bind
/// (V7).
class DriveBandView {
public:
  struct Config {
    lv_subject_t *profile;      ///< int: the MIB::DriveProfile the MIB reports
    lv_subject_t *speed_tenths; ///< int: the speed in tenths of mph (hmi_format)
    /// The MIB::DriveProfile values of the three buttons, in bind order: Manual, Assist, Auto.
    std::array<int32_t, 3> profiles;
    /// Mirrors a profile out to the ADC task (main's drive_profile_published). Any context.
    void (*store_profile)(int32_t profile);
    /// The drive session's PROFILE_CLICK input (re-publishes the request with the new profile).
    /// UI task, lvgl_mutex held.
    void (*profile_clicked)();
    /// nav's state mirroring: the button's label follows its pressed and checked states.
    void (*mirror_states)(lv_obj_t *button);
  };

  constexpr explicit DriveBandView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Binds the speed label to `speed_tenths` ("N.N", object-bound observer).
  /// app_main, before lv_task starts.
  void bind_speed(lv_obj_t *label) const;

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
  std::array<ProfileButton, 3> buttons_{};
};

} // namespace hmi::ui
