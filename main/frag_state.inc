// split_main.py fragment of main.cpp (frag_state: original lines 108-263); one TU, see main.cpp.
// What is left here after S7 (app-main-shrink.md), and why it stays in main for now:
//  - mib_state, rtps_link, rtps_blink, locked: read by several views (SharedSubjects) and,
//    with nav_menu_open and nav_menu_on_arrival, by name by the drive port and its frozen
//    goldens (tests/host/drive_golden);
//  - the atomics: what the ADC task and the drive port read without the LVGL lock (channels
//    are T-H4a);
//  - the Settings subjects and their atomics: the settings model's, which the modules lane
//    moves with main/settings.cpp.
// The views' own subjects are their members (CS-UI-05): JoystickView, DriveBandView,
// StatusBandView, RefusalView; the groups are NavView's and BenchPinView's.
// The Joystick test screen's readouts (axis bars, button count) own their subjects.
// Their declarations, for the code that leaves this unit (components/hmi_ui app_state.hpp).
#include "hmi_ui/app_state.hpp"
constinit hmi::ui::JoystickView joystick_view;
// The MIB's state, as reported over RTPS. The joystick is a slave here: this holds
// whatever the MIB last said, and every DriveBand on every screen follows it. One
// subject, because MibSystemState answers both of a panel's labels - where the message
// it replaced needed a drive status and a fault. Values are the MIB::MibSystemState
// enum from messages/mib_message.hpp.
static lv_subject_t mib_state_subject;

// Joystick button, mirrored out of the LVGL world so the ADC task can publish
// it alongside the stick position without taking the LVGL lock.
std::atomic<bool> joy_button_pressed{false};

// Which drive mode the user picked on the DriveScreen (DriveBandView's profile
// subject shows what the MIB took). The HMI reports it to the MCB with every
// DriveCommand; an atomic for the same reason as the button: the ADC task must
// not take the LVGL lock to read it.
std::atomic<MIB::DriveProfile> drive_profile_published{MIB::DriveProfile::NORMAL};

// Link health behind those two, polled from rtps_comms (RtpsLinkState). Drives
// the TopBar's RTPS indicator, and greys the status labels when it is not
// CONNECTED - a value we can no longer vouch for must not keep showing.
static lv_subject_t rtps_link_subject;
// 0/1 blink phase for the indicator, flipped by the same poll timer
lv_subject_t rtps_blink_subject;


// Lock state. Declared up here with the other subjects because the unlock
// gesture and the nav layer both read it; the rest of the lock/unlock machinery
// lives in its own section further down, after the haptic and audio helpers it
// needs.
//
// 1 = locked, 0 = unlocked. The single source of truth for both padlock images
// and both labels, so a future "lock on request" is one set_locked(true) call
// and every bound widget follows.
static lv_subject_t locked_subject;
// Joystick -> LVGL keypad. Held at the *level* the stick is at: whichever
// LV_KEY_* direction it is deflected toward, or 0 when centered. Reporting a
// held key rather than a one-shot latch is what lets LVGL's own key repeat
// ([lv_indev.c] re-sends the key every long_press_repeat_time) walk the
// settings list while the stick stays pushed.
std::atomic<uint32_t> joy_key{0};

// GPIO48 "select" stays edge-latched, unlike the directions: repeating ENTER
// would re-click the focused row every repeat period, which for the theme-toggle
// row means it strobes. Consumed by the indev read.
std::atomic<bool> select_key{false};
// A direction held by the remote UI channel (remote_ui.cpp); 0 = none. The ADC
// task recomputes joy_key from the real stick every cycle, so a direction
// written straight into joy_key lasted at most 33 ms and a keypad read could
// miss it. This one outlasts that until the channel lets go.
std::atomic<uint32_t> remote_key{0};
// The last direction the stick engaged, kept until the keypad has read it. A
// quick flick can engage and release inside one ADC period plus one keypad read,
// and the read would then only ever see joy_key at rest -- a flick the stick
// felt and the UI never did.
std::atomic<uint32_t> joy_flick{0};

