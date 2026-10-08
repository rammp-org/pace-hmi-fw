// hmi::stick: the stick pipeline (see stick_pipeline.hpp). Every expression here is today's,
// moved from main.cpp's adc_task_fn and frag_stick_config.inc without change, so the golden
// vectors replay bit-exactly; the comments say where each came from.

#include "stick/stick_pipeline.hpp"

#include <utility>

namespace hmi::stick {

// frag_stick_config.inc:10-13 (stick_horizontal_config)
espp::FloatRangeMapper::Config horizontal_config(const AxisCalMv &cal) {
  return {.center = cal.center_mv, .minimum = cal.min_mv, .maximum = cal.max_mv};
}

// frag_stick_config.inc:15-18 (stick_vertical_config)
espp::FloatRangeMapper::Config vertical_config(const AxisCalMv &cal) {
  return {
      .center = cal.center_mv, .minimum = cal.min_mv, .maximum = cal.max_mv, .invert_output = true};
}

// frag_stick_config.inc:20-27 (stick_twist_config)
espp::FloatRangeMapper::Config twist_config(const AxisCalMv &cal, float center_deadband_mv,
                                            float range_deadband_mv) {
  return {.center = cal.center_mv,
          .center_deadband = center_deadband_mv,
          .minimum = cal.min_mv,
          .maximum = cal.max_mv,
          .range_deadband = range_deadband_mv};
}

// main.cpp:1505-1516
Position mount(Position p, bool swap, bool invert_x, bool invert_y) {
  if (swap) {
    std::swap(p.x, p.y);
  }
  if (invert_x) {
    p.x = -p.x;
  }
  if (invert_y) {
    p.y = -p.y;
  }
  return p;
}

// Analog -> keypad level (main.cpp:1518-1535 at 7ea7592; its comment kept).
// Schmitt trigger (engage past the engage threshold, release below the release
// one) so the boundary can't chatter; between the two thresholds the previous
// state holds. The direction is recomputed every cycle, so rolling the stick
// from one direction to another re-aims without needing to pass through
// center. The larger component wins, so a diagonal resolves to one direction
// rather than two.
//
// How far the stick must go to count as a key is the Settings stick
// sensitivity, 1..10. The position is rescaled past the circular dead zone --
// 0 at its edge, 1 at the gate -- so the thresholds are fractions of the travel
// OUTSIDE it: level 1 engages at 0.30 (about 36% of the full throw), level 10
// at 0.01, the moment the stick leaves the dead zone, and the default 9 at
// ~0.04 (about 14%; it was 0.06, and 0.20 before that). Release is half the
// engage, so the key lets go on the way back rather than chattering at the
// boundary. The dead zone itself (center_deadzone_radius, shared with driving)
// is what keeps rest noise from engaging at any level.
//
// The int-to-float conversion was implicit in main.cpp; it is the same
// conversion, written out.
KeyThresholds key_thresholds(int sensitivity) {
  const int level = std::clamp<int>(sensitivity, SENSITIVITY_MIN, SENSITIVITY_MAX);
  const float engage = 0.30f - (static_cast<float>(level - 1) * (0.29f / 9.0f));
  return {.engage = engage, .release = engage * 0.5f};
}

// main.cpp:1537-1548
bool key_engaged(bool engaged, bool calibrating, float x, float y, KeyThresholds thresholds) {
  const float mag = std::max(std::abs(x), std::abs(y));
  if (calibrating) {
    return false; // "hold LEFT" must not page the menus or exit
  }
  if (mag > thresholds.engage) {
    return true;
  }
  if (mag < thresholds.release) {
    return false;
  }
  return engaged;
}

// main.cpp:1550-1557 (the stick's half; the remote key is applied by the caller)
std::uint32_t key_direction(bool engaged, float x, float y, const KeyCodes &codes) {
  if (!engaged) {
    return 0;
  }
  if (std::abs(x) >= std::abs(y)) {
    return x > 0 ? codes.right : codes.left;
  }
  // +Y is up after the vertical axis's inversion, and up the list is prev
  return y > 0 ? codes.up : codes.down;
}

// main.cpp:1592-1596. Settings "Speed sensitivity" scales all three axes on the
// way to the MCB, as if the stick moved that much less: 1.0x sends it as it is,
// 0.1x a tenth of it. Only there -- the bars and the UI keys keep the stick as
// it is.
float drive_speed_scale(int drive_speed) {
  return static_cast<float>(std::clamp<int>(drive_speed, DRIVE_SPEED_MIN, DRIVE_SPEED_MAX)) /
         static_cast<float>(DRIVE_SPEED_MAX);
}

// main.cpp:1598: `stick_x * scale, stick_y * scale, stick.z() * scale`
Command command(Position p, float scale) {
  return {.x = p.x * scale, .y = p.y * scale, .twist = p.twist * scale};
}

// frag_stick_config.inc (stick_pipeline_config) with frag_status_band.inc's constants
StickPipeline::Config pipeline_config(const CalibrationMv &cal, const KeyCodes &keys) {
  return {.calibration = cal,
          .center_deadzone_radius = CENTER_DEADZONE_RADIUS,
          .range_deadzone = RANGE_DEADZONE,
          .twist_center_deadband_mv = TWIST_CENTER_DEADBAND_MV,
          .twist_range_deadband_mv = TWIST_RANGE_DEADBAND_MV,
          .keys = keys};
}

// main.cpp:1441-1447 (the `static espp::Joystick stick`)
StickPipeline::StickPipeline(const Config &config)
    : config_(config)
    , joystick_(
          {.x_calibration = horizontal_config(config.calibration.horizontal),
           .y_calibration = vertical_config(config.calibration.vertical),
           .z_calibration = twist_config(config.calibration.twist, config.twist_center_deadband_mv,
                                         config.twist_range_deadband_mv),
           .type = espp::Joystick::Type::CIRCULAR,
           .center_deadzone_radius = config.center_deadzone_radius,
           .range_deadzone = config.range_deadzone,
           .log_level = espp::Logger::Verbosity::WARN}) {}

// frag_stick_config.inc:29-33 (stick_apply_cal)
void StickPipeline::apply_calibration(const CalibrationMv &cal) {
  joystick_.set_calibration(horizontal_config(cal.horizontal), vertical_config(cal.vertical),
                            config_.center_deadzone_radius, config_.range_deadzone);
  joystick_.set_z_calibration(
      twist_config(cal.twist, config_.twist_center_deadband_mv, config_.twist_range_deadband_mv));
}

// main.cpp:1500 (stick.update) and the reads of stick.x(), stick.y(), stick.z() after it
Position StickPipeline::map(float horizontal_mv, float vertical_mv, float twist_mv) {
  joystick_.update(horizontal_mv, vertical_mv, twist_mv);
  return {.x = joystick_.x(), .y = joystick_.y(), .twist = joystick_.z()};
}

} // namespace hmi::stick
