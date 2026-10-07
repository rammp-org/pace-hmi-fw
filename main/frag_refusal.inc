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
// Saying why driving is not permitted (hmi::ui::RefusalView)
//
// One cause, several ErrorBanners. On the Locked screen, and on the screens a
// menu refusal can happen over: why a request to drive, or to open Seat
// Functions, was refused - the unlock hold is gated in applies(), so a refused
// one otherwise does nothing at all. On the DriveScreen and the SeatScreen: why
// it was cut short, by the link dropping or the MCB faulting. All word the
// cause from hmi_rtps_spec.hpp.
/////////////////////////////////////////////////////////////////////////////
#include "hmi_ui/refusal_view.hpp"

static constexpr uint32_t kDriveRefusedShowMs = 3000; // how long one refusal stays up
static constexpr uint32_t kExitRefusedShowMs = 2000;  // a refused exit gets its own dwell
// Which push was refused, so the panel can say which (hmi::ui::Refused).
enum : int32_t {
  kRefusedNone = hmi::ui::REFUSED_NONE,
  kRefusedDrive = hmi::ui::REFUSED_DRIVE,
  kRefusedSeat = hmi::ui::REFUSED_SEAT,
  kRefusedDriveNotGranted = hmi::ui::REFUSED_DRIVE_NOT_GRANTED,
  kRefusedDriveStopped = hmi::ui::REFUSED_DRIVE_STOPPED,
  kRefusedExit = hmi::ui::REFUSED_EXIT,
  kRefusedDriveLost = hmi::ui::REFUSED_DRIVE_LOST,
  kRefusedDriveMenu = hmi::ui::REFUSED_DRIVE_MENU,
};
static lv_subject_t entry_refused_subject; // kRefused*; panel up unless None

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

// The drive session's input, defined in frag_drive.inc.
static bool drive_session_input(hmi::drive_session::Input input);

// The one instance, for every refusal banner.
static constinit hmi::ui::RefusalView refusal_view{{
    .shared = &ui_shared_subjects,
    .error_text = &error_text_subject,
    .error_footer = &error_footer_subject,
    .refused = &entry_refused_subject,
    .wifi = [] { return rtps_comms_net_link() == NetLink::WIFI; },
    .mcb_ready = mcb_ready,
    .play_refusal = [](bool warning) { play_refusal(warning); },
    .keep_overlay_fill = keep_overlay_fill,
    .button_held = joy_button_held,
    .now_us = [] { return esp_timer_get_time(); },
    .menu_open = [] { return nav_menu_open != nullptr; },
    // A push is decided by the drive session (rows 38-39: locked, on the Locked
    // screen, no menu, MCB not ready).
    .entry_push = [] { return drive_session_input(hmi::drive_session::Input::ENTRY_PUSH); },
    .grace_ms = kBarGraceMs,
}};

hmi::ui::RefusalView::Text hmi::ui::RefusalView::link_text(LinkState link) const {
  const bool wifi = config_.wifi();
  switch (link) {
  case LinkState::NET_FAILED:
    return wifi ? Text{rammp::kHmiWifiFailedText, rammp::kHmiWifiFailedFooter}
                : Text{rammp::kHmiEthFailedText, rammp::kHmiEthFailedFooter};
  case LinkState::LINK_DOWN:
    return wifi ? Text{rammp::kHmiWifiDownText, rammp::kHmiWifiDownFooter}
                : Text{rammp::kHmiLinkDownText, rammp::kHmiLinkDownFooter};
  case LinkState::NO_IP:
    return {rammp::kHmiNoIpText, rammp::kHmiNoIpFooter};
  case LinkState::NO_PEER:
    return {rammp::kHmiNoPeerText, rammp::kHmiNoPeerFooter};
  case LinkState::CONNECTED:
    break;
  }
  return {"", ""}; // only asked when not CONNECTED
}

