// split_main.py fragment of main.cpp (frag_state: original lines 108-263); one TU, see main.cpp.
// What is left here, and why it stays in main for now: mib_state, rtps_link and locked, which
// several views read (SharedSubjects) and the drive port reads by name, pinned by its frozen
// goldens (tests/host/drive_golden), with the tables over them. The rest of the shared state
// is components/hmi_ui's app_state (S7 moves each piece on to its owner).
#include "hmi_ui/app_state.hpp"
// The MIB's state, as reported over RTPS. The joystick is a slave here: this holds
// whatever the MIB last said, and every DriveBand on every screen follows it. One
// subject, because MibSystemState answers both of a panel's labels - where the message
// it replaced needed a drive status and a fault. Values are the MIB::MibSystemState
// enum from messages/mib_message.hpp.
static lv_subject_t mib_state_subject;

// Link health behind those two, polled from rtps_comms (RtpsLinkState). Drives
// the TopBar's RTPS indicator, and greys the status labels when it is not
// CONNECTED - a value we can no longer vouch for must not keep showing.
static lv_subject_t rtps_link_subject;

// Lock state. Declared up here with the other subjects because the unlock
// gesture and the nav layer both read it; the rest of the lock/unlock machinery
// lives in its own section further down, after the haptic and audio helpers it
// needs.
//
// 1 = locked, 0 = unlocked. The single source of truth for both padlock images
// and both labels, so a future "lock on request" is one set_locked(true) call
// and every bound widget follows.
static lv_subject_t locked_subject;
// An indev can own exactly one group, so the joystick's group follows the
// active screen. The swap happens on LV_EVENT_SCREEN_LOADED, so it covers
// every route between the screens.
// "This object covers what is behind it" (components/hmi_ui overdraw.hpp).
#include "hmi_ui/overdraw.hpp"
static constexpr lv_obj_flag_t kOverlayFlag = hmi::ui::OVERLAY_FLAG;
using hmi::ui::keep_overlay_fill;

// Defined with the burger menu (see "The burger menu, and moving around with
// the joystick"); declared here for the screens and gestures above it.
static void nav_use_group(lv_group_t *g, const lv_obj_t *screen);
static void nav_arrive(const lv_obj_t *screen);
static void nav_mirror_states(lv_obj_t *obj);
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
