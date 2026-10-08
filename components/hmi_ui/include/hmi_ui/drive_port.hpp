#pragma once
// The drive adapter's port on the UI task (components/drive_adapter, `DrivePort`): each method
// is the LVGL or RTPS call its action has always been. Moved from main/frag_drive.inc
// (MainDriveView, lock_open_visual, entry_refused_show) without a change. Safety-relevant
// (CS-SAF-01): it performs the drive session's actions, the DriveCommand among them.
//
// A template over the drive UI it acts on (`Ui`: the firmware's DriveUi, drive_ui.hpp), so
// every call is a direct call (CS-SAF-08) and the drive goldens (tests/host/drive_golden) run
// this code as it is over a recording Ui. What `Ui` provides is listed on DrivePort.
//
// Included where the one DriveAdapter is made (main) and by the goldens. It needs `ui` (the
// export's screens and helpers) and `esp_timer`, which hmi_ui requires privately: an includer
// requires them too.

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "esp_timer.h"
#include "lvgl.h"
#include "messages/joystick_message.hpp"
#include "messages/mib_message.hpp"
#include "ui.h"

#include "drive_adapter.hpp"
#include "drive_session.hpp"
#include "hmi_ui/hold_range.hpp"
#include "hmi_ui/link_state.hpp"
#include "hmi_ui/refused.hpp"

