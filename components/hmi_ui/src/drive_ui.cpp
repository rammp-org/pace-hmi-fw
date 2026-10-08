// DriveUi: the padlock and the advance to Drive (moved from main/frag_lock.inc and app_main's
// lock wiring); the port that performs the drive session's actions is drive_port.hpp.

#include "hmi_ui/drive_ui.hpp"

#include "ui.h"

#include "hmi_ui/hold_gesture.hpp"

// Locking and unlocking the HMI are the drive session's actions, performed by the drive
// adapter (components/drive_adapter, through DrivePort) in the order set_locked() used to
// run them:
//   unlock: CANCEL_UNLOCK_TIMER, START_UNLOCK_TIMER, SET_UNLOCKED, GATE_UPDATE
//   lock:   CANCEL_UNLOCK_TIMER, RING_REST, GO_LOCKED_SCREEN, SET_LOCKED, GATE_UPDATE
// They run on the LVGL task: writing the locked subject runs its observers
// synchronously and those touch widgets.
//
// Unlocking arms unlock_advance_cb, which opens Drive. Locking goes straight
// back to the Locked screen from wherever you were -- a hard cut, per the
// spec's rule that the move between LOCKED and ACTIVE is instant -- so a
// re-lock can never strand you on a screen that assumes the chair may move.

void hmi::ui::DriveUi::init_lock() {
  // Where the shackle sits at rest, so the 01b rise can be undone exactly.
  lv_obj_update_layout(ui_Shackle);
  shackle_rest_y_ = lv_obj_get_y(ui_Shackle);
  shackle_rest_h_ = lv_obj_get_height(ui_Shackle);
  lv_arc_set_range(ui_LockRing, 0, HOLD_MAX);
  lv_obj_remove_flag(ui_LockRing, LV_OBJ_FLAG_CLICKABLE);
}

void hmi::ui::DriveUi::bind_ring(lv_subject_t *progress) {
  lv_subject_add_observer_obj(progress, lock_ring_hold_observer, ui_LockRing, this);
}

// The Drive screen, dissolving in one second after the unlock landed: the
// session's UNLOCK_TIMER (UNLOCK_TIMER_DONE forgets the timer, GO_DRIVE_SCREEN).
void hmi::ui::DriveUi::unlock_advance_cb(lv_timer_t *timer) {
  auto *self = static_cast<DriveUi *>(lv_timer_get_user_data(timer));
  (void)self->config_.input(hmi::drive_session::Input::UNLOCK_TIMER);
  // repeat_count 1: LVGL deletes the timer after this. UNLOCK_TIMER_DONE has
  // already forgotten it; this only keeps the pointer from dangling if a session
  // ever let it fire without a row (it cannot: the timer is armed only in the
  // phases whose rows take it).
  self->unlock_advance_timer_ = nullptr;
}

void hmi::ui::DriveUi::ring_spin_cb(void *ring, int32_t angle) {
  lv_arc_set_rotation(static_cast<lv_obj_t *>(ring), angle);
}

void hmi::ui::DriveUi::lock_visual_rest() {
  lock_waiting_ = false;
  lv_anim_delete(ui_LockRing, ring_spin_cb);
  lv_arc_set_rotation(ui_LockRing, 270);
  lv_arc_set_value(ui_LockRing, 0);
  lv_obj_set_y(ui_Shackle, shackle_rest_y_);
  lv_obj_set_height(ui_Shackle, shackle_rest_h_);
}

void hmi::ui::DriveUi::lock_visual_wait() {
  lock_waiting_ = true;
  lv_arc_set_value(ui_LockRing, RING_WAIT_ARC);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ui_LockRing);
  lv_anim_set_exec_cb(&a, ring_spin_cb);
  lv_anim_set_values(&a, 270, 270 + 360);
  lv_anim_set_duration(&a, RING_SPIN_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

// While nothing else is showing, the ring follows the button hold.
void hmi::ui::DriveUi::lock_ring_hold_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *self = static_cast<const DriveUi *>(lv_observer_get_user_data(observer));
  if (!self->lock_waiting_ && lv_subject_get_int(self->config_.shared->locked) != 0) {
    lv_arc_set_value(ui_LockRing, lv_subject_get_int(subject));
  }
}

void hmi::ui::DriveUi::unlock_timer_cancel() {
  if (unlock_advance_timer_) {
    lv_timer_delete(unlock_advance_timer_);
    unlock_advance_timer_ = nullptr;
  }
}

void hmi::ui::DriveUi::unlock_timer_start() {
  unlock_advance_timer_ = lv_timer_create(unlock_advance_cb, UNLOCK_ADVANCE_MS, this);
  lv_timer_set_repeat_count(unlock_advance_timer_, 1);
}

void hmi::ui::DriveUi::locked_screen_go() {
  _ui_screen_change(&ui_LockedScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, &ui_LockedScreen_screen_init);
}
