// split_main.py fragment of main.cpp (frag_rtps_poll: original lines 639-717); one TU, see main.cpp.
// Runs on the LVGL task, so the subject writes are already covered by the lock
// lv_task_handler() is called under. Staleness has to be polled - nothing
// happens when a sample fails to arrive - so the blink phase rides along here
// rather than owning a second timer.
// Defined with the other draw-side helpers below; re-run whenever the theme
// changes, because _ui_switch_theme re-applies every registered themeable
// property -- including the BG_OPA this cleared -- and would otherwise put all
// the redundant fills straight back.
static void strip_all_overdraw();

// Every change of the link state goes to the serial log, and so to the
// LogScreen: the TopBar shows where the link is now, the log keeps when it
// changed. Warnings on the way down, info on the way up.
static void log_link_change(RtpsLinkState state) {
  static espp::Logger link_logger({.tag = "rtps_link", .level = espp::Logger::Verbosity::INFO});
  static std::optional<RtpsLinkState> last;
  if (last == state) {
    return;
  }
  const std::string meaning = rtps_comms_link_state_meaning(state);
  if (!last) {
    link_logger.info("{} ({})", rtps_comms_link_state_name(state), meaning);
  } else if (state > *last) {
    link_logger.info("{} -> {} ({})", rtps_comms_link_state_name(*last),
                     rtps_comms_link_state_name(state), meaning);
  } else {
    link_logger.warn("{} -> {} ({})", rtps_comms_link_state_name(*last),
                     rtps_comms_link_state_name(state), meaning);
  }
  last = state;
}

static void diag_poll();       // DiagnosticsScreen, further down
static void drive_wait_poll(); // DriveScreen entry, further down

static void rtps_poll_cb(lv_timer_t *) {
  const RtpsLinkState state = rtps_comms_link_state();
  log_link_change(state);
  lv_subject_set_int(&rtps_link_subject, static_cast<int32_t>(state));
  static uint32_t ticks = 0;
  // flip every other tick: a 500 ms half-period, i.e. a 1 Hz blink
  lv_subject_set_int(&rtps_blink_subject, static_cast<int32_t>((++ticks / 2) & 1u));
  diag_poll(); // diagnostics staleness rides the same 250 ms tick
  // and so does the wait for the MCB to enable driving (timed: drive_timing.hpp)
  drive_timed(TimedPath::TICK, drive_wait_poll);

  // Defensive: if the RTPS label's text colour/opacity are registered as
  // themeable in the SquareLine project, ui_theme_set() re-applies the theme's
  // values over whatever this indicator last painted, and since the subject has
  // not changed nothing would repaint it — a solid state would keep the theme's
  // colour indefinitely (a blinking one would self-heal within 500 ms, which is
  // what makes the bug read as intermittent). Re-notifying on a theme change
  // takes the label back either way, so this stays correct whichever way the
  // export sets those two properties.
  static uint8_t last_theme = ui_theme_idx;
  if (ui_theme_idx != last_theme) {
    last_theme = ui_theme_idx;
    lv_subject_notify(&rtps_link_subject);
    // Same reason, different property: the theme switch restored the redundant
    // background fills, so take them out again.
    strip_all_overdraw();
    // The switch itself is the Settings Theme row (or a CALL FUNCTION
    // event reaching ui_events.cpp's theme_toggle, or the remote UI); this is
    // where firmware first sees the result, so it is saved from here, and the
    // row is brought into step with a switch it did not make.
    settings_set_theme(ui_theme_idx);
    lv_subject_set_int(&theme_subject, ui_theme_idx == UI_THEME_DAY ? 1 : 0);
  }
}

/////////////////////////////////////////////////////////////////////////////
// Backlight
//
// One brightness setting, 5..100 %, whoever changes it: the RTPS brightness
// command, the Tab5's side button, and the Brightness row of Settings.
// Saved a second after it stops changing, so a run
// of steps is one flash write rather than one per step.
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kBrightnessSaveDelayMs = 1000;
