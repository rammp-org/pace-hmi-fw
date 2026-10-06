// split_main.py fragment of main.cpp (frag_drive: original lines 1525-1726); one TU, see main.cpp.
static std::atomic<rammp::DriveRequest> drive_request{rammp::DriveRequest::DISABLE};
static int64_t drive_wait_until_us; // 0 = not waiting for an answer
static int64_t drive_wait_warn_us;  // 0 = nothing left to say about that request
// Leaving is a request too: the MIB decides when driving stops, so the exit gesture
// asks and this is how long to wait before telling the user it was refused.
static int64_t drive_exit_until_us; // 0 = not waiting for the MIB to stop
// Latched separately from the deadline: a refused exit clears the deadline when it
// raises its warning, and the MIB granting the stop late must still read as the user
// leaving rather than as the chair stopping on its own.
static bool drive_exit_requested;
// The stop was asked by the burger key, not the exit hold: once the MIB grants
// it, the menu opens over the Locked screen, which is what the key was for.
static bool drive_exit_then_menu;

static void drive_publish() {
  rtps_comms_publish_drive(drive_request.load(), drive_profile_published.load());
}

static void drive_ask(rammp::DriveRequest request) {
  drive_request.store(request);
  drive_publish();
}

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
// drive_session_table.hpp and its TABLE.md). This part samples what a decision
// needs (drive_env), hands the session one input, and performs the actions it
// returns, in order, with the same LVGL and RTPS calls as before the extraction
// (drive_perform). Every input arrives on the LVGL task: rtps_poll_cb's tick, the
// hold completions, the nav callbacks and the unlock timer.
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
static hmi::drive_session::DriveSession drive_session;

// When SEND_ENABLE asked: ARM_WARN and ARM_GIVEUP count from that moment, as
// drive_request_enter did.
static int64_t drive_ask_at_us = 0;
// Whether this step's actions include OPEN_MENU_ON_ARRIVAL: the relock writes
// nav_menu_on_arrival either way before the Locked screen loads.
static bool drive_step_menu_on_arrival = false;

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

// What one input is decided on: the link and MIB subjects, the screen and the
// menu as they are now, and each deadline against now_us. DRIVING_OK and
// MCB_READY come out of it exactly as `driving` and mcb_ready() did.
static hmi::drive_session::Env drive_env(int64_t now_us) {
  return hmi::drive_session::Env{
      .link_connected = static_cast<RtpsLinkState>(lv_subject_get_int(&rtps_link_subject)) ==
                        RtpsLinkState::CONNECTED,
      .mib =
          drive_mib_of(static_cast<MIB::MibSystemState>(lv_subject_get_int(&mib_state_subject))),
      .screen = drive_screen_of(lv_screen_active()),
      .menu_open = nav_menu_open != nullptr,
      .exit_elapsed = now_us >= drive_exit_until_us,
      .warn_elapsed = now_us >= drive_wait_warn_us,
      .giveup_elapsed = now_us >= drive_wait_until_us,
  };
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

// One action of the session, with the call it has always been.
static void drive_perform(hmi::drive_session::Action action) {
  using hmi::drive_session::Action;
  switch (action) {
  case Action::NONE:
    break;
  case Action::SEND_ENABLE:
    drive_ask_at_us = esp_timer_get_time();
    drive_ask(rammp::DriveRequest::ENABLE);
    break;
  case Action::SEND_DISABLE:
    drive_ask(rammp::DriveRequest::DISABLE);
    break;
  case Action::PUBLISH_DRIVE:
    drive_publish();
    break;
  case Action::ARM_WARN:
    drive_wait_warn_us = drive_ask_at_us + kDriveAnswerUs;
    break;
  case Action::ARM_GIVEUP:
    drive_wait_until_us = drive_ask_at_us + kDriveWaitUs;
    break;
  case Action::CLEAR_WARN:
    drive_wait_warn_us = 0;
    break;
  case Action::CLEAR_GIVEUP:
    drive_wait_until_us = 0;
    break;
  case Action::ARM_EXIT_DEADLINE:
    drive_exit_until_us = esp_timer_get_time() + kDriveAnswerUs;
    break;
  case Action::CLEAR_EXIT_DEADLINE:
    drive_exit_until_us = 0;
    break;
  case Action::SET_EXIT_REQUESTED:
    drive_exit_requested = true;
    break;
  case Action::CLEAR_EXIT_REQUESTED:
    drive_exit_requested = false;
    break;
  case Action::SET_THEN_MENU:
    drive_exit_then_menu = true;
    break;
  case Action::CLEAR_THEN_MENU:
    drive_exit_then_menu = false;
    break;
  case Action::RING_WAIT:
    lock_visual_wait();
    break;
  case Action::RING_REST:
    lock_visual_rest();
    break;
  case Action::LOCK_OPEN_VISUAL:
    lock_open_visual();
    break;
  case Action::CANCEL_UNLOCK_TIMER:
    unlock_timer_cancel();
    break;
  case Action::START_UNLOCK_TIMER:
    unlock_timer_start();
    break;
  case Action::SET_UNLOCKED:
    lv_subject_set_int(&locked_subject, 0);
    break;
  case Action::SET_LOCKED:
    lv_subject_set_int(&locked_subject, 1);
    break;
  case Action::GO_LOCKED_SCREEN:
    // Before the load: an instant screen change runs nav_arrive inside it.
    nav_menu_on_arrival = drive_step_menu_on_arrival;
    locked_screen_go();
    break;
  case Action::GATE_UPDATE:
    nav_update_stick_gate();
    break;
  case Action::OPEN_MENU_ON_ARRIVAL:
    drive_step_menu_on_arrival = true;
    break;
  case Action::UNLOCK_TIMER_DONE:
    unlock_advance_timer = nullptr; // repeat_count 1: LVGL deletes it after its callback
    break;
  case Action::GO_DRIVE_SCREEN:
    _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, kUnlockDissolveMs, 0,
                      &ui_DriveScreen_screen_init);
    break;
  case Action::NAV_HOME:
    nav_home();
    break;
  case Action::SHOW_REFUSED_DRIVE:
    entry_refused_show(kRefusedDrive, kDriveRefusedShowMs);
    break;
  case Action::SHOW_REFUSED_SEAT:
    entry_refused_show(kRefusedSeat, kDriveRefusedShowMs);
    break;
  case Action::SHOW_NOT_GRANTED:
    entry_refused_show(kRefusedDriveNotGranted, kDriveRefusedShowMs);
    break;
  case Action::SHOW_DRIVE_STOPPED:
    entry_refused_show(kRefusedDriveStopped, kDriveRefusedShowMs);
    break;
  case Action::SHOW_EXIT_REFUSED:
    entry_refused_show(kRefusedExit, kExitRefusedShowMs);
    break;
  case Action::SHOW_DRIVE_LOST:
    entry_refused_show(kRefusedDriveLost, kDriveRefusedShowMs);
    break;
  case Action::SHOW_REFUSED_DRIVE_MENU:
    entry_refused_show(kRefusedDriveMenu, kDriveRefusedShowMs);
    break;
  case Action::REFUSAL_FEEDBACK:
    refusal_feedback();
    break;
  default:
    break; // not an Action: DriveSession only returns its own enumerators
  }
}

