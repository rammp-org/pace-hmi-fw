// split_main.py fragment of main.cpp (frag_refusal: original lines 1193-1524); one TU, see main.cpp.
// Is the MCB currently telling us the chair is fit to drive, or to move the
// seat? Requires a live
// link as well as an OK state: if the status is stale we do not KNOW the state,
// and "unknown" is not "known good" on a device that moves someone.
static bool mcb_ready() {
  const auto state = static_cast<MIB::MibSystemState>(lv_subject_get_int(&mib_state_subject));
  return static_cast<RtpsLinkState>(lv_subject_get_int(&rtps_link_subject)) ==
             RtpsLinkState::CONNECTED &&
         (state == MIB::MibSystemState::IDLE || state == MIB::MibSystemState::ENABLED);
}

// The MIB disables manual seat control while it is driving, so the seat screen asks for
// more than mcb_ready(): IDLE exactly. INITIALIZING and ERROR are barred by both.
static bool seat_ready() {
  return static_cast<RtpsLinkState>(lv_subject_get_int(&rtps_link_subject)) ==
             RtpsLinkState::CONNECTED &&
         static_cast<MIB::MibSystemState>(lv_subject_get_int(&mib_state_subject)) ==
             MIB::MibSystemState::IDLE;
}

/////////////////////////////////////////////////////////////////////////////
// Saying why driving is not permitted
//
// One cause, several ErrorBanners. On the Locked screen, and on the screens a
// menu refusal can happen over: why a request to drive, or to open Seat
// Functions, was refused - the unlock hold is gated in applies(), so a refused
// one otherwise does nothing at all. On the DriveScreen and the SeatScreen: why
// it was cut short, by the link dropping or the MCB faulting. All word the
// cause from hmi_rtps_spec.hpp.
//
// entry_refused_subject records only THAT a request was refused. The banners work
// out WHY from the link and state subjects whenever any of them changes, so
// they always name the current cause - cable back in but no DHCP lease yet,
// say - rather than the one at the moment it first went wrong.
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kDriveRefusedShowMs = 3000; // how long one refusal stays up
static constexpr uint32_t kExitRefusedShowMs = 2000;  // a refused exit gets its own dwell
// Which push was refused, so the panel can say which.
enum : int32_t {
  kRefusedNone = 0,
  kRefusedDrive = 1, // a push barred before it was sent; the cause is read live
  kRefusedSeat = 2,
  // These three are the MIB's own doing, so they stay up for their whole window
  // rather than clearing the moment mcb_ready() comes back true - the chair being
  // fine again is exactly what makes them worth reading.
  kRefusedDriveNotGranted = 3, // asked to drive, never got ENABLED
  kRefusedDriveStopped = 4,    // was driving, the MIB stopped it
  kRefusedExit = 5,            // asked to stop, the MIB is still driving
  kRefusedDriveLost = 6,       // was driving, then the link went; cause read live
  // Drive picked from the menu while the MCB could not drive. Like kRefusedSeat:
  // no push holds it up, so it stays its window unless the cause clears.
  kRefusedDriveMenu = 7,
};
static lv_subject_t entry_refused_subject; // kRefused*; panel up unless None
// Created paused and re-armed by each refusal, like haptic_label_timer, so a
// second push restarts the countdown rather than stacking a timer.
static lv_timer_t *entry_refused_timer = nullptr;

static_assert(sizeof(rammp::kHmiEthFailedText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiLinkDownText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiWifiFailedText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiWifiDownText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiNoIpText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiNoPeerText) <= rammp::kErrorTextLen,
              "refusal body outgrows the banner it shares with MCB faults");
static_assert(sizeof(rammp::kHmiEthFailedFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiLinkDownFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiWifiFailedFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiWifiDownFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiNoIpFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiNoPeerFooter) <= rammp::kErrorFooterLen,
              "refusal footer outgrows the banner it shares with MCB faults");

struct RefusalText {
  const char *body;
  const char *footer;
};