namespace hmi::ui {

/// Spec 01b: the shackle rises this far (103 px -> 133 px), its bottom edge fixed.
inline constexpr int32_t SHACKLE_RISE_PX = 30;
/// The Drive screen's fade in, once the unlock has landed (ms).
inline constexpr uint32_t UNLOCK_DISSOLVE_MS = 280;

/// @brief The screen as the drive session sees it. Drive is tested first, so the stick gate's
///        test is exactly `lv_screen_active() == ui_DriveScreen`, as it always was.
/// Any context: compares pointers only.
[[nodiscard]] inline hmi::drive_session::Screen drive_screen_of(const lv_obj_t *screen) {
  using hmi::drive_session::Screen;
  if (screen == ui_DriveScreen) {
    return Screen::DRIVE;
  }
  if (screen == ui_LockedScreen) {
    return Screen::LOCKED;
  }
  if (screen == ui_SeatScreen) {
    return Screen::SEAT;
  }
  return screen == ui_BootScreen ? Screen::BOOT : Screen::OTHER;
}

/// @brief The MIB's state as the drive session sees it.
/// Any context: pure.
[[nodiscard]] inline hmi::drive_session::MibState drive_mib_of(MIB::MibSystemState state) {
  using hmi::drive_session::MibState;
  switch (state) {
  case MIB::MibSystemState::INITIALIZING:
    return MibState::INITIALIZING;
  case MIB::MibSystemState::IDLE:
    return MibState::IDLE;
  case MIB::MibSystemState::ENABLED:
    return MibState::ENABLED;
  case MIB::MibSystemState::ERROR:
    return MibState::OTHER;
  }
  return MibState::OTHER; // not a state the MIB has: neither ready nor driving
}

/// The drive adapter's port: stateless over the Ui it points to. Every method runs on the LVGL
/// task with lvgl_mutex held, inside an input of the drive adapter.
///
/// `Ui` provides, each a direct call on the UI task:
///  - the subjects: `rtps_link_subject()`, `mib_state_subject()`, `locked_subject()`,
///    `entry_refused_subject()`, and the refusal banners' dwell timer `refused_timer()`;
///  - the menu: `nav_menu_open()` (compared with nullptr), `nav_menu_on_arrival()` (assigned);
///  - the padlock: `lock_waiting()` and `unlock_advance_timer()` (assigned), `shackle_rest_y()`,
///    `shackle_rest_h()`, `lock_visual_wait()`, `lock_visual_rest()`, `unlock_timer_start()`,
///    `unlock_timer_cancel()`, `locked_screen_go()` and the ring's animation `Ui::ring_spin_cb`;
///  - the rest: `drive_profile()`, `publish_drive(request, profile)`, `update_stick_gate()`,
///    `nav_home()`, `refusal_feedback()`, `haptic_click()` (the STRONG_CLICK).
template <class Ui> class DrivePort {
public:
  constexpr explicit DrivePort(Ui *ui) noexcept
      : ui_(ui) {}

  // The link and MIB subjects, the screen and the menu as they are now, read in this order.
  [[nodiscard]] hmi::drive_adapter::DriveSample sample() const {
    return hmi::drive_adapter::DriveSample{
        .link_connected = static_cast<LinkState>(lv_subject_get_int(ui_->rtps_link_subject())) ==
                          LinkState::CONNECTED,
        .mib = drive_mib_of(
            static_cast<MIB::MibSystemState>(lv_subject_get_int(ui_->mib_state_subject()))),
        .screen = drive_screen_of(lv_screen_active()),
        .menu_open = ui_->nav_menu_open() != nullptr,
    };
  }
  [[nodiscard]] int64_t now_us() const { return esp_timer_get_time(); }
  // The DriveCommand, with the profile the user picked (one-shot, result ignored: H6).
  void publish(bool enable) const {
    ui_->publish_drive(enable ? rammp::DriveRequest::ENABLE : rammp::DriveRequest::DISABLE,
                       ui_->drive_profile());
  }
  void ring_wait() const { ui_->lock_visual_wait(); }
  void ring_rest() const { ui_->lock_visual_rest(); }
  // The MIB said ENABLED: spec 01b, a hard cut. The ring closes and the shackle
  // rises SHACKLE_RISE_PX with its bottom edge where it was; the unlock that follows
  // (START_UNLOCK_TIMER, SET_UNLOCKED) flips the band to ACTIVE and starts the
  // one-second hold before Drive dissolves in.
  void lock_open_visual() const {
    ui_->lock_waiting() = false;
    lv_anim_delete(ui_LockRing, Ui::ring_spin_cb);
    lv_arc_set_rotation(ui_LockRing, 270);
    lv_arc_set_value(ui_LockRing, HOLD_MAX);
    lv_obj_set_y(ui_Shackle, ui_->shackle_rest_y() - SHACKLE_RISE_PX);
    lv_obj_set_height(ui_Shackle, ui_->shackle_rest_h() + SHACKLE_RISE_PX);
    ui_->haptic_click();
  }
  void unlock_timer_start() const { ui_->unlock_timer_start(); }
  void unlock_timer_cancel() const { ui_->unlock_timer_cancel(); }
  // repeat_count 1: LVGL deletes the timer after its callback.
  void unlock_timer_forget() const { ui_->unlock_advance_timer() = nullptr; }
  void set_locked(bool locked) const { lv_subject_set_int(ui_->locked_subject(), locked ? 1 : 0); }
  void gate_update() const { ui_->update_stick_gate(); }
  // Set before GO_LOCKED_SCREEN: an instant screen change runs nav_arrive inside the load.
  void menu_on_arrival(bool open) const { ui_->nav_menu_on_arrival() = open; }
  void go_locked_screen() const { ui_->locked_screen_go(); }
  void go_drive_screen() const {
    _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, UNLOCK_DISSOLVE_MS, 0,
                      &ui_DriveScreen_screen_init);
  }
  void nav_home() const { ui_->nav_home(); }
  // Each banner with its dwell (show_refused), indexed by DriveBanner.
  void show_banner(hmi::drive_adapter::DriveBanner banner) const {
    static constexpr std::array<std::pair<int32_t, uint32_t>, 7> kBanners{{
        {REFUSED_DRIVE, DRIVE_REFUSED_SHOW_MS},
        {REFUSED_SEAT, DRIVE_REFUSED_SHOW_MS},
        {REFUSED_DRIVE_NOT_GRANTED, DRIVE_REFUSED_SHOW_MS},
        {REFUSED_DRIVE_STOPPED, DRIVE_REFUSED_SHOW_MS},
        {REFUSED_EXIT, EXIT_REFUSED_SHOW_MS},
        {REFUSED_DRIVE_LOST, DRIVE_REFUSED_SHOW_MS},
        {REFUSED_DRIVE_MENU, DRIVE_REFUSED_SHOW_MS},
    }};
    const auto index = static_cast<size_t>(banner);
    if (index < kBanners.size()) {
      show_refused(kBanners[index].first, kBanners[index].second);
    }
  }
  void refusal_feedback() const { ui_->refusal_feedback(); }

  // Both refusals read the same: see RefusalView::poll. The dwell is the caller's,
  // because a refused exit is asked to stay up for less time than a refused entry, and
  // the timer is shared - so the period has to be set on every raise, not once.
  void show_refused(int32_t which, uint32_t show_ms) const {
    lv_subject_set_int(ui_->entry_refused_subject(), which);
    lv_timer_set_period(ui_->refused_timer(), show_ms);
    lv_timer_reset(ui_->refused_timer());
    lv_timer_resume(ui_->refused_timer());
  }

private:
  Ui *ui_;
};

} // namespace hmi::ui
