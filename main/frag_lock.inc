// split_main.py fragment of main.cpp (frag_lock: original lines 1067-1192); one TU, see main.cpp.
// One-shot, armed by the unlock and cancelled by a re-lock that beats it. Held
// so set_locked(true) can delete it: a re-lock inside the kUnlockAdvanceMs
// window would otherwise be immediately undone by the advance it left running.
static lv_timer_t *unlock_advance_timer = nullptr;

// Asked the MIB to drive, no answer yet: the ring is going round.
static bool lock_waiting = false;
// The shackle's resting geometry, read from the export at wiring time.
static int32_t shackle_rest_y = 0;
static int32_t shackle_rest_h = 0;

// The Drive screen, dissolving in one second after the unlock landed.
static void unlock_advance_cb(lv_timer_t *) {
  unlock_advance_timer = nullptr; // repeat_count 1: LVGL deletes it after this
  _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, kUnlockDissolveMs, 0,
                    &ui_DriveScreen_screen_init);
}

static void ring_spin_cb(void *ring, int32_t angle) {
  lv_arc_set_rotation(static_cast<lv_obj_t *>(ring), angle);
}

// The ring and the padlock back at rest: empty, shackle down.
static void lock_visual_rest() {
  lock_waiting = false;
  lv_anim_delete(ui_LockRing, ring_spin_cb);
  lv_arc_set_rotation(ui_LockRing, 270);
  lv_arc_set_value(ui_LockRing, 0);
  lv_obj_set_y(ui_Shackle, shackle_rest_y);
  lv_obj_set_height(ui_Shackle, shackle_rest_h);
}

// Asked the MIB and waiting: a quarter of the ring goes round until it answers.
static void lock_visual_wait() {
  lock_waiting = true;
  lv_arc_set_value(ui_LockRing, kRingWaitArc);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ui_LockRing);
  lv_anim_set_exec_cb(&a, ring_spin_cb);
  lv_anim_set_values(&a, 270, 270 + 360);
  lv_anim_set_duration(&a, kRingSpinMs);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

// While nothing else is showing, the ring follows the button hold.
static void lock_ring_hold_observer(lv_observer_t *, lv_subject_t *subject) {
  if (!lock_waiting && lv_subject_get_int(&locked_subject) != 0) {
    lv_arc_set_value(ui_LockRing, lv_subject_get_int(subject));
  }
}

// Locks or unlocks the HMI. Callers must hold lvgl_mutex: writing a subject runs
// its observers synchronously on the calling task and those touch widgets.
// Callers already on the LVGL task (timers, event callbacks) are covered by the
// lock lv_task_handler() is called under.
//
// Unlocking arms unlock_advance_cb, which opens Drive. Locking goes straight
// back to the Locked screen from wherever you were -- a hard cut, per the
// spec's rule that the move between LOCKED and ACTIVE is instant -- so a
// re-lock can never strand you on a screen that assumes the chair may move.
static void set_locked(bool locked) {
  if (unlock_advance_timer) {
    lv_timer_delete(unlock_advance_timer);
    unlock_advance_timer = nullptr;
  }
  if (locked) {
    lock_visual_rest();
    _ui_screen_change(&ui_LockedScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                      &ui_LockedScreen_screen_init);
  } else {
    unlock_advance_timer = lv_timer_create(unlock_advance_cb, kUnlockAdvanceMs, nullptr);
    lv_timer_set_repeat_count(unlock_advance_timer, 1);
  }
  lv_subject_set_int(&locked_subject, locked ? 1 : 0);
  nav_update_stick_gate();
}

// Defined with the drive request it makes, further down.
static bool mcb_ready();
static void activate_drive();
static void lock_open();

// Holding the stick button on the Locked screen: how driving is asked for, the
// same hold that leaves it on Drive (and so the same "let go first" flag). Only
// while there is something to ask -- a MIB that is not ready gets the refusal
// once the press is a hold instead (entry_refusal_poll), rather than a ring
// that fills for a second and then says no.
static HoldGesture unlock_gesture{
    .armed = &joy_button_armed,
    .is_held = joy_button_held,
    .applies =
        [] {
          return lv_subject_get_int(&locked_subject) != 0 && !lock_waiting &&
                 lv_screen_active() == ui_LockedScreen && nav_menu_open == nullptr && mcb_ready();
        },
    .completed = [] { activate_drive(); },
    // The button doubles as select (a tap opens the menu from the key), so a
    // tap must not tick the ring.
    .grace_ms = kBarGraceMs,
};

/////////////////////////////////////////////////////////////////////////////
// Moving between screens
//
// Spec V2 navigates with the burger menu: the key at the bottom of every
// screen opens a full-screen overlay of destinations, and DRIVE in the band
// goes home from anywhere. A screen change is just a screen change (see
// docs/ui-architecture.md).
//
// Swaps go through the SquareLine helper rather than lv_screen_load, so they
// take the same path as the boot screen's own transition and re-create the
// target if it was ever destroyed.
/////////////////////////////////////////////////////////////////////////////

// SettingsScreen, defined with its rows further down. The actuators
// page comes after the settings_spec.hpp pages.
static constexpr int32_t kActuatorsPage = SETTINGS_PAGE_COUNT;
static void setting_page_open(int32_t page);
static void settings_screen_ensure();
static void actions_screen_ensure();
static void actions_open();
static void diagnostics_screen_ensure();
static void diagnostics_open();

