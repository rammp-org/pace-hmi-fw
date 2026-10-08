// split_main.py fragment of main.cpp (frag_clock: original lines 767-852); one TU, see main.cpp.
#include "hmi_format/topbar.hpp"
#include "hmi_ui/topbar_view.hpp"

/////////////////////////////////////////////////////////////////////////////
// TopBar clock
//
// The MIB sends Unix time plus its UTC offset in every MibStatus. It sets the
// system clock and the RTC, so the time keeps running through a lost link, and
// across a reboot without the MIB. Clock1 on every TopBar shows the system
// clock. No TZ is set, so the system clock simply holds the local wall time the
// MIB reported and nothing converts again.
/////////////////////////////////////////////////////////////////////////////

// The system clock's validity: set by hmi::housekeeping::SystemClock (app_main's
// system_clock), read by the TopBar. The time sync itself is the housekeeping
// component's.
static std::atomic<bool> clock_valid{false};

// An RTC that lost power reads 2000; anything before 2025 is not a real time
// (components/hmi_format, REQ-FMT-06).
using hmi::format::clock_plausible;

// The TopBar's link label: what RTPS runs over. The design's "BT · WI-FI" is only
// a placeholder; it is set from the setting before rtps_comms_start, then from the
// link it brought up (WiFi with no network known runs Ethernet).
// NetLink -> hmi_format's Link (REQ-FMT-07): anything but WiFi reads Ethernet.
static const char *link_text(NetLink link) {
  return hmi::format::link_text(link == NetLink::WIFI ? hmi::format::Link::WIFI
                                                      : hmi::format::Link::ETHERNET);
}

// The one instance, for every TopBar; bound from app_main and the screens built on demand.
static constinit hmi::ui::TopBarView topbar_view{{.clock_valid = &clock_valid}};
static void bind_topbar_labels(lv_obj_t *bar) { topbar_view.bind(bar); }

// The hmi_ui chrome views of one screen, in the order app_main has always bound them: the
// DriveBand's status cells, then the TopBar's RTPS label, then its clock and link. The screens
// built on demand bind the same three in their *_ensure().
static void bind_chrome_views(lv_obj_t *band, lv_obj_t *bar) {
  bind_status_panel(band);
  bind_rtps_label(bar);
  bind_topbar_labels(bar);
}

// One edge of the stick button, from the GPIO or from the remote UI channel --
// one path, so what a script presses is the real handling. Touches subjects,
// never widgets directly. Any task.
//
// "Select" -- the keypad's ENTER, which clicks whatever the joystick has
// focused -- fires on RELEASE, and only for a press shorter than
// hmi::stick::SELECT_MAX_US. The same button is held to stop driving and to start a
// calibration, and a select on the press edge would have clicked the focused
// control (on the Drive screen: opened the menu) at the start of every hold.
// SELECT_MAX_US is a hold's grace period, so a press short enough to select
// never starts a hold filling, and one long enough to fill never selects.
