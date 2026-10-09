// UiPoll: the UI island's 250 ms tick (moved from main/frag_rtps_poll.inc).

#include "hmi_ui/ui_poll.hpp"

#include "ui.h"

void hmi::ui::UiPoll::poll() {
  const LinkState state = config_.link_state();
  config_.link_seen(state);
  lv_subject_set_int(config_.shared->rtps_link, static_cast<int32_t>(state));
  // flip every other tick: a 500 ms half-period, i.e. a 1 Hz blink
  lv_subject_set_int(config_.shared->rtps_blink, static_cast<int32_t>((++ticks_ / 2) & 1u));
  config_.diag_poll();
  config_.post_tick();
  config_.drive_tick();
  config_.post_indicator();
  check_theme();
}

// Defensive: if the RTPS label's text colour/opacity are registered as
// themeable in the SquareLine project, ui_theme_set() re-applies the theme's
// values over whatever this indicator last painted, and since the subject has
// not changed nothing would repaint it — a solid state would keep the theme's
// colour indefinitely (a blinking one would self-heal within 500 ms, which is
// what makes the bug read as intermittent). Re-notifying on a theme change
// takes the label back either way, so this stays correct whichever way the
// export sets those two properties.
void hmi::ui::UiPoll::check_theme() {
  if (!theme_seen_) { // the theme at the first tick is the one to compare with
    theme_seen_ = true;
    last_theme_ = ui_theme_idx;
  }
  if (ui_theme_idx != last_theme_) {
    last_theme_ = ui_theme_idx;
    lv_subject_notify(config_.shared->rtps_link);
    config_.theme_switched(ui_theme_idx);
    // The row is brought into step with a switch it did not make.
    lv_subject_set_int(config_.theme, ui_theme_idx == UI_THEME_DAY ? 1 : 0);
  }
}