// Fills an ErrorBanner with why driving is not permitted right now. The
// titles are the caller's, because the same cause reads differently as a
// refused push and as a drive cut short; so is visibility.
void hmi::ui::RefusalView::fill_drive_blocked(lv_obj_t *panel, const char *link_title,
                                              const char *mcb_title) const {
  lv_obj_t *title = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERTITLE);
  lv_obj_t *body = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERMESSAGE);
  lv_obj_t *footer = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERFOOTER);

  // The link comes first, as in mcb_ready(): with no live link the state
  // subject is only the last thing the MCB said, not what it is saying now.
  const auto link = static_cast<LinkState>(lv_subject_get_int(config_.shared->rtps_link));
  if (link != LinkState::CONNECTED) {
    const Text text = link_text(link);
    lv_label_set_text(title, link_title);
    lv_label_set_text(body, text.body);
    lv_label_set_text(footer, text.footer);
  } else {
    const auto state =
        static_cast<MIB::MibSystemState>(lv_subject_get_int(config_.shared->mib_state));
    const char *error_text = lv_subject_get_string(config_.error_text);
    lv_label_set_text(title, mcb_title);
    if (error_text[0] != '\0') {
      lv_label_set_text(body, error_text);
    } else {
      lv_label_set_text_fmt(body, rammp::kHmiMcbNoTextFmt, static_cast<unsigned>(state),
                            rammp::to_string(state));
    }
    lv_label_set_text(footer, lv_subject_get_string(config_.error_footer));
  }
}

// Fills a panel with a reason that is the MIB's own: its error_message when it sent
// one, and the given fallback when it did not. Used for the refusals that happen with
// the link up, where fill_drive_blocked's "what is wrong with the link" half has
// nothing to say.
void hmi::ui::RefusalView::fill_mib_reason(lv_obj_t *panel, const char *title,
                                           const char *fallback_body,
                                           const char *fallback_footer) const {
  lv_obj_t *title_label = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERTITLE);
  lv_obj_t *body = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERMESSAGE);
  lv_obj_t *footer = ui_comp_get_child(panel, UI_COMP_ERRORBANNER_BANNERBOX_BANNERFOOTER);
  const char *error_text = lv_subject_get_string(config_.error_text);
  const char *error_footer = lv_subject_get_string(config_.error_footer);

  lv_label_set_text(title_label, title);
  lv_label_set_text(body, error_text[0] != '\0' ? error_text : fallback_body);
  lv_label_set_text(footer, error_footer[0] != '\0' ? error_footer : fallback_footer);
}

// Binds `cb` on `panel` to every subject the cause depends on, so the subject
// argument each observer gets is ignored: whichever fired, the answer depends
// on all of them.
void hmi::ui::RefusalView::bind_to_cause(lv_obj_t *panel, lv_observer_cb_t cb, void *user_data) {
  config_.keep_overlay_fill(panel);
  for (lv_subject_t *subject : {config_.shared->rtps_link, config_.shared->mib_state,
                                config_.error_text, config_.error_footer}) {
    lv_subject_add_observer_obj(subject, cb, panel, user_data);
  }
}

// Shows or hides a banner, sounding the refusal when one comes up on the
// screen in front: a warning or an error arriving is heard as well as read.
// Only on the rise, because every cause subject re-runs the observers.
void hmi::ui::RefusalView::show(lv_obj_t *panel, bool up) const {
  const bool was_up = !lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flag(panel, LV_OBJ_FLAG_HIDDEN, !up);
  if (up && !was_up && lv_obj_get_screen(panel) == lv_screen_active()) {
    config_.play_refusal(true);
  }
}

