#pragma once
// The DiagnosticsScreen: the MCB's live readings, one row per item of the diagnostics table.

#include <cstdint>

#include "lvgl.h"

#include "hmi_ui/fn.hpp"
#include "hmi_ui/nav_port.hpp"
#include "messages/joystick_message.hpp"

namespace hmi::ui {

/// One row per entry in RAMMP_DIAG_TABLE: short label, label, and up to three readings, each
/// under its unit. The readings are the view's subjects (CS-UI-05), set under lvgl_mutex by the
/// RTPS handler (RtpsUiBridge); DiagnosticsFreqLabel shows how fast they arrive. Once nothing has
/// arrived for rammp::kDiagTimeout, or nothing ever has, every row's text and the label turn red
/// and blink: readings the MCB stopped sending must not sit there looking current. The screen is
/// built on demand; the rows exist only while it is up.
class DiagnosticsView {
public:
  /// The red of a stale reading (the self test's FAIL red).
  static constexpr uint32_t STALE_COLOUR = 0xFF5050;

  struct Config {
    const NavPort *nav;
    /// main's RTPS arrival statistics: when the latest sample landed (esp_timer us, 0 = never)
    /// and the arrival rate in tenths of a Hz. UI task.
    void (*stats)(int64_t *last_us, int32_t *rate_tenths_hz);
    /// No sample for this long (us) = stale (main's rammp::kDiagTimeout).
    int64_t timeout_us;
    lv_subject_t *blink;                 ///< int: 0/1 blink phase
    Fn<void()> screen_ensure;            ///< builds the screen if it is not up (OnDemandScreens)
    void (*row_focus_cb)(lv_event_t *e); ///< the FOCUSED/DEFOCUSED look for a row (SettingsView's)
  };

  constexpr explicit DiagnosticsView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Initialises the readings (unknown), stale (1) and rate (0) subjects: before the
  ///        poll timer that keeps the latter current, and before any RTPS sample can land.
  /// app_main, before lv_task starts and before RTPS starts.
  void init_subjects();
  /// @brief Keeps the stale and rate subjects current: staleness has to be polled, since
  ///        nothing happens when a sample fails to arrive.
  /// UI task (UiPoll, every 250 ms).
  void poll();
  /// The readings, raw, [item][reading]; VALUE_UNKNOWN until the MCB sends one. Written by
  /// the RTPS handler under lvgl_mutex.
  [[nodiscard]] constexpr lv_subject_t (*values())[rammp::kDiagFields] { return values_; }
  /// int: 1 = nothing within rammp::kDiagTimeout, or ever.
  [[nodiscard]] constexpr lv_subject_t *stale() { return &stale_; }
  /// int: arrival rate, tenths of a Hz.
  [[nodiscard]] constexpr lv_subject_t *rate() { return &rate_; }
  /// The rows' group (init()), for the joystick.
  [[nodiscard]] lv_group_t *group() const { return group_; }
  /// @brief Creates the rows' group (what outlives the screen).
  /// @return the group, for the joystick
  /// app_main, before lv_task starts.
  lv_group_t *init();
  /// @brief Builds the rows and shows the screen.
  /// UI task (a click).
  void open();
  /// @brief Deletes the rows (their observers and events go with them).
  /// UI task (the screen's unload).
  void rows_clear();
  /// @brief Puts the joystick on row `index`, clamped.
  /// @param index the row
  /// UI task.
  void focus(int index);
  /// @brief Paints DiagnosticsFreqLabel: the rate, or "No data" in blinking red while stale.
  /// @param label the label
  /// UI task (observers on the rate, stale and blink subjects).
  void paint_freq(lv_obj_t *label);

private:
  struct Field {
    uint8_t item;
    uint8_t field;
  };
  static void value_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void row_stale_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void key_cb(lv_event_t *e);
  static void mark_stale(lv_obj_t *obj, bool stale, lv_opa_t opa);
  lv_opa_t blink_opa() const;

  Config config_;
  lv_subject_t values_[rammp::kDiagCount][rammp::kDiagFields]{};
  lv_subject_t stale_{};
  lv_subject_t rate_{};
  Field fields_[rammp::kDiagCount][rammp::kDiagFields]{}; ///< what each value observer shows
  lv_obj_t *rows_[rammp::kDiagCount]{};
  int row_count_ = 0; ///< rows on screen; 0 while it is not up
  int cursor_ = 0;    ///< which row the joystick is on
  lv_group_t *group_ = nullptr;
};

} // namespace hmi::ui
