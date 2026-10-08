#pragma once
// The SeatScreen: the function buttons, and the adjustment page over them (spec 04b).

#include <array>
#include <cstddef>
#include <cstdint>

#include "lvgl.h"

#include "drive_ui/fn.hpp"
#include "hmi_format/stepper.hpp"
#include "hmi_ui/button_grid.hpp"
#include "hmi_ui/nav_port.hpp"
#include "messages/joystick_message.hpp"

namespace hmi::ui {

/// The seat screen's two pages and their joystick walks. Picking a function button names its
/// motion on the adjustment page and shows the page over the buttons; "<", or left from the
/// page's first column, hides it again. The page's "-"/"+" and three presets ask main to move
/// the axis: the seat command path (UiApp's seat_step and seat_request, safety-relevant) stays
/// in main, and nothing here changes a seat value. The numbers on both pages show only what
/// the MCB reports, through UiApp's value subjects.
class SeatView {
public:
  static constexpr size_t AXES = rammp::kSeatAxisCount;

  struct Config {
    const NavPort *nav;
    lv_subject_t *values;                     ///< UiApp's seat_axis_value_[AXES]; int, raw units
    Fn<void(size_t row, int direction)> step; ///< UiApp's seat_step: one step from the MCB's value
    Fn<void(rammp::SeatAxis axis, int32_t target)> request; ///< UiApp's seat_request (absolute)
    Fn<void()> show_buttons_page; ///< this view's show_buttons_page (the grids' off_left)
    void (*keep_overlay_fill)(lv_obj_t *obj); ///< the overdraw exemption
  };

  constexpr explicit SeatView(const Config &config) noexcept
      : config_(config)
      , buttons_grid_{.off_bottom = config.nav->to_key}
      , adjust_grid_{.left_edge_is_back = true,
                     .off_bottom = config.nav->to_key,
                     .off_left = config.show_buttons_page} {}

  /// @brief Wires both pages: their grids, groups, focus looks and click callbacks.
  /// app_main, before lv_task starts.
  void init_pages();
  /// @brief Initialises the value subjects (unknown until the MCB reports) and the function
  ///        label's subject, and binds the numbers on both pages to them.
  /// app_main, before lv_task starts; before anything else binds to the values (V7).
  void init_values();
  /// @brief Hides the adjustment page and hands the joystick back to the function buttons,
  ///        on the one last selected. Also the screen's arrival.
  /// UI task.
  void show_buttons_page();
  /// @brief Redraws the adjustment page's two numbers and its preset highlight for the
  ///        selected axis; nothing before a function button has picked one.
  /// UI task (or the RTPS receive task under lvgl_mutex, through the value observers).
  void angle_refresh();
  /// The function buttons' grid (UiApp's arrival resets its cursor on arrival).
  constexpr ButtonGrid &buttons_grid() noexcept { return buttons_grid_; }

private:
  // Both pages' cells in the order they are drawn, and the function buttons' labels.
  void fill_grids();
  // Which seat axis each function button adjusts: the rows of the axis table in order, -1
  // for a button with no row behind it (the Static/Dynamic pair).
  static constexpr int button_axis(int row, int col);
  // A preset's target, read off its label in the axis' display units.
  static int32_t preset_target(size_t row, const lv_obj_t *label);
  // LVGL event callbacks; user_data is the view. UI task.
  static void click_cb(lv_event_t *e);
  static void adjust_click_cb(lv_event_t *e);
  static void back_cb(lv_event_t *e);
  // Value observers. UI task, or the RTPS receive task under lvgl_mutex.
  static void angle_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void button_value_observer(lv_observer_t *observer, lv_subject_t *subject);

  static constexpr size_t FUNCTION_TEXT_SIZE = 24;

  Config config_;
  ButtonGrid buttons_grid_;
  ButtonGrid adjust_grid_;
  lv_group_t *buttons_group_ = nullptr;
  lv_group_t *adjust_group_ = nullptr;
  /// The label each function button names on the adjustment page; null for the inert pair.
  lv_obj_t *labels_[GRID_MAX_ROWS][GRID_MAX_COLS]{};
  int selected_axis_ = -1; ///< the axis the adjustment page shows; -1 until a button picks one
  /// Which page is showing: 0 = the function buttons, 1 = the adjustment panel over them
  /// (spec 04b). Was main's seat_page.
  int page_ = 0;
  std::array<format::StepperSpec, AXES> formats_{}; ///< how each axis' number is drawn
  lv_subject_t function_subject_{}; ///< string: which function the adjustment page shows
  std::array<char, FUNCTION_TEXT_SIZE> function_buf_{};
  std::array<char, FUNCTION_TEXT_SIZE> function_prev_buf_{};
};

} // namespace hmi::ui