// One input against an Env already sampled. Returns true when a row of the table
// acted (its actions were performed).
static bool drive_session_apply(hmi::drive_session::Input input,
                                const hmi::drive_session::Env &env) {
  hmi::drive_session::Actions actions{};
  if (!drive_session.step(input, env, actions)) {
    // A corrupted phase or input: the session is LOCKED and `actions` is its safe
    // state (DISABLE, Locked screen, gate shut), performed below. The logger is
    // made only on this path, so the normal path allocates nothing new.
    static espp::Logger drive_logger({.tag = "drive", .level = espp::Logger::Verbosity::INFO});
    drive_logger.error("corrupted drive session state, safe state entered (input {})",
                       static_cast<int>(input));
  }
  drive_step_menu_on_arrival = false;
  for (const hmi::drive_session::Action action : actions) {
    drive_perform(action);
  }
  return actions[0] != hmi::drive_session::Action::NONE;
}

// One input now (the hold completions, the nav callbacks, the unlock timer).
static bool drive_session_input(hmi::drive_session::Input input) {
  return drive_session_apply(input, drive_env(esp_timer_get_time()));
}

// TABLE.md U3, kept exactly as it was and outside the table (parked for the
// owner; removing it is a behaviour change): the exit hold has no lock check and
// its completion does not re-check applies(), so a relock that lands between the
// last hold poll and the end of the fill still asks the MIB to stop and latches
// the exit while locked, and that exit's deadline still raises its banner. The
// session is not told: no row reads drive_request's bit, and the next unlock
// (rows 1-2) clears the latch.
static void drive_exit_ask_while_locked() {
  drive_ask(rammp::DriveRequest::DISABLE);
  drive_exit_requested = true;
  drive_exit_then_menu = false;
  drive_exit_until_us = esp_timer_get_time() + kDriveAnswerUs;
}

static void drive_exit_due_while_locked(int64_t now_us) {
  if (drive_session.locked() && drive_exit_until_us != 0 && now_us >= drive_exit_until_us) {
    drive_exit_until_us = 0;
    drive_exit_then_menu = false;
    entry_refused_show(kRefusedExit, kExitRefusedShowMs);
  }
}

// The 250 ms tick, from rtps_poll_cb: follow the MIB's state first, then the
// three deadlines in order (TICK_SEQUENCE). `now` is read after follow-state has
// run, once for the three, as it always was; follow-state reads no deadline.
static void drive_wait_poll() {
  using hmi::drive_session::Input;
  (void)drive_session_apply(Input::TICK_FOLLOW, drive_env(esp_timer_get_time()));
  const int64_t now = esp_timer_get_time();
  const hmi::drive_session::Env env = drive_env(now);
  (void)drive_session_apply(Input::TICK_EXIT_DUE, env);
  drive_exit_due_while_locked(now); // U3, above
  (void)drive_session_apply(Input::TICK_WARN_DUE, env);
  (void)drive_session_apply(Input::TICK_GIVEUP_DUE, env);
}

// The unlock hold completed (unlock_gesture): refused on the spot when the MIB
// could not act on it; otherwise asked, with the ring going round until the MIB
// answers; ignored when already asked (rows 18-20).
static void drive_unlock_hold_done() {
  (void)drive_session_input(hmi::drive_session::Input::UNLOCK_HOLD_DONE);
}

// The exit hold completed (drive_exit_gesture): ask the MIB to stop (rows 21-24).
static void drive_exit_hold_done() {
  if (drive_session.locked()) {
    drive_exit_ask_while_locked(); // U3, above
    return;
  }
  (void)drive_session_input(hmi::drive_session::Input::EXIT_HOLD_DONE);
}

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
    .completed = [] { drive_exit_hold_done(); },
    // The button doubles as select, so a tap would visibly tick the bar and
    // snap back without this.
    .grace_ms = kBarGraceMs,
};