static RefusalText link_refusal_text(RtpsLinkState link) {
  const bool wifi = rtps_comms_net_link() == NetLink::WIFI;
  switch (link) {
  case RtpsLinkState::NET_FAILED:
    return wifi ? RefusalText{rammp::kHmiWifiFailedText, rammp::kHmiWifiFailedFooter}
                : RefusalText{rammp::kHmiEthFailedText, rammp::kHmiEthFailedFooter};
  case RtpsLinkState::LINK_DOWN:
    return wifi ? RefusalText{rammp::kHmiWifiDownText, rammp::kHmiWifiDownFooter}
                : RefusalText{rammp::kHmiLinkDownText, rammp::kHmiLinkDownFooter};
  case RtpsLinkState::NO_IP:
    return {rammp::kHmiNoIpText, rammp::kHmiNoIpFooter};
  case RtpsLinkState::NO_PEER:
    return {rammp::kHmiNoPeerText, rammp::kHmiNoPeerFooter};
  case RtpsLinkState::CONNECTED:
    break;
  }
  return {"", ""}; // only asked when not CONNECTED
}

// Fills an ErrorBanner with why driving is not permitted right now. The
// titles are the caller's, because the same cause reads differently as a
// refused push and as a drive cut short; so is visibility.
static void fill_drive_blocked_panel(lv_obj_t *panel, const char *link_title,
                                     const char *mcb_title) {
  lv_obj_t *title = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERTITLE);
  lv_obj_t *body = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERMESSAGE);
  lv_obj_t *footer = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERFOOTER);

  // The link comes first, as in mcb_ready(): with no live link the state
  // subject is only the last thing the MCB said, not what it is saying now.
  const auto link = static_cast<RtpsLinkState>(lv_subject_get_int(&rtps_link_subject));
  if (link != RtpsLinkState::CONNECTED) {
    const RefusalText text = link_refusal_text(link);
    lv_label_set_text(title, link_title);
    lv_label_set_text(body, text.body);
    lv_label_set_text(footer, text.footer);
  } else {
    const auto state = static_cast<MIB::MibSystemState>(lv_subject_get_int(&mib_state_subject));
    const char *error_text = lv_subject_get_string(&error_text_subject);
    lv_label_set_text(title, mcb_title);
    if (error_text[0] != '\0') {
      lv_label_set_text(body, error_text);
    } else {
      lv_label_set_text_fmt(body, rammp::kHmiMcbNoTextFmt, static_cast<unsigned>(state),
                            rammp::to_string(state));
    }
    lv_label_set_text(footer, lv_subject_get_string(&error_footer_subject));
  }
}

// Fills a panel with a reason that is the MIB's own: its error_message when it sent
// one, and the given fallback when it did not. Used for the refusals that happen with
// the link up, where fill_drive_blocked_panel's "what is wrong with the link" half has
// nothing to say.
static void fill_mib_reason_panel(lv_obj_t *panel, const char *title, const char *fallback_body,
                                  const char *fallback_footer) {
  lv_obj_t *title_label = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERTITLE);
  lv_obj_t *body = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERMESSAGE);
  lv_obj_t *footer = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERFOOTER);
  const char *error_text = lv_subject_get_string(&error_text_subject);
  const char *error_footer = lv_subject_get_string(&error_footer_subject);

  lv_label_set_text(title_label, title);
  lv_label_set_text(body, error_text[0] != '\0' ? error_text : fallback_body);
  lv_label_set_text(footer, error_footer[0] != '\0' ? error_footer : fallback_footer);
}

// Binds `cb` on `panel` to every subject the cause depends on, so the subject
// argument each observer gets is ignored: whichever fired, the answer depends
// on all of them.
static void bind_to_drive_blocked_cause(lv_obj_t *panel, lv_observer_cb_t cb) {
  keep_overlay_fill(panel);
  for (lv_subject_t *subject :
       {&rtps_link_subject, &mib_state_subject, &error_text_subject, &error_footer_subject}) {
    lv_subject_add_observer_obj(subject, cb, panel, nullptr);
  }
}

