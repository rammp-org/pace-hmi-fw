// split_main.py fragment of main.cpp (frag_drive: original lines 1525-1726); one TU, see main.cpp.
// Both refusals read the same: see entry_refusal_poll. The dwell is the caller's,
// because a refused exit is asked to stay up for less time than a refused entry, and
// the timer is shared - so the period has to be set on every raise, not once.
static void entry_refused_show(int32_t which, uint32_t show_ms) {
  lv_subject_set_int(&entry_refused_subject, which);
  lv_timer_set_period(entry_refused_timer, show_ms);
  lv_timer_reset(entry_refused_timer);
  lv_timer_resume(entry_refused_timer);
}

// The drive session: lock, ask, unlock, drive, ask to stop. The decisions live in
// components/drive_session (DriveSession, checked against the AS-IS table in
// drive_session_table.hpp and its TABLE.md); sampling, the deadlines and performing the
// actions live in components/drive_adapter (DriveAdapter). This part is the adapter's port
// in this unit, MainDriveView: the LVGL and RTPS calls each action has always been. Every
// input arrives on the LVGL task: rtps_poll_cb's tick, the hold completions, the nav
// callbacks and the unlock timer.
//
// What the session encodes, from the comments that used to sit on this code:
// - The DriveScreen is a view of one fact: the MIB is driving. It opens whenever
//   MibStatus says ENABLED and closes as soon as it stops saying so, whoever asked
//   -- this firmware's own DriveCommand is only how it asks the MIB to get there,
//   and the MIB can enter or leave driving without being asked (TABLE.md rows 1-9).
// - Stopped while unlocked: leaving on request needs no explanation; the MIB
//   stopping the chair by itself does, said on the screen the user lands on, and
//   the link going is not the MIB deciding anything (rows 3-9).
// - The seat screen needs the MCB as much as driving does: losing it there is a way
//   out, home with the reason (rows 10-11). Asked, and the answer window passed
//   without ENABLED: the ring stops going round (rows 12-13).
// - Asking to stop is a request too: the exit hold, or the burger key on Drive,
//   which opens the menu only once driving has stopped (rows 21-28). A refused
//   stop is said and the screen stays put; the request stays latched, so a stop
//   granted later is still the user's doing (row 14).