// An indev can own exactly one group, so the joystick's group follows the
// active screen. The swap happens on LV_EVENT_SCREEN_LOADED, so it covers
// every route between the screens.
// "This object covers what is behind it" (components/hmi_ui overdraw.hpp).
#include "hmi_ui/overdraw.hpp"
static constexpr lv_obj_flag_t kOverlayFlag = hmi::ui::OVERLAY_FLAG;
using hmi::ui::keep_overlay_fill;


// The Settings rows' subjects, one per SETTINGS_PARAM_* (hmi_ui). Up here because
// the backlight, rtps_poll_cb (the theme row), the menu (the slide) and the
// Internet screen (the network) read them. Each owner's observer applies and
// saves its value.
constinit hmi::ui::SettingSubjects setting_subjects;

// What the ADC task and the click sound need of those, as atomics they can read
// without the LVGL lock (components/settings). Written by setting_store_observer.
constinit hmi::settings::AppliedSettings applied_settings;

// The burger menu's overlay while it is up, else null. Up here because the
// hold gestures ask whether it is open before they apply.
lv_obj_t *nav_menu_open = nullptr;

// Whether the stick may drive the chair right now: the Drive screen is up, the
// menu is shut and drive is active. Written on the LVGL task by
// nav_update_stick_gate, read by the ADC task, which sends the MCB a centred
// stick whenever it is false.
std::atomic<bool> stick_drives{false};

// Defined with the burger menu (see "The burger menu, and moving around with
// the joystick"); declared here for the screens and gestures above it.
static void nav_use_group(lv_group_t *g, const lv_obj_t *screen);
static void nav_arrive(const lv_obj_t *screen);
static void nav_mirror_states(lv_obj_t *obj);
// Open the menu on the next screen to load: the burger key on Drive asks the
// MIB to stop first, and the menu follows once it has, over the Locked screen.
bool nav_menu_on_arrival = false;
static void nav_to_key();
static void nav_home();
static void nav_focus_ring(lv_obj_t *obj);
static void nav_update_stick_gate();

// The subjects above that the hmi_ui views read (components/hmi_ui/shared_subjects.hpp).
#include "hmi_ui/link_state.hpp"
#include "hmi_ui/nav_port.hpp"
#include "hmi_ui/nav_view.hpp"
#include "hmi_ui/shared_subjects.hpp"
static constexpr hmi::ui::SharedSubjects ui_shared_subjects{
    .locked = &locked_subject,
    .mib_state = &mib_state_subject,
    .rtps_link = &rtps_link_subject,
    .rtps_blink = &rtps_blink_subject,
};
// rtps_link_subject carries RtpsLinkState; the views read it as hmi::ui::LinkState.
static constexpr bool same_link_state(RtpsLinkState a, hmi::ui::LinkState b) {
  return static_cast<int32_t>(a) == static_cast<int32_t>(b);
}
static_assert(same_link_state(RtpsLinkState::NET_FAILED, hmi::ui::LinkState::NET_FAILED));
static_assert(same_link_state(RtpsLinkState::LINK_DOWN, hmi::ui::LinkState::LINK_DOWN));
static_assert(same_link_state(RtpsLinkState::NO_IP, hmi::ui::LinkState::NO_IP));
static_assert(same_link_state(RtpsLinkState::NO_PEER, hmi::ui::LinkState::NO_PEER));
static_assert(same_link_state(RtpsLinkState::CONNECTED, hmi::ui::LinkState::CONNECTED));
// Navigation for the hmi_ui views (components/hmi_ui/nav_port.hpp): NavView's API. The look
// is NavView's own (static); the two calls that need the one instance go through it.
static constexpr hmi::ui::NavPort ui_nav_port{
    .to_key = nav_to_key,
    .use_group = nav_use_group,
    .focus_ring = hmi::ui::NavView::focus_ring,
    .mirror_states = hmi::ui::NavView::mirror_states,
};
