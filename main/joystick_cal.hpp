#pragma once

/**
 * @file joystick_cal.hpp
 * @brief Joystick calibration: the measured travel of each axis, the run that
 *        measures it, and the file it is kept in.
 *
 * A calibration is three raw-mV numbers per axis - where it rests and the two
 * ends of its travel - and nothing else. What the firmware does with that
 * travel (deadzones, the Y inversion, the circular gimbal) stays in main.cpp.
 *
 * The run starts from the CALIBRATE button on the JoystickTest screen and
 * prompts, one step at a time: let go, push left, right, forward, back, twist
 * clockwise, counter-clockwise, let go. It runs on the LVGL task; the ADC task
 * only feeds it samples and picks up the result between two cycles, so the
 * joystick is never reconfigured from two tasks at once.
 *
 * Kept in <file system root>/joystick_cal.txt (espp::FileSystem: LittleFS on
 * the `storage` partition), as text, so it can be read off a board by eye.
 */

#include <array>
#include <functional>
#include <optional>

#include "lvgl.h"

/// One axis's travel in raw ADC millivolts; min < center < max. Which physical
/// direction reads which end is main.cpp's business (its AXIS WIRING note).
struct JoystickAxisCal {
  float min_mv;
  float center_mv;
  float max_mv;
};

enum JoystickAxis { JOY_HORIZONTAL = 0, JOY_VERTICAL = 1, JOY_TWIST = 2 };

/// Indexed by JoystickAxis.
using JoystickCal = std::array<JoystickAxisCal, 3>;

/// Mount the file system and load the saved calibration, falling back to
/// `defaults` when there is none or it is implausible. Returns what is now in
/// use. Call once from app_main, before the ADC task starts.
JoystickCal joystick_cal_load(const JoystickCal &defaults);

/// Whether the calibration in use is the one saved in flash, rather than the
/// compiled-in defaults. Any task.
bool joystick_cal_saved();

/// The calibration in use. Any task.
JoystickCal joystick_cal_current();

/// The JoystickTest screen's widgets the run drives, plus the shared 1 Hz
/// blink phase its prompts blink on.
struct JoystickCalUi {
  lv_obj_t *screen = nullptr;       ///< leaving it cancels a run
  lv_obj_t *button = nullptr;       ///< starts a run, and cancels one
  lv_obj_t *button_label = nullptr; ///< reads CANCEL while a run is going
  lv_obj_t *instructions = nullptr; ///< the prompts, then the result
  lv_subject_t *blink = nullptr;    ///< 0/1, flipped by main.cpp's poll timer
  std::function<void()> feedback;   ///< a step was captured (click, buzz)
};

/// Bind the run to its widgets. Same context as main.cpp's other bindings:
/// after ui_init, before the LVGL task runs, and after `blink` is initialised.
void joystick_cal_init_ui(const JoystickCalUi &ui);

/// ADC task, every cycle it read all three axes: the values the stick is fed.
void joystick_cal_note_raw(float horizontal_mv, float vertical_mv, float twist_mv);

/// A run owns the stick: nothing downstream may act on its position (it is
/// being pushed to every end in turn). Any task.
bool joystick_cal_running();

/// A calibration the ADC task should switch to, once per finished run.
std::optional<JoystickCal> joystick_cal_take_new();
