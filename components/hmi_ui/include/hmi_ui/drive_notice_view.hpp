#pragma once
// The Drive screen's notice slot (docs/plans/hazard-c1-spec.md §2.7, REQ-UI-17): what the drive
// adapter says about the user's stop and why the stick is held, in its own place on the Drive
// screen, separate from the refusal banner. Code-made (not the SquareLine export): one label
// over the top of the Drive screen's body, hidden while there is nothing to say.

#include <cstdint>

#include "lvgl.h"

#include "drive_notice.hpp"

namespace hmi::ui {

/// @brief The text the Drive screen shows for a notice (hmi_rtps_spec's kHmiNotice*); null for
///        NONE and for a value outside the enum. POST_NOT_PASSED reads "Start-up check not run"
///        (C1's text for the gate NOT_RUN).
/// Any context: pure.
[[nodiscard]] const char *drive_notice_text(hmi::drive_adapter::DriveNotice notice);

/// One instance (UiApp's). Owns its subject (CS-UI-05): int, the DriveNotice shown.
class DriveNoticeView {
public:
  /// The slot's geometry on the Drive screen: the top of its body (the export's DriveBody at
  /// y = 195), full width.
  static constexpr int32_t SLOT_Y = 195;
  static constexpr int32_t SLOT_PAD = 12;
  static constexpr uint32_t SLOT_BG = 0xFFB300; ///< amber: a warning, not a fault banner
  static constexpr uint32_t SLOT_TEXT = 0x101010;

  constexpr DriveNoticeView() noexcept = default;

  /// int: the DriveNotice shown (DriveNotice::NONE: nothing).
  [[nodiscard]] lv_subject_t *subject() { return &notice_; }

  /// @brief Inits the subject, makes the label on `screen` (the Drive screen) and binds it.
  /// app_main, before lv_task starts, after the Drive screen exists.
  void build(lv_obj_t *screen);

  /// @brief Shows a notice (the drive adapter's port, DrivePort::show_notice).
  /// UI task, lvgl_mutex held.
  void show(hmi::drive_adapter::DriveNotice notice);

private:
  static void observer(lv_observer_t *observer, lv_subject_t *subject);

  lv_subject_t notice_{};
  lv_obj_t *label_ = nullptr;
};

} // namespace hmi::ui
