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

#include "hmi_ui/ui_poll.hpp"
// The poll is the drive session's tick (drive_session_table.hpp kTickPeriod).
static_assert(std::chrono::milliseconds{hmi::ui::UiPoll::PERIOD_MS} ==
              hmi::drive_session::kTickPeriod);
static constexpr uint32_t kRtpsPollMs = hmi::ui::UiPoll::PERIOD_MS;

// The one instance; app_main starts its timer.
static constinit hmi::ui::UiPoll ui_poll{{
    .shared = &ui_shared_subjects,
    .theme = setting_subjects.value(SETTINGS_PARAM_THEME),
    .link_state = [] { return static_cast<hmi::ui::LinkState>(rtps_comms_link_state()); },
    .link_seen = [](hmi::ui::LinkState state) { log_link_change(static_cast<RtpsLinkState>(state)); },
    .diag_poll = [] { diag_poll(); },               // diagnostics staleness rides the same tick
    .drive_tick = [] { drive_wait_poll(); },        // and so does the wait for the MCB to drive
    // The theme switch restored the redundant background fills, so take them out again. The
    // switch itself is the Settings Theme row (or a CALL FUNCTION event reaching
    // ui_events.cpp's theme_toggle, or the remote UI); this is where firmware first sees the
    // result, so it is saved from here.
    .theme_switched =
        [](uint8_t theme) {
          strip_all_overdraw();
          settings_set_theme(theme);
        },
}};

static void rtps_poll_cb(lv_timer_t *) { ui_poll.poll(); }

/////////////////////////////////////////////////////////////////////////////
// Backlight
//
// One brightness setting, 5..100 %, whoever changes it: the RTPS brightness
// command, the Tab5's side button, and the Brightness row of Settings.
// Saved a second after it stops changing, so a run
// of steps is one flash write rather than one per step.
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kBrightnessSaveDelayMs = 1000;