// Locked, and the screens a menu refusal lands on. Says which request was
// refused, and hides itself the moment
// the MCB is ready again, so it never claims a fault that has already cleared.
void hmi::ui::RefusalView::refused_panel_observer(lv_observer_t *observer, lv_subject_t *) {
  const auto *view = static_cast<const RefusalView *>(lv_observer_get_user_data(observer));
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  // Every show and hide goes through here. On the Locked screen the banner
  // takes the top of the body, which is where the padlock is, and half a
  // padlock under a fault reads as a drawing glitch -- so the padlock and its
  // ring step aside while the banner is up.
  auto shown = [view, panel](bool up) {
    view->show(panel, up);
    if (panel == ui_ErrorBanner2) {
      for (lv_obj_t *art : {ui_LockRing, ui_Shackle, ui_LockBody, ui_LockKeyhole}) {
        lv_obj_set_flag(art, LV_OBJ_FLAG_HIDDEN, up);
      }
    }
  };
  const int32_t refused = lv_subject_get_int(view->config_.refused);
  if (refused == REFUSED_NONE || refused == REFUSED_EXIT) {
    // REFUSED_EXIT belongs to the DriveScreen's own panel, not this one.
    shown(false);
    return;
  }
  // The MIB's own refusals: shown for their window whatever the state says now,
  // because "driving stopped" is worth reading precisely when all is well again.
  if (refused == REFUSED_DRIVE_NOT_GRANTED || refused == REFUSED_DRIVE_STOPPED) {
    const bool stopped = refused == REFUSED_DRIVE_STOPPED;
    view->fill_mib_reason(
        panel, stopped ? rammp::kHmiDriveStoppedTitle : rammp::kHmiDriveNotGrantedTitle,
        stopped ? rammp::kHmiDriveStoppedText : rammp::kHmiDriveNotGrantedText,
        stopped ? rammp::kHmiDriveStoppedFooter : rammp::kHmiDriveNotGrantedFooter);
    shown(true);
    return;
  }
  if (view->config_.mcb_ready()) {
    shown(false);
    return;
  }
  if (refused == REFUSED_SEAT) {
    view->fill_drive_blocked(panel, rammp::kHmiSeatLinkRefusedTitle,
                             rammp::kHmiSeatMcbRefusedTitle);
  } else if (refused == REFUSED_DRIVE_LOST) {
    view->fill_drive_blocked(panel, rammp::kHmiDriveLostLinkTitle, rammp::kHmiDriveLostMcbTitle);
  } else {
    view->fill_drive_blocked(panel, rammp::kHmiLinkRefusedTitle, rammp::kHmiMcbRefusedTitle);
  }
  shown(true);
}

void hmi::ui::RefusalView::bind_refused_panel(lv_obj_t *panel) {
  config_.keep_overlay_fill(panel);
  if (panel == nullptr) {
    return;
  }
  lv_subject_add_observer_obj(config_.refused, refused_panel_observer, panel, this);
  bind_to_cause(panel, refused_panel_observer, this);
}

// DriveScreen and SeatScreen. No push to wait for here: entry already required a live
// link and an OK state, so anything else means it was lost mid-drive. The banner stays up for as
// long as that lasts rather than timing out, and clears by itself when the link and state recover.
// A state neither board knows counts as not OK (mcb_ready compares against OK), so it raises the
// banner rather than silently hiding it.
void hmi::ui::RefusalView::lost_panel_observer(lv_observer_t *observer, lv_subject_t *) {
  const auto *view = static_cast<const RefusalView *>(lv_observer_get_user_data(observer));
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  // A refused exit: the MIB is still driving, so the screen stays and says why.
  if (lv_subject_get_int(view->config_.refused) == REFUSED_EXIT &&
      lv_screen_active() == ui_DriveScreen) {
    view->fill_mib_reason(panel, rammp::kHmiExitRefusedTitle, rammp::kHmiExitRefusedText,
                          rammp::kHmiExitRefusedFooter);
    view->show(panel, true);
    return;
  }
  if (view->config_.mcb_ready()) {
    view->show(panel, false);
    return;
  }
  view->fill_drive_blocked(panel, rammp::kHmiLinkLostTitle, rammp::kHmiMcbFaultTitle);
  view->show(panel, true);
}

void hmi::ui::RefusalView::bind_lost_panel(lv_obj_t *panel) {
  config_.keep_overlay_fill(panel);
  if (panel == nullptr) {
    return;
  }
  // Also on the refusal subject: a refused exit is raised with the link and state
  // unchanged, so nothing else would bring this panel back.
  lv_subject_add_observer_obj(config_.refused, lost_panel_observer, panel, this);
  bind_to_cause(panel, lost_panel_observer, this);
}

void hmi::ui::RefusalView::start_timer(uint32_t period_ms) {
  timer_ = lv_timer_create(timer_cb, period_ms, this);
  lv_timer_pause(timer_);
}

void hmi::ui::RefusalView::timer_cb(lv_timer_t *timer) {
  static_cast<const RefusalView *>(lv_timer_get_user_data(timer))->clear();
}

