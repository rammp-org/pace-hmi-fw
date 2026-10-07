#pragma once
// Golden-vector types for the stick pipeline characterisation (step 11, refactor.md §3.2).
//
// One `StickVector` is one 33 ms cycle of the ADC task: what the hardware and the other tasks
// gave it (inputs), and what it did (outputs). Vectors run in order; a vector with `reset`
// starts a scenario from power-on: a fresh joystick on calibration `power_on_cal`, the key
// Schmitt trigger released and joy_key 0. Before every cycle joy_flick is cleared, as the
// keypad read (main.cpp:730, `joy_flick.exchange(0)`) does between two ADC cycles, so
// `joy_flick` out is the flick this cycle latched.
//
// Float outputs are kept as bit patterns: the replay is bit-exact (refactor.md §3.2).

#include <cstdint>

namespace stick_test {

/// Calibrations the vectors use, by id. 0 = none (only valid for `new_cal`).
enum CalId : std::uint8_t {
  CAL_NONE = 0,
  CAL_IDEAL = 1,  ///< main.cpp kIdealAxis: 0 / 1650 / 3300 mV on every axis
  CAL_BOARD2 = 2, ///< board 2: horizontal 11/1507/2971, vertical 6/1510/2962, twist 10/1477/2960
};

struct StickVector {
  // --- inputs -------------------------------------------------------------------------------
  bool reset;                // start of a scenario (power-on state)
  std::uint8_t power_on_cal; // CalId the joystick is built on at reset
  bool horiz_ok;             // adc.get_mv(channels[1]) has a value
  bool vert_ok;              // adc.get_mv(channels[0]) has a value
  bool twist_ok;             // at least one oneshot twist read succeeded
  float horiz_mv;            // ADC1_CH1, horizontal pot
  float vert_mv;             // ADC1_CH0, vertical pot
  float twist_mv;            // the twist after twist_lowpass (the filter stays in main)
  std::uint8_t new_cal;      // what joystick_cal_take_new() returns this cycle (CalId)
  bool calibrating;          // joystick_cal_running()
  bool swap;                 // stick_swap
  bool invert_x;             // stick_invert_x
  bool invert_y;             // stick_invert_y
  int sensitivity;           // stick_sensitivity (1..10 in Settings; out-of-range is clamped)
  std::uint32_t remote_key;  // remote_key
  int drive_speed;           // drive_speed (1..10 tenths; out-of-range is clamped)
  bool drives;               // stick_drives: the gate
  bool button;               // joy_button_pressed
  // --- outputs ------------------------------------------------------------------------------
  bool published;          // rtps_comms_publish_adc was called this cycle
  std::uint32_t cmd_x;     // its x (float bits)
  std::uint32_t cmd_y;     // its y (float bits)
  std::uint32_t cmd_twist; // its twist (float bits)
  std::uint32_t buttons;   // its rammp::Buttons value
  bool bars_set;           // the three bar subjects were written
  std::int32_t bar_x;      // adc_x_subject
  std::int32_t bar_y;      // adc_y_subject
  std::int32_t bar_twist;  // adc_twist_subject
  std::uint32_t joy_key;   // joy_key after the cycle
  std::uint32_t joy_flick; // joy_flick after the cycle (latched this cycle)
};

/// What one cycle produced; the output half of a StickVector.
struct StickOutputs {
  bool published;
  std::uint32_t cmd_x;
  std::uint32_t cmd_y;
  std::uint32_t cmd_twist;
  std::uint32_t buttons;
  bool bars_set;
  std::int32_t bar_x;
  std::int32_t bar_y;
  std::int32_t bar_twist;
  std::uint32_t joy_key;
  std::uint32_t joy_flick;
};

/// LVGL's key codes (lvgl/src/core/lv_group.h): the values joy_key carries.
inline constexpr std::uint32_t KEY_UP = 17;
inline constexpr std::uint32_t KEY_DOWN = 18;
inline constexpr std::uint32_t KEY_RIGHT = 19;
inline constexpr std::uint32_t KEY_LEFT = 20;

} // namespace stick_test