// Shows or hides a banner, sounding the refusal when one comes up on the
// screen in front: a warning or an error arriving is heard as well as read.
// Only on the rise, because every cause subject re-runs the observers.
static void banner_show(lv_obj_t *panel, bool up) {
  const bool was_up = !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flag(panel, LV_OBJ_FLAG_HIDDEN, !up);
  if (up && !was_up && lv_obj_get_screen(panel) == lv_screen_active()) {
    play_refusal(true);
  }
}

// Locked, and the screens a menu refusal lands on. Says which request was
// refused, and hides itself the moment
// the MCB is ready again, so it never claims a fault that has already cleared.
static void entry_refused_panel_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  // Every show and hide goes through here. On the Locked screen the banner
  // takes the top of the body, which is where the padlock is, and half a
  // padlock under a fault reads as a drawing glitch -- so the padlock and its
  // ring step aside while the banner is up.
  auto shown = [panel](bool up) {
    banner_show(panel, up);
    if (panel == ui_ErrorBanner2) {
      for (lv_obj_t *art : {ui_LockRing, ui_Shackle, ui_LockBody, ui_LockKeyhole}) {
        lv_obj_set_flag(art, LV_OBJ_FLAG_HIDDEN, up);
      }
    }
  };
  const int32_t refused = lv_subject_get_int(&entry_refused_subject);
  if (refused == kRefusedNone || refused == kRefusedExit) {
    // kRefusedExit belongs to the DriveScreen's own panel, not this one.
    shown(false);
    return;
  }
  // The MIB's own refusals: shown for their window whatever the state says now,
  // because "driving stopped" is worth reading precisely when all is well again.
  if (refused == kRefusedDriveNotGranted || refused == kRefusedDriveStopped) {
    const bool stopped = refused == kRefusedDriveStopped;
    fill_mib_reason_panel(
        panel, stopped ? rammp::kHmiDriveStoppedTitle : rammp::kHmiDriveNotGrantedTitle,
        stopped ? rammp::kHmiDriveStoppedText : rammp::kHmiDriveNotGrantedText,
        stopped ? rammp::kHmiDriveStoppedFooter : rammp::kHmiDriveNotGrantedFooter);
    shown(true);
    return;
  }
  if (mcb_ready()) {
    shown(false);
    return;
  }
  if (refused == kRefusedSeat) {
    fill_drive_blocked_panel(panel, rammp::kHmiSeatLinkRefusedTitle,
                             rammp::kHmiSeatMcbRefusedTitle);
  } else if (refused == kRefusedDriveLost) {
    fill_drive_blocked_panel(panel, rammp::kHmiDriveLostLinkTitle, rammp::kHmiDriveLostMcbTitle);
  } else {
    fill_drive_blocked_panel(panel, rammp::kHmiLinkRefusedTitle, rammp::kHmiMcbRefusedTitle);
  }
  shown(true);
}

static void bind_entry_refused_panel(lv_obj_t *panel) {
  keep_overlay_fill(panel);
  if (panel == nullptr) {
    return;
  }
  lv_subject_add_observer_obj(&entry_refused_subject, entry_refused_panel_observer, panel, nullptr);
  bind_to_drive_blocked_cause(panel, entry_refused_panel_observer);
}

// DriveScreen and SeatScreen. No push to wait for here: entry already required a live
// link and an OK state, so anything else means it was lost mid-drive. The banner stays up for as
// long as that lasts rather than timing out, and clears by itself when the link and state recover.
// A state neither board knows counts as not OK (mcb_ready compares against OK), so it raises the
// banner rather than silently hiding it.
static void drive_screen_warning_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  // A refused exit: the MIB is still driving, so the screen stays and says why.
  if (lv_subject_get_int(&entry_refused_subject) == kRefusedExit &&
      lv_screen_active() == ui_DriveScreen) {
    fill_mib_reason_panel(panel, rammp::kHmiExitRefusedTitle, rammp::kHmiExitRefusedText,
                          rammp::kHmiExitRefusedFooter);
    banner_show(panel, true);
    return;
  }
  if (mcb_ready()) {
    banner_show(panel, false);
    return;
  }
  fill_drive_blocked_panel(panel, rammp::kHmiLinkLostTitle, rammp::kHmiMcbFaultTitle);
  banner_show(panel, true);
}

