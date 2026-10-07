#pragma once
// hmi::stick: the stick pipeline. Raw millivolts from the three pots in, the keypad key and
// the motion command out. It is the continuous logic CS-SAF-02 asks to be a pure function
// with its own tests: everything it reads or writes outside itself goes through the `Io` the
// caller passes to cycle(), in a fixed order, and it holds only the joystick mapping and the
// key trigger's one bit.
//
// Extracted from main.cpp's adc_task_fn (dev_refactor 7ea7592, lines 1481-1601) and
// main/frag_stick_config.inc. REFACTOR ONLY: it reproduces that code's golden vectors
// bit-exactly (components/stick/test), including what is unsafe today (refactor.md §1):
//   - H3: a pot outside its calibration is clamped to full deflection (open circuit = full);
//   - H9: an invalid ADC cycle publishes nothing (no neutral);
//   - the gate is a multiply (x * 0.0f): a negative deflection goes out as -0.0, a NaN as NaN.
// Fixes are parked proposals (refactor.md §3.3) and need two approvals.
//
// No LVGL, no ESP-IDF, no allocation after construction. The mapping engine is
// components/joystick (espp::Joystick), as today.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "joystick.hpp"

namespace hmi::stick {

/// Settings ranges (main/settings_spec.h: STICK_SENSITIVITY and DRIVE_SPEED, 1..10). main
/// static_asserts that they still match.
inline constexpr int SENSITIVITY_MIN = 1;
inline constexpr int SENSITIVITY_MAX = 10;
inline constexpr int DRIVE_SPEED_MIN = 1;
inline constexpr int DRIVE_SPEED_MAX = 10;

/// One pot's travel in raw ADC millivolts: min < center < max (joystick_cal's job).
struct AxisCalMv {
  float min_mv;
  float center_mv;
  float max_mv;
};

/// The three pots. Horizontal reads higher to the right; vertical reads LOWER moving up;
/// twist reads higher clockwise (main.cpp's AXIS WIRING note).
struct CalibrationMv {
  AxisCalMv horizontal;
  AxisCalMv vertical;
  AxisCalMv twist;
};

/// The stick, calibrated: each axis in [-1, 1], 0 at rest, +x right, +y forward/up, +twist
/// clockwise. X/Y have the circular dead zone applied and stay within the unit circle.
struct Position {
  float x;
  float y;
  float twist;
};

/// One cycle's three ADC reads, raw millivolts; empty when that read failed. Named fields, so a
/// call site says which channel is which (`.horizontal_mv = ...`): X and Y swapped would map the
/// stick onto the wrong axes and the compiler cannot see it in two same-typed parameters.
struct RawReadsMv {
  std::optional<float> horizontal_mv; ///< ADC1_CH1 (GPIO17)
  std::optional<float> vertical_mv;   ///< ADC1_CH0 (GPIO16)
  std::optional<float> twist_mv;      ///< ADC2_CH3 (GPIO52), oversampled and averaged
};

/// What the MCB is sent: the mounted position times the drive scale (0 when gated).
struct Command {
  float x;
  float y;
  float twist;
};

/// Fractions of the travel outside the dead zone where a key engages and lets go.
struct KeyThresholds {
  float engage;
  float release;
};

/// The key codes joy_key carries (LVGL's LV_KEY_*, passed in so this stays LVGL-free).
struct KeyCodes {
  std::uint32_t up;
  std::uint32_t down;
  std::uint32_t right;
  std::uint32_t left;
};

/// Range-mapper configs for each pot (were stick_*_config in frag_stick_config.inc).
[[nodiscard]] espp::FloatRangeMapper::Config horizontal_config(const AxisCalMv &cal);
/// The vertical pot reads lower moving up, so its output is inverted: +y is up.
[[nodiscard]] espp::FloatRangeMapper::Config vertical_config(const AxisCalMv &cal);
[[nodiscard]] espp::FloatRangeMapper::Config
twist_config(const AxisCalMv &cal, float center_deadband_mv, float range_deadband_mv);

/// The stick as mounted: swap first, then mirror, so "mirror left/right" always means the
/// direction the user pushes. Twist is untouched.
[[nodiscard]] Position mount(Position p, bool swap, bool invert_x, bool invert_y);

/// Sensitivity 1..10 (clamped) to the key thresholds: level 1 engages at 0.30, level 10 at
/// 0.01; release is half the engage.
[[nodiscard]] KeyThresholds key_thresholds(int sensitivity);

/// The key's Schmitt trigger: released while calibrating, engaged past `engage`, released
/// below `release`, and between the two it keeps `engaged`. The larger axis counts.
[[nodiscard]] bool key_engaged(bool engaged, bool calibrating, float x, float y,
                               KeyThresholds thresholds);

/// The key the stick holds: 0 when not engaged, else the larger axis's direction (a tie goes
/// to x).
[[nodiscard]] std::uint32_t key_direction(bool engaged, float x, float y, const KeyCodes &codes);

/// Settings "Speed sensitivity" 1..10 (clamped) as a scale: 1.0 sends the stick as it is.
[[nodiscard]] float drive_speed_scale(int drive_speed);

/// The command: each axis times `scale`. A multiply, also when the gate makes it 0.
[[nodiscard]] Command command(Position p, float scale);

/// The pipeline. One instance, owned by the ADC task (it was app_main's `static
/// espp::Joystick stick` and the key block's `static bool engaged`).
///
/// `Io` is what one ADC cycle talks to, called in exactly this order (today's order):
///   std::optional<CalibrationMv> take_new_calibration();    // joystick_cal_take_new
///   float smooth_twist_mv(float twist_mv);                   // twist_lowpass
///   void note_raw_mv(float horizontal_mv, float vertical_mv, float twist_mv);
///   bool calibrating();                                      // joystick_cal_running
///   bool swap(); bool invert_x(); bool invert_y();           // Settings, mount
///   int sensitivity();                                       // Settings
///   std::uint32_t joy_key(); std::uint32_t remote_key();
///   void set_joy_key(std::uint32_t); void set_joy_flick(std::uint32_t);
///   void show(const Position &mounted);                      // the bars
///   int drive_speed(); bool stick_drives(); bool button_pressed();
///   bool publish(const Command &command, bool button_pressed);  // XYTwist
class StickPipeline {
public:
  struct Config {
    CalibrationMv calibration;      ///< what the stick starts on
    float center_deadzone_radius;   ///< X/Y circular dead zone, fraction of the travel
    float range_deadzone;           ///< X/Y: within this of the edge reads as full
    float twist_center_deadband_mv; ///< twist dead band around center
    float twist_range_deadband_mv;  ///< twist: within this of either end reads as full
    KeyCodes keys;
  };

