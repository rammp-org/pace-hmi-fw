#pragma once
// The drive UI: what the Locked and Drive screens do around the drive session. The padlock (its
// ring and shackle) and the one-second advance to Drive after an unlock, and what the drive
// adapter's port (drive_port.hpp) acts on. Safety-relevant in part (CS-SAF-01): the port it
// serves sends the DriveCommand and performs the relocks the session decides.

#include <atomic>
#include <cstdint>

#include "lvgl.h"

#include "messages/joystick_message.hpp"
#include "messages/mib_message.hpp"

#include "drive_session.hpp"
#include "hmi_ui/shared_subjects.hpp"

namespace hmi::ui {

/// @brief Is the MCB telling us the chair is fit to drive: the link CONNECTED and the MIB
///        IDLE or ENABLED. Reads `mib_state`, then `rtps_link`.
/// UI task (or app_main), lvgl_mutex held.
[[nodiscard]] bool mcb_ready(const SharedSubjects &shared);
/// @brief Is the seat free to move: the link CONNECTED and the MIB IDLE exactly. Reads
///        `rtps_link`, then `mib_state`.
/// UI task (or app_main), lvgl_mutex held.
[[nodiscard]] bool seat_ready(const SharedSubjects &shared);

/// One instance (main's, constinit): the drive UI's state, and DrivePort's `Ui`. Every call runs
/// on the UI task with lvgl_mutex held (an LVGL timer, event or observer, or an input of the
/// drive adapter), or in app_main while it builds the UI before lv_task starts.
class DriveUi {
public:
  struct Config {
    const SharedSubjects *shared;   ///< `locked`, `mib_state` and `rtps_link` are read
    lv_subject_t *refused;          ///< int: Refused; RefusalView's `refused` (main's subject)
    lv_timer_t *(*refused_timer)(); ///< RefusalView's dwell timer (after its start_timer)
    lv_obj_t **menu_open;           ///< main's nav_menu_open: the overlay up, else null
    bool *menu_on_arrival;          ///< main's nav_menu_on_arrival: open it on the next load
    /// The drive profile the user picked, mirrored for the ADC task (main's
    /// drive_profile_published).
    const std::atomic<MIB::DriveProfile> *profile;
    /// The DriveCommand (main's rtps_comms_publish_drive). Result ignored (H6).
    bool (*publish_drive)(rammp::DriveRequest request, MIB::DriveProfile profile);
    /// The drive session's input (main's one DriveAdapter); true when its row acted.
    bool (*input)(hmi::drive_session::Input input);
    /// The stick gate: whether the stick may drive the chair now. Written here (the UI task),
    /// read by the ADC task, which sends the MCB a centred stick whenever it is false.
    std::atomic<bool> *stick_drives;
    void (*nav_home)();         ///< NavView::home
    void (*refusal_feedback)(); ///< "can't do that", heard and felt
    void (*haptic_click)();     ///< the STRONG_CLICK the unlock lands with
  };

  /// The arc of the ring that goes round while the MIB is asked, and one turn's time.
  static constexpr int32_t RING_WAIT_ARC = 25;
  static constexpr uint32_t RING_SPIN_MS = 900;
  /// Beat between the unlock landing and the Drive screen dissolving in, so the unlocked
  /// padlock is legible rather than a flash.
  static constexpr uint32_t UNLOCK_ADVANCE_MS = 1000;

  constexpr explicit DriveUi(const Config &config) noexcept
      : config_(config) {}

  /// The stick button's "let go first" flag, shared by the unlock and drive exit holds: is the
  /// button released since the last completed hold (HoldGesture::armed).
  [[nodiscard]] constexpr bool *button_armed() { return &button_armed_; }
  /// Calibrate's own "let go first" flag (the stick button or a finger on Calibrate).
  [[nodiscard]] constexpr bool *calibrate_armed() { return &calibrate_armed_; }

  /// @brief Reads the shackle's resting geometry from the export and readies the ring (its
  ///        range, not clickable). app_main, before lv_task starts.
  void init_lock();
  /// @brief The ring round the padlock follows `progress` (the unlock hold's fill) while
  ///        nothing else is showing on it. app_main, after `progress` is initialised (V7).
  void bind_ring(lv_subject_t *progress);