void hmi::ui::RefusalView::clear() const {
  lv_timer_pause(timer_);
  lv_subject_set_int(config_.refused, 0);
}

// Runs from hold_poll_cb, on the same input and cadence as the gesture it
// shadows.
void hmi::ui::RefusalView::poll() {
  // Holding the stick button on the Locked screen is the unlock gesture, so a
  // hold there with the MCB not ready is a refused attempt to drive. A hold, not
  // a press: under grace_ms the button is a tap, which only selects. Once
  // per press, timed here rather than read off joy_button_armed, which the
  // gestures own and which cannot tell a fresh press from a held one.
  const int64_t now = config_.now_us();
  if (!config_.button_held()) {
    pressed_at_us_ = 0;
    refused_this_press_ = false;
  } else if (pressed_at_us_ == 0) {
    pressed_at_us_ = now;
  }
  const bool pushed = pressed_at_us_ != 0 && !refused_this_press_ &&
                      now - pressed_at_us_ >= static_cast<int64_t>(config_.grace_ms) * 1000;

  const bool attempt = lv_screen_active() == ui_LockedScreen && !config_.menu_open() &&
                       lv_subject_get_int(config_.shared->locked) != 0;
  const int32_t page = attempt ? REFUSED_DRIVE : REFUSED_NONE;
  const int32_t refused = lv_subject_get_int(config_.refused);
  // A push is decided by the drive session: the refusal goes through entry_refused_show,
  // so it always gets its own dwell back after a refusal that asked for a shorter
  // one, with refusal_feedback -- distinct from the STRONG_CLICK a completed hold
  // gives, so a refusal can be felt as well as read.
  if (pushed && config_.entry_push()) {
    refused_this_press_ = true;
  } else if ((refused == REFUSED_DRIVE && (refused != page || config_.mcb_ready())) ||
             ((refused == REFUSED_SEAT || refused == REFUSED_DRIVE_LOST ||
               refused == REFUSED_DRIVE_MENU) &&
              config_.mcb_ready())) {
    // A refused drive is tied to the push on the Locked screen, so it goes
    // with it. A refused seat comes from the menu -- no push to hold it up --
    // so it stays its window (the dwell timer) unless the cause clears.
    // Clear rather than merely hide, so a cause that clears and then recurs
    // inside the window does not bring the panel back without a new push.
    clear();
  }
}

// What the rest of the unit calls (frag_settings_ui, frag_hold_poll, app_main).
static void banner_show(lv_obj_t *panel, bool up) { refusal_view.show(panel, up); }
static void fill_drive_blocked_panel(lv_obj_t *panel, const char *link_title,
                                     const char *mcb_title) {
  refusal_view.fill_drive_blocked(panel, link_title, mcb_title);
}
static void bind_entry_refused_panel(lv_obj_t *panel) { refusal_view.bind_refused_panel(panel); }
static void bind_mcb_lost_panel(lv_obj_t *panel) { refusal_view.bind_lost_panel(panel); }
static void bind_to_drive_blocked_cause(lv_obj_t *panel, lv_observer_cb_t cb) {
  refusal_view.bind_to_cause(panel, cb, nullptr);
}
static void entry_refusal_poll() { refusal_view.poll(); }

// The MIB decides whether the chair drives: the joystick asks with a DriveCommand and
// shows the DriveScreen only once MibStatus says ENABLED. Two windows, because saying
// "that was refused" and giving up on the request are different jobs:
//
//  - kDriveAnswer is when to speak. One MibStatus period plus a margin, so a sample
//    already in flight when the request went out cannot be mistaken for a refusal.
//    This is as early as a refusal can honestly be known, and waiting longer just
//    reads as the HMI ignoring the user.
//  - kDriveWait is when to stop asking, so a silent MIB is not left with a live
//    request. The warning is long since up by then.
// Both are the drive table's (drive_session_table.hpp), the drive adapter's default
// windows; here they are held to the RTPS spec they come from.
static_assert(hmi::drive_session::kDriveAnswer ==
              rammp::kMibStatusPeriod + std::chrono::milliseconds{250});
static_assert(hmi::drive_session::kDriveWait == rammp::kMibStatusTimeout);