  explicit StickPipeline(const Config &config);

  /// Switches to a new calibration (between two cycles, on the owning task).
  void apply_calibration(const CalibrationMv &cal);

  /// Raw millivolts to the calibrated position (circular dead zone on X/Y, twist mapped on
  /// its own). Out-of-calibration input is clamped to full deflection (H3).
  [[nodiscard]] Position map(float horizontal_mv, float vertical_mv, float twist_mv);

  /// One ADC cycle. Returns whether XYTwist was published. An invalid cycle (any read
  /// missing) calls nothing on `io` and publishes nothing (H9).
  /// always_inline (also update_keys): the ADC task has ~1.5 kB of stack free and the guard
  /// is "no less than before" (refactor.md §3.2); inlined, the cycle costs the task's frame
  /// what the inline code did (RISC-V GCC 15.2 -O2 estimate: +16 B), outlined about +64 B.
  template <typename Io>
  [[nodiscard, gnu::always_inline]] inline bool cycle(Io &io, const RawReadsMv &raw);

  /// The key trigger's state (for tests).
  [[nodiscard]] bool key_engaged_state() const { return key_engaged_; }

private:
  template <typename Io>
  [[gnu::always_inline]] inline void update_keys(Io &io, const Position &mounted, bool calibrating);

  Config config_;
  espp::Joystick joystick_;
  bool key_engaged_ = false;
};

template <typename Io> inline bool StickPipeline::cycle(Io &io, const RawReadsMv &raw) {
  if (!(raw.vertical_mv && raw.horizontal_mv && raw.twist_mv)) {
    return false;
  }
  const float horizontal_mv = *raw.horizontal_mv;
  const float vertical_mv = *raw.vertical_mv;
  const float twist_mv = *raw.twist_mv;
  // A calibration run just finished: switch to it here, between two samples.
  if (auto cal = io.take_new_calibration()) {
    apply_calibration(*cal);
  }
  const float twist_smoothed_mv = io.smooth_twist_mv(twist_mv);
  io.note_raw_mv(horizontal_mv, vertical_mv, twist_smoothed_mv);
  // While a calibration run owns the stick nothing downstream may act on it.
  const bool calibrating = io.calibrating();
  const Position mapped = map(horizontal_mv, vertical_mv, twist_smoothed_mv);
  const bool swap = io.swap();
  const bool invert_x = io.invert_x();
  const bool invert_y = io.invert_y();
  const Position mounted = mount(mapped, swap, invert_x, invert_y);
  update_keys(io, mounted, calibrating);
  io.show(mounted);
  // The gate: a calibration run or a stick that is walking the UI sends 0 * the stick.
  const float speed = drive_speed_scale(io.drive_speed());
  const float scale = calibrating || !io.stick_drives() ? 0.0f : speed;
  return io.publish(command(mounted, scale), io.button_pressed());
}

template <typename Io>
inline void StickPipeline::update_keys(Io &io, const Position &mounted, bool calibrating) {
  const KeyThresholds thresholds = key_thresholds(io.sensitivity());
  key_engaged_ = key_engaged(key_engaged_, calibrating, mounted.x, mounted.y, thresholds);
  const std::uint32_t was = io.joy_key();
  if (io.remote_key() != 0) {
    io.set_joy_key(io.remote_key());
  } else {
    io.set_joy_key(key_direction(key_engaged_, mounted.x, mounted.y, config_.keys));
  }
  // A fresh engage (or a re-aim) is latched for the keypad (joy_flick).
  if (const std::uint32_t now = io.joy_key(); now != 0 && now != was) {
    io.set_joy_flick(now);
  }
}

} // namespace hmi::stick