  // --- DrivePort's Ui (drive_port.hpp); UI task, lvgl_mutex held ---------------------------
  [[nodiscard]] lv_subject_t *rtps_link_subject() const { return config_.shared->rtps_link; }
  [[nodiscard]] lv_subject_t *mib_state_subject() const { return config_.shared->mib_state; }
  [[nodiscard]] lv_subject_t *locked_subject() const { return config_.shared->locked; }
  [[nodiscard]] lv_subject_t *entry_refused_subject() const { return config_.refused; }
  [[nodiscard]] lv_timer_t *refused_timer() const { return config_.refused_timer(); }
  [[nodiscard]] lv_obj_t *nav_menu_open() const { return *config_.menu_open; }
  [[nodiscard]] bool &nav_menu_on_arrival() { return *config_.menu_on_arrival; }
  /// Asked the MIB to drive, no answer yet: the ring is going round.
  [[nodiscard]] bool &lock_waiting() { return lock_waiting_; }
  /// The one-shot advance to Drive, armed by the unlock (null when not armed).
  [[nodiscard]] lv_timer_t *&unlock_advance_timer() { return unlock_advance_timer_; }
  [[nodiscard]] int32_t shackle_rest_y() const { return shackle_rest_y_; }
  [[nodiscard]] int32_t shackle_rest_h() const { return shackle_rest_h_; }
  [[nodiscard]] MIB::DriveProfile drive_profile() const { return config_.profile->load(); }
  bool publish_drive(rammp::DriveRequest request, MIB::DriveProfile profile) const {
    return config_.publish_drive(request, profile);
  }
  /// @brief Writes the stick gate from the lock, the screen and the menu as they are now
  ///        (drive_session's stick_drives). Called at exactly the GATE_TRIGGERS sites
  ///        (drive_session_table.hpp): the drive session's GATE_UPDATE and NavView's
  ///        `gate_update`.
  void update_stick_gate() const;
  void nav_home() const { config_.nav_home(); }
  void refusal_feedback() const { config_.refusal_feedback(); }
  void haptic_click() const { config_.haptic_click(); }
  /// The ring and the padlock back at rest: empty, shackle down.
  void lock_visual_rest();
  /// Asked the MIB and waiting: a quarter of the ring goes round until it answers.
  void lock_visual_wait();
  /// Arms the advance to Drive (UNLOCK_ADVANCE_MS, once).
  void unlock_timer_start();
  /// Cancels an armed advance: a re-lock inside its window would otherwise be undone by it.
  void unlock_timer_cancel();
  /// Hard cut to the Locked screen.
  static void locked_screen_go();
  /// The waiting ring's animation (its `exec_cb`, the handle lv_anim_delete matches on).
  static void ring_spin_cb(void *ring, int32_t angle);

private:
  static void unlock_advance_cb(lv_timer_t *timer);
  static void lock_ring_hold_observer(lv_observer_t *observer, lv_subject_t *subject);

  Config config_;
  // One-shot, armed by the unlock and cancelled by a re-lock that beats it. Held
  // so a re-lock (CANCEL_UNLOCK_TIMER) can delete it: a re-lock inside the UNLOCK_ADVANCE_MS
  // window would otherwise be immediately undone by the advance it left running.
  lv_timer_t *unlock_advance_timer_ = nullptr;
  bool lock_waiting_ = false;
  // The shackle's resting geometry, read from the export at wiring time.
  int32_t shackle_rest_y_ = 0;
  int32_t shackle_rest_h_ = 0;
  // Whether each input is currently released, and so free to start a new hold.
  //
  // This is the "let go first" rule, and it is per *input* rather than per
  // gesture on purpose: completing one gesture usually navigates straight to a
  // screen where another gesture watches the same input, and the user's thumb is
  // still where it was. Without this, the button hold that enters driving would
  // roll on into the Drive screen's exit hold, which watches the same button.
  bool button_armed_ = true;
  // Its own flag, not button_armed_: that one is shared with the drive exit hold, whose
  // poll sees the stick button up while a FINGER holds Calibrate and re-arms it every
  // 33 ms -- so a completed touch hold was armed again at once, and one long press
  // started a run and then cancelled it.
  bool calibrate_armed_ = true;
};

} // namespace hmi::ui
