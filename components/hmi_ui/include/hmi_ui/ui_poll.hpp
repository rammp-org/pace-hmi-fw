#pragma once
// The UI island's 250 ms poll: the link indicator and its blink, the diagnostics' staleness, the
// drive adapter's tick, and noticing a theme switch.

#include <cstdint>

#include "lvgl.h"

#include "drive_ui/fn.hpp"
#include "drive_ui/link_state.hpp"
#include "drive_ui/shared_subjects.hpp"

namespace hmi::ui {

/// One instance; main runs `poll` from an LVGL timer every PERIOD_MS. Staleness has to be
/// polled (nothing happens when a sample fails to arrive), so the blink phase, the diagnostics
/// and the drive session's deadlines ride the same tick rather than owning timers of their own.
/// Writes the `rtps_link` and `rtps_blink` subjects of `SharedSubjects` and `theme`, all
/// initialised before the timer starts (V7).
class UiPoll {
public:
  /// The tick (was kRtpsPollMs; the drive table's kTickPeriod, main static_asserts it).
  static constexpr uint32_t PERIOD_MS = 250;

  struct Config {
    const SharedSubjects *shared; ///< UiApp's subjects; `rtps_link`, `rtps_blink` are written
    lv_subject_t *theme;          ///< int: the Settings Theme row, 0 dark, 1 day
    /// The link's state now (rtps_comms_link_state). UI task.
    LinkState (*link_state)();
    /// Called with the state on every tick, before the subject is set: main logs a change.
    void (*link_seen)(LinkState state);
    /// Diagnostics staleness (DiagnosticsView::poll). UI task, lvgl_mutex held.
    Fn<void()> diag_poll;
    /// The drive adapter's tick (main's DriveAdapter, through UiApp's DriveInputs). UI task,
    /// lvgl_mutex held.
    void (*drive_tick)();
    /// The theme was switched (by any route): main takes the redundant background fills out
    /// again and saves the setting. Called after the link subject is re-notified and before the
    /// Theme row's subject is set. UI task, lvgl_mutex held.
    Fn<void(uint8_t theme)> theme_switched;
  };

  constexpr explicit UiPoll(const Config &config) noexcept
      : config_(config) {}

  /// @brief One tick, in this order: the link state (seen, then into `rtps_link`), the blink
  ///        phase, diag_poll, drive_tick, then the theme check.
  /// UI task (an LVGL timer callback), lvgl_mutex held by lv_task.
  void poll();

private:
  void check_theme();

  Config config_;
  uint32_t ticks_ = 0;      ///< ticks so far; the blink flips every other one
  bool theme_seen_ = false; ///< false until the first theme check has read ui_theme_idx
  uint8_t last_theme_ = 0;  ///< the theme at the last check
};

} // namespace hmi::ui
