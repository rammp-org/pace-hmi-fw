#pragma once
// The SettingsScreen: pages of -/+ rows.

#include <cstdint>

#include "lvgl.h"

#include "hmi_format/stepper.hpp"
#include "hmi_ui/nav_port.hpp"
#include "hmi_ui/setting_subjects.hpp"

namespace hmi::ui {

/// A page is a title and some rows; each row is the export's SettingRow component: short
/// label, label, value, and -/+ buttons that grey out at the ends of the range. Up/down move
/// between rows, left/right step the focused one (holding the stick repeats), and touch works
/// on the buttons. What a page holds, and what a step on it means, is main's: a settings row
/// sets its subject (whose owner applies and saves it); an actuator row asks main to step the
/// seat (main's seat_step), and its number moves only when the MCB says so. The screen and its
/// rows exist only while it is up.
class SettingsView {
public:
  static constexpr int ROWS_MAX = 16;
  /// How long a joystick press shows on a step button: comfortably shorter than the 250 ms
  /// key repeat, so holding the stick reads as one blink per step.
  static constexpr uint32_t PRESS_FLASH_MS = 120;
  /// The actuator rejection flash: nothing flashed.
  static constexpr int32_t REJECT_NONE = -1;
  static constexpr uint32_t REJECT_FLASH_MS = 900;

  struct Config {
    const NavPort *nav;
    lv_subject_t *locked;      ///< int: 1 = locked; a locked_only row refuses a step while 0
    SettingSubjects *subjects; ///< what each settings row shows and steps
    /// main's seat_axis_value[rammp::kSeatAxisCount]: what each actuator row shows (raw units,
    /// as the MCB last reported them).
    lv_subject_t *seat_values;
    void (*screen_ensure)(); ///< builds the screen if it is not up (main's on-demand code)
    int32_t actuators_page;  ///< the page number of the actuator rows
    void (*seat_step)(size_t row, int direction); ///< main's: one actuator step, a request
    void (*refuse)();                             ///< the refusal cue
  };

  constexpr explicit SettingsView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Creates the press-flash timer (paused), the rows' group and the rejection subject
  ///        and its timer (paused): what outlives the screen.
  /// @return the group, for the joystick
  /// app_main, before lv_task starts.
  lv_group_t *init();
  /// The rows' group (init()), for the joystick.
  [[nodiscard]] lv_group_t *group() const { return group_; }
  /// int: the page that is up, a SETTINGS_PAGE_* or Config::actuators_page. A subject because
  /// main's warning panel depends on it; main initialises it before that panel binds.
  [[nodiscard]] constexpr lv_subject_t *page() { return &page_; }
  /// @brief Fills the screen for @p page and shows it: the actuator rows (DEBUG ACTUATORS),
  ///        or the settings_spec.hpp rows of that page.
  /// @param page a SETTINGS_PAGE_* or Config::actuators_page
  /// UI task (a click handler).
  void open_page(int32_t page);
  /// @brief Empties the screen: deletes the rows (their observers and events go with them)
  ///        and ends a press flash first.
  /// UI task.
  void rows_clear();
  /// @brief Adds one row.
  /// @param spec how it reads and steps
  /// @param value its subject (raw units; VALUE_UNKNOWN greys both buttons)
  /// @param actuator an actuator row: a step is main's seat request, and the row shows the
  ///        rejection flash
  /// UI task.
  void row_add(const format::StepperSpec &spec, lv_subject_t *value, bool actuator);
  /// @brief Puts the joystick cursor on row `index`, clamped.
  /// @param index the row
  /// UI task.
  void focus(int index);
  /// @brief LV_EVENT_FOCUSED / LV_EVENT_DEFOCUSED on a row: the focused look on the row and
  ///        everything in it (the step buttons keep their DISABLED state: a different bit).
  /// @param e the event
  /// UI task.
  static void focus_cb(lv_event_t *e);

private:
  // Everything needed to drive one row, so a key callback or an observer gets it all from one
  // user_data pointer.
  struct Row {
    format::StepperSpec spec;
    lv_subject_t *value; // the row's whole truth; VALUE_UNKNOWN until known
    int index;           // position on the page; on the actuators page, the actuator id
    lv_obj_t *row;       // the SettingRow root; what takes focus
    lv_obj_t *value_label;
    lv_obj_t *minus;
    lv_obj_t *plus;
    SettingsView *view;
  };

  // The actuator a packed rejection names.
  static uint8_t reject_id(int32_t packed) { return static_cast<uint8_t>(packed & 0xFF); }
  static void value_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void limits_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void reject_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void reject_clear_cb(lv_timer_t *timer);
  static void press_flash_cb(lv_timer_t *timer);
  static void key_cb(lv_event_t *e);
  static void step_click_cb(lv_event_t *e);
  static void row_click_cb(lv_event_t *e);
  void press_flash_end();
  void press_flash(lv_obj_t *button);
  void step(const Row *row, int direction);

  Config config_;
  lv_subject_t page_{};
  Row rows_[ROWS_MAX]{};
  int row_count_ = 0; ///< rows on the page that is up; 0 while the screen is not
  int cursor_ = 0;    ///< which row the joystick is on
  lv_group_t *group_ = nullptr;
  lv_timer_t *press_flash_timer_ = nullptr;
  lv_obj_t *press_flash_button_ = nullptr;
  /// The rejection flash: which actuator was refused (low byte); REJECT_NONE = nothing.
  lv_subject_t reject_subject_{};
  lv_timer_t *reject_timer_ = nullptr;
};

} // namespace hmi::ui
