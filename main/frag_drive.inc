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

// The DriveScreen is a view of one fact: the MIB is driving. It opens whenever
// MibStatus says ENABLED and closes as soon as it stops saying so, whoever asked --
// this firmware's own DriveCommand is only how it asks the MIB to get there, and the
// MIB can enter or leave driving without being asked. Anything else lets the screen
// disagree with the chair, which is the one thing it must never do.
//
// Closing wants a live link as well as a state: with the link down the last state is
// not news, so the screen stays up and drive_screen_warning_observer says the link is
// gone. Tipping someone off the driving screen on a blip is worse than the banner.
static void drive_screen_follow_state() {
  const bool connected = static_cast<RtpsLinkState>(lv_subject_get_int(&rtps_link_subject)) ==
                         RtpsLinkState::CONNECTED;
  const auto state = static_cast<MIB::MibSystemState>(lv_subject_get_int(&mib_state_subject));
  const bool driving = connected && state == MIB::MibSystemState::ENABLED;
  const bool locked = lv_subject_get_int(&locked_subject) != 0;

  if (driving && locked) {
    // The chair drives: the HMI has to say so, asked or not. The screen stays
    // where it is -- spec V2 keeps drive ACTIVE across the menu and the other
    // screens, and the stick only drives from Drive (stick_drives) -- but
    // coming from Locked, the unlock plays and Drive follows.
    drive_wait_until_us = 0; // whatever was being waited for, it happened
    drive_wait_warn_us = 0;  // and there is nothing to complain about
    drive_exit_until_us = 0; // any older request to stop is stale
    drive_exit_requested = false;
    drive_exit_then_menu = false;
    lock_open();
    return;
  }
  if (!driving && !locked) {
    // Stopped. Asked for, or done to us? Leaving on request needs no
    // explanation; the MIB stopping the chair by itself does, and it has to be
    // said on the screen the user lands on.
    const bool asked = drive_exit_requested;
    const bool then_menu = asked && drive_exit_then_menu;
    drive_exit_until_us = 0;
    drive_exit_requested = false;
    drive_exit_then_menu = false;
    // Before set_locked: an instant screen change loads the Locked screen, and
    // runs nav_arrive, inside it.
    nav_menu_on_arrival = then_menu;
    set_locked(true);
    if (!asked) {
      // The link going is not the MIB deciding anything: say which it was.
      // kRefusedDriveLost reads the cause live ("RTPS LINK") and clears when
      // the MCB is back; kRefusedDriveStopped is the MIB's own decision.
      entry_refused_show(connected ? kRefusedDriveStopped : kRefusedDriveLost, kDriveRefusedShowMs);
    }
    return;
  }
  // The seat screen needs the MCB as much as driving does. Losing it there is
  // a way out, not a screen left looking live: home, with the reason.
  if (lv_screen_active() == ui_SeatScreen && !mcb_ready()) {
    nav_home();
    entry_refused_show(kRefusedSeat, kDriveRefusedShowMs);
    return;
  }
  // Asked, and the answer window has passed without ENABLED: the refusal is on
  // the banner (drive_wait_poll), so the ring stops going round.
  if (locked && lock_waiting && drive_wait_warn_us == 0) {
    lock_visual_rest();
  }
}

static void drive_wait_poll() {
  drive_screen_follow_state();
  const int64_t now = esp_timer_get_time();
  if (drive_exit_until_us != 0 && now >= drive_exit_until_us) {
    // Asked to stop and the chair is still driving. Say so and stay put: the screen
    // has to keep agreeing with the chair, however much the user wants to leave. The
    // request stays latched, so a stop granted later is still their doing.
    drive_exit_until_us = 0;
    drive_exit_then_menu = false; // refused: no menu, even if it stops later
    entry_refused_show(kRefusedExit, kExitRefusedShowMs);
  }
  if (drive_wait_warn_us != 0 && now >= drive_wait_warn_us) {
    // A MibStatus has been and gone and the chair still is not driving. That is as
    // much as can be known this early, and it is enough to say so.
    drive_wait_warn_us = 0;
    entry_refused_show(kRefusedDriveNotGranted, kDriveRefusedShowMs);
  }
  if (drive_wait_until_us == 0 || now < drive_wait_until_us) {
    return; // not waiting on a request, or still inside its window
  }
  // Long since told the user; this is only about not leaving a live request behind.
  drive_wait_until_us = 0;
  drive_ask(rammp::DriveRequest::DISABLE);
}

// Asking the MIB to start driving: the stick button held on the Locked screen
// (unlock_gesture, through activate_drive).
//
// Ask, then wait: drive_screen_follow_state opens the screen when the MIB says
// it is driving, and the deadline is only how long to wait before giving up.
static void drive_request_enter() {
  if (!mcb_ready()) {
    entry_refused_show(kRefusedDrive, kDriveRefusedShowMs);
    return;
  }
  const int64_t now = esp_timer_get_time();
  drive_ask(rammp::DriveRequest::ENABLE);
  drive_wait_warn_us = now + kDriveAnswerUs;
  drive_wait_until_us = now + kDriveWaitUs;
}

// The unlock hold completed. Refused on the spot when the MIB
// could not act on it; otherwise asked, with the ring going round until the MIB
// answers (drive_screen_follow_state opens the lock on ENABLED, and stops the
// ring if the answer window passes without one).
static void activate_drive() {
  if (lv_subject_get_int(&locked_subject) == 0 || lock_waiting) {
    return; // already driving, or already asked
  }
  if (!mcb_ready()) {
    lock_visual_rest();
    entry_refused_show(kRefusedDrive, kDriveRefusedShowMs);
    refusal_feedback();
    return;
  }
  lock_visual_wait();
  drive_request_enter();
}

// The MIB said ENABLED: spec 01b, a hard cut. The ring closes, the shackle rises
// kShackleRisePx with its bottom edge where it was, and
// set_locked(false) flips the band to ACTIVE and starts the one-second hold
// before Drive dissolves in.
static void lock_open() {
  lock_waiting = false;
  lv_anim_delete(ui_LockRing, ring_spin_cb);
  lv_arc_set_rotation(ui_LockRing, 270);
  lv_arc_set_value(ui_LockRing, kHoldMax);
  lv_obj_set_y(ui_Shackle, shackle_rest_y - kShackleRisePx);
  lv_obj_set_height(ui_Shackle, shackle_rest_h + kShackleRisePx);
  haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
  set_locked(false);
}

// Asks the MIB to stop driving: the exit hold below, or the burger key on the
// Drive screen (then_menu), which may open the menu only once driving has
// stopped. drive_screen_follow_state leaves the screen when the MIB does stop;
// drive_wait_poll says so if it does not.
static void drive_exit_ask(bool then_menu) {
  drive_ask(rammp::DriveRequest::DISABLE);
  drive_exit_requested = true;
  drive_exit_then_menu = then_menu;
  drive_exit_until_us = esp_timer_get_time() + kDriveAnswerUs;
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
    // Ask only. drive_screen_follow_state closes the screen once the MIB actually
    // stops driving, so a MIB that does not stop cannot leave someone looking at the
    // main screen while the chair is still moving.
    .completed = [] { drive_exit_ask(false); },
    // The button doubles as select, so a tap would visibly tick the bar and
    // snap back without this.
    .grace_ms = kBarGraceMs,
};

