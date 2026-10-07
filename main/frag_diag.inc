// split_main.py fragment of main.cpp (frag_diag: original lines 2980-3229); one TU, see main.cpp.
#include "hmi_format/diag.hpp"
#include "hmi_ui/diagnostics_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// DiagnosticsScreen: live readings from the MCB
//
// Opened from the DIAGNOSTICS settings row, left by pulling and holding. One
// row per entry in RAMMP_DIAG_TABLE (messages/joystick_message.hpp): short label, label,
// and up to three readings, each under its unit. The MCB publishes them all on
// rammp::kMcbDiagnostics every rammp::kDiagPeriod; they land in
// diag_value (subjects, set under the LVGL lock by the RTPS handler in
// app_main) and the rows observe them.
//
// DiagnosticsFreqLabel shows how fast they are arriving ("2.0 Hz - Live").
// Once nothing has arrived for rammp::kDiagTimeout - or nothing ever has -
// every row's text and the label turn red and blink: readings the MCB stopped
// sending must not sit there looking current.
//
// The red comes through LV_STATE_USER_1. The labels' text colours are themed
// for DEFAULT and FOCUSED, and a style on a higher state outranks both without
// touching anything the theme manager re-applies on a theme change.
//
// Built on demand like the settings and actions screens (see "Screens built
// on demand").
/////////////////////////////////////////////////////////////////////////////

static_assert(rammp::kDiagFields == 3, "the DiagnosticComponent has exactly three readings");

// Readings, raw, [table row][reading]; kValueUnknown until the MCB sends one.
// Static: the rows' observers point at them, and they keep updating with no
// screen up.
static lv_subject_t diag_value[rammp::kDiagCount][rammp::kDiagFields];
static lv_subject_t diag_stale_subject; // 1 = nothing within rammp::kDiagTimeout, or ever
static lv_subject_t diag_rate_subject;  // arrival rate, tenths of a Hz
static lv_group_t *diag_group = nullptr;

// Keeps diag_stale_subject and diag_rate_subject current. LVGL task, from
// rtps_poll_cb: staleness has to be polled - nothing happens when a sample
// fails to arrive.
static void diag_poll() {
  const RtpsDiagStats stats = rtps_comms_diag_stats();
  const bool stale =
      stats.last_us == 0 ||
      esp_timer_get_time() - stats.last_us >
          std::chrono::duration_cast<std::chrono::microseconds>(rammp::kDiagTimeout).count();
  lv_subject_set_int(&diag_stale_subject, stale ? 1 : 0);
  lv_subject_set_int(&diag_rate_subject, stale ? 0 : stats.rate_tenths_hz);
}

// The one instance; the readings, the stale and rate subjects and the group stay out here
// (rtps_poll_cb, the RTPS handler, the on-demand screen and nav use them).
static constinit hmi::ui::DiagnosticsView diag_view{{
    .nav = &ui_nav_port,
    .values = diag_value,
    .stale = &diag_stale_subject,
    .rate = &diag_rate_subject,
    .blink = &rtps_blink_subject,
    .screen_ensure = diagnostics_screen_ensure,
    .row_focus_cb = setting_focus_cb,
}};
static void diag_freq_observer(lv_observer_t *observer, lv_subject_t *) {
  diag_view.paint_freq(lv_observer_get_target_obj(observer));
}
static void diag_focus(int index) { diag_view.focus(index); }
static void diag_rows_clear() { diag_view.rows_clear(); }
static void diagnostics_open() { diag_view.open(); }

/////////////////////////////////////////////////////////////////////////////
// The burger menu, and moving around with the joystick
//
// Spec V2 gives every screen but BootScreen the same chrome: a TopBar across
// the top, a DriveBand under it, a MenuKey in the bottom 162 px, and a
// MenuOverlay that exactly covers the 720x921 body between them and starts
// hidden. Tapping the key slides the overlay up over the body; the TopBar, the
// band and the key itself do not move, which is what the spec means by
// "sticky".
//
// The joystick reaches all of it. Every screen's focus group ends with that
// screen's burger key: push down past the last row (or key, or log line) and
// the key is focused; push up from it and you are back where you were. The
// stick button presses whatever is focused -- a row, a button, the key. In the
// open menu, up and down walk the rows, and left (or the key) closes it.
//
// The Drive screen is the exception, because there the stick drives: nothing
// on it takes focus but its key, which has no focus ring, so a short press of
// the stick button opens the menu and a hold still stops the chair
// (drive_exit_gesture). The button selects on RELEASE, and only for a press
// shorter than a hold's grace, so a hold never also clicks.
//
// What the cursor looks like: rows (menu, settings, diagnostics) go negative,
// the spec's own "selected"; buttons get a ring in the theme's focus colour,
// because on a button the negative already means pressed -- a drive mode
// filled is the one selected, and the key filled white is "menu open".
//
// Every screen carries its own instance of all four pieces of chrome, so
// nav_attach_chrome is called once per screen -- from app_main for the screens
// ui_init builds, and from the *_screen_ensure() functions for the ones built
// on demand.
//
// This lives in main.cpp rather than a ui_nav.cpp: every destination is a
// file-static here (the *_open functions, the focus groups, the PIN gate), and
// a header exporting all of them to one caller would be more coupling than the
// split removes.
/////////////////////////////////////////////////////////////////////////////

// The overlay sits over the body; sliding it clear of the screen bottom is the
// closed position. 921 px in 280 ms is the spec's transition.
static constexpr int32_t kMenuShownY = 195;
static constexpr int32_t kMenuHiddenY = 195 + 921;
static constexpr uint32_t kMenuSlideMs = 280;
// p21 "ROW PRESS": the row holds negative this long before anything moves.
static constexpr uint32_t kRowPressMs = 300;
