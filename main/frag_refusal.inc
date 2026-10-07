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

// The banners' words, from the shared spec.
static constexpr hmi::ui::RefusalTexts kRefusalTexts{
    .eth_failed = {rammp::kHmiEthFailedText, rammp::kHmiEthFailedFooter},
    .wifi_failed = {rammp::kHmiWifiFailedText, rammp::kHmiWifiFailedFooter},
    .link_down = {rammp::kHmiLinkDownText, rammp::kHmiLinkDownFooter},
    .wifi_down = {rammp::kHmiWifiDownText, rammp::kHmiWifiDownFooter},
    .no_ip = {rammp::kHmiNoIpText, rammp::kHmiNoIpFooter},
    .no_peer = {rammp::kHmiNoPeerText, rammp::kHmiNoPeerFooter},
    .mcb_no_text_fmt = rammp::kHmiMcbNoTextFmt,
    .state_name = [](MIB::MibSystemState state) { return rammp::to_string(state); },
    .drive_stopped = {rammp::kHmiDriveStoppedTitle, rammp::kHmiDriveStoppedText,
                      rammp::kHmiDriveStoppedFooter},
    .drive_not_granted = {rammp::kHmiDriveNotGrantedTitle, rammp::kHmiDriveNotGrantedText,
                          rammp::kHmiDriveNotGrantedFooter},
    .exit_refused = {rammp::kHmiExitRefusedTitle, rammp::kHmiExitRefusedText,
                     rammp::kHmiExitRefusedFooter},
    .link_refused_title = rammp::kHmiLinkRefusedTitle,
    .mcb_refused_title = rammp::kHmiMcbRefusedTitle,
    .seat_link_refused_title = rammp::kHmiSeatLinkRefusedTitle,
    .seat_mcb_refused_title = rammp::kHmiSeatMcbRefusedTitle,
    .drive_lost_link_title = rammp::kHmiDriveLostLinkTitle,
    .drive_lost_mcb_title = rammp::kHmiDriveLostMcbTitle,
    .link_lost_title = rammp::kHmiLinkLostTitle,
    .mcb_fault_title = rammp::kHmiMcbFaultTitle,
};

// The one instance, for every refusal banner.
static constinit hmi::ui::RefusalView refusal_view{{
    .shared = &ui_shared_subjects,
    .texts = &kRefusalTexts,
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