static void bind_mcb_lost_panel(lv_obj_t *panel) {
  keep_overlay_fill(panel);
  if (panel == nullptr) {
    return;
  }
  // Also on the refusal subject: a refused exit is raised with the link and state
  // unchanged, so nothing else would bring this panel back.
  lv_subject_add_observer_obj(&entry_refused_subject, drive_screen_warning_observer, panel,
                              nullptr);
  bind_to_drive_blocked_cause(panel, drive_screen_warning_observer);
}

static void entry_refused_clear() {
  lv_timer_pause(entry_refused_timer);
  lv_subject_set_int(&entry_refused_subject, 0);
}

static void entry_refused_timer_cb(lv_timer_t *) { entry_refused_clear(); }

// Defined below, with the drive gestures that raise most of these.
static void entry_refused_show(int32_t which, uint32_t show_ms);

// Runs from hold_poll_cb, on the same input and cadence as the gesture it
// shadows.
static void entry_refusal_poll() {
  // Holding the stick button on the Locked screen is the unlock gesture, so a
  // hold there with the MCB not ready is a refused attempt to drive. A hold, not
  // a press: under kBarGraceMs the button is a tap, which only selects. Once
  // per press, timed here rather than read off joy_button_armed, which the
  // gestures own and which cannot tell a fresh press from a held one.
  static int64_t pressed_at_us = 0;
  static bool refused_this_press = false;
  const int64_t now = esp_timer_get_time();
  if (!joy_button_held()) {
    pressed_at_us = 0;
    refused_this_press = false;
  } else if (pressed_at_us == 0) {
    pressed_at_us = now;
  }
  const bool pushed = pressed_at_us != 0 && !refused_this_press &&
                      now - pressed_at_us >= static_cast<int64_t>(kBarGraceMs) * 1000;

  const bool attempt = lv_screen_active() == ui_LockedScreen && nav_menu_open == nullptr &&
                       lv_subject_get_int(&locked_subject) != 0;
  const int32_t page = attempt ? kRefusedDrive : kRefusedNone;
  const int32_t refused = lv_subject_get_int(&entry_refused_subject);
  if (pushed && page != kRefusedNone && !mcb_ready()) {
    // Through the helper, so this always gets its own dwell back after a refusal
    // that asked for a shorter one.
    refused_this_press = true;
    entry_refused_show(page, kDriveRefusedShowMs);
    // Distinct from the STRONG_CLICK a completed hold gives, so a refusal can
    // be felt as well as read.
    refusal_feedback();
  } else if ((refused == kRefusedDrive && (refused != page || mcb_ready())) ||
             ((refused == kRefusedSeat || refused == kRefusedDriveLost ||
               refused == kRefusedDriveMenu) &&
              mcb_ready())) {
    // A refused drive is tied to the push on the Locked screen, so it goes
    // with it. A refused seat comes from the menu -- no push to hold it up --
    // so it stays its window (entry_refused_timer) unless the cause clears.
    // Clear rather than merely hide, so a cause that clears and then recurs
    // inside the window does not bring the panel back without a new push.
    entry_refused_clear();
  }
}

// The MIB decides whether the chair drives: the joystick asks with a DriveCommand and
// shows the DriveScreen only once MibStatus says ENABLED. Two windows, because saying
// "that was refused" and giving up on the request are different jobs:
//
//  - kDriveAnswerUs is when to speak. One MibStatus period plus a margin, so a sample
//    already in flight when the request went out cannot be mistaken for a refusal.
//    This is as early as a refusal can honestly be known, and waiting longer just
//    reads as the HMI ignoring the user.
//  - kDriveWaitUs is when to stop asking, so a silent MIB is not left with a live
//    request. The warning is long since up by then.
static constexpr int64_t kDriveAnswerUs =
    std::chrono::duration_cast<std::chrono::microseconds>(rammp::kMibStatusPeriod +
                                                          std::chrono::milliseconds{250})
        .count();
static constexpr int64_t kDriveWaitUs =
    std::chrono::duration_cast<std::chrono::microseconds>(rammp::kMibStatusTimeout).count();