// The screen as the session sees it. Drive is tested first, so the stick gate's
// test is exactly `lv_screen_active() == ui_DriveScreen`, as it always was.
static hmi::drive_session::Screen drive_screen_of(const lv_obj_t *screen) {
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

static hmi::drive_session::MibState drive_mib_of(MIB::MibSystemState state) {
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

// The MIB said ENABLED: spec 01b, a hard cut. The ring closes and the shackle
// rises kShackleRisePx with its bottom edge where it was; the unlock that follows
// (START_UNLOCK_TIMER, SET_UNLOCKED) flips the band to ACTIVE and starts the
// one-second hold before Drive dissolves in.
static void lock_open_visual() {
  lock_waiting = false;
  lv_anim_delete(ui_LockRing, ring_spin_cb);
  lv_arc_set_rotation(ui_LockRing, 270);
  lv_arc_set_value(ui_LockRing, kHoldMax);
  lv_obj_set_y(ui_Shackle, shackle_rest_y - kShackleRisePx);
  lv_obj_set_height(ui_Shackle, shackle_rest_h + kShackleRisePx);
  haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
}

// The drive adapter's port in this unit (components/drive_adapter, README "The port"): each
// method is the LVGL or RTPS call its action has always been. Stateless. Every method runs on
// the LVGL task with lvgl_mutex held, inside an input of drive_adapter. In an unnamed
// namespace, so nothing of it (or of the adapter made over it) is visible outside this unit.
namespace {
struct MainDriveView {
  // The link and MIB subjects, the screen and the menu as they are now, read in this order.
  hmi::drive_adapter::DriveSample sample() const {
    return hmi::drive_adapter::DriveSample{
        .link_connected = static_cast<RtpsLinkState>(lv_subject_get_int(&rtps_link_subject)) ==
                          RtpsLinkState::CONNECTED,
        .mib = drive_mib_of(
            static_cast<MIB::MibSystemState>(lv_subject_get_int(&mib_state_subject))),
        .screen = drive_screen_of(lv_screen_active()),
        .menu_open = nav_menu_open != nullptr,
    };
  }
  int64_t now_us() const { return esp_timer_get_time(); }
  // The DriveCommand, with the profile the user picked (one-shot, result ignored: H6).
  void publish(bool enable) const {
    rtps_comms_publish_drive(enable ? rammp::DriveRequest::ENABLE : rammp::DriveRequest::DISABLE,
                             drive_profile_published.load());
  }
  void ring_wait() const { lock_visual_wait(); }
  void ring_rest() const { lock_visual_rest(); }
  void lock_open_visual() const { ::lock_open_visual(); }
  void unlock_timer_start() const { ::unlock_timer_start(); }
  void unlock_timer_cancel() const { ::unlock_timer_cancel(); }
  // repeat_count 1: LVGL deletes the timer after its callback.
  void unlock_timer_forget() const { unlock_advance_timer = nullptr; }
  void set_locked(bool locked) const { lv_subject_set_int(&locked_subject, locked ? 1 : 0); }
  void gate_update() const { nav_update_stick_gate(); }
  // Set before GO_LOCKED_SCREEN: an instant screen change runs nav_arrive inside the load.
  void menu_on_arrival(bool open) const { nav_menu_on_arrival = open; }
  void go_locked_screen() const { locked_screen_go(); }
  void go_drive_screen() const {
    _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, kUnlockDissolveMs, 0,
                      &ui_DriveScreen_screen_init);
  }
  void nav_home() const { ::nav_home(); }
  // Each banner with its dwell (entry_refused_show), indexed by DriveBanner.
  void show_banner(hmi::drive_adapter::DriveBanner banner) const {
    static constexpr std::array<std::pair<int32_t, uint32_t>, 7> kBanners{{
        {kRefusedDrive, kDriveRefusedShowMs},
        {kRefusedSeat, kDriveRefusedShowMs},
        {kRefusedDriveNotGranted, kDriveRefusedShowMs},
        {kRefusedDriveStopped, kDriveRefusedShowMs},
        {kRefusedExit, kExitRefusedShowMs},
        {kRefusedDriveLost, kDriveRefusedShowMs},
        {kRefusedDriveMenu, kDriveRefusedShowMs},
    }};
    const auto index = static_cast<size_t>(banner);
    if (index < kBanners.size()) {
      entry_refused_show(kBanners[index].first, kBanners[index].second);
    }
  }
  void refusal_feedback() const { ::refusal_feedback(); }
};
} // namespace
static_assert(hmi::drive_adapter::DrivePort<MainDriveView>);

// The drive session's adapter: the session, its deadlines and latches, and the logger for a
// corrupted state, made at start-up with the rest (CS-SAF-04). Its windows are the table's
// kDriveAnswer and kDriveWait (pinned to the RTPS spec in frag_refusal.inc).
static hmi::drive_adapter::DriveAdapter<MainDriveView> drive_adapter{{.view = MainDriveView{}}};

// The inputs the fragments before this one declare: one input now; the 250 ms tick from
// rtps_poll_cb; the unlock hold completed (unlock_gesture).
static bool drive_session_input(hmi::drive_session::Input input) {
  return drive_adapter.input(input);
}
static void drive_wait_poll() { drive_adapter.tick(); }
static void drive_unlock_hold_done() { drive_adapter.unlock_hold_done(); }

// Exits on the stick BUTTON, not on pulling the stick back: pulling back is how
// you drive in reverse, so a pull-to-exit gesture fired every time the user
// reversed. The button is the only input on this screen that means nothing to
// driving.
//
// This one survives the move to the burger menu because it is not navigation:
// it asks the MIB to stop. Leaving the screen by any other route does not.
static HoldGesture drive_exit_gesture{
    .armed = &joy_button_armed,
    .is_held = joy_button_held,
    .applies = [] { return lv_screen_active() == ui_DriveScreen && nav_menu_open == nullptr; },
    // Ask only. The session's relock (TICK_FOLLOW) closes the screen once the MIB actually
    // stops driving, so a MIB that does not stop cannot leave someone looking at the
    // main screen while the chair is still moving.
    .completed = [] { drive_timed(TimedPath::EXIT_HOLD, [] { drive_adapter.exit_hold_done(); }); },
    // The button doubles as select, so a tap would visibly tick the bar and
    // snap back without this.
    .grace_ms = kBarGraceMs,
};

