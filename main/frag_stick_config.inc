// split_main.py fragment of main.cpp (frag_stick_config: original lines 480-518); one TU, see main.cpp.
// AXIS WIRING: the gimbal pots are cross-wired relative to the channel names.
// ADC1_CH1 (GPIO17) is the HORIZONTAL axis and reads higher to the right, so
// it feeds the joystick's X with no inversion. ADC1_CH0 (GPIO16) is the
// VERTICAL axis and reads *lower* moving up, so it feeds Y with invert_output
// — after which +Y is up, as the rest of the code assumes. Twist reads higher
// clockwise. Fixed here so every consumer (UI bars, the keypad, RTPS)
// sees correct axes without compensating itself; joystick_cal.cpp's step
// prompts rely on the same directions.
static espp::FloatRangeMapper::Config stick_horizontal_config(const JoystickCal &cal) {
  const JoystickAxisCal &a = cal[JOY_HORIZONTAL];
  return {.center = a.center_mv, .minimum = a.min_mv, .maximum = a.max_mv};
}

static espp::FloatRangeMapper::Config stick_vertical_config(const JoystickCal &cal) {
  const JoystickAxisCal &a = cal[JOY_VERTICAL];
  return {.center = a.center_mv, .minimum = a.min_mv, .maximum = a.max_mv, .invert_output = true};
}

static espp::FloatRangeMapper::Config stick_twist_config(const JoystickCal &cal) {
  const JoystickAxisCal &a = cal[JOY_TWIST];
  return {.center = a.center_mv,
          .center_deadband = kTwistCenterDeadbandMv,
          .minimum = a.min_mv,
          .maximum = a.max_mv,
          .range_deadband = kTwistRangeDeadbandMv};
}

static void stick_apply_cal(espp::Joystick &stick, const JoystickCal &cal) {
  stick.set_calibration(stick_horizontal_config(cal), stick_vertical_config(cal),
                        kStickCenterDeadzoneRadius, kStickRangeDeadzone);
  stick.set_z_calibration(stick_twist_config(cal));
}

/////////////////////////////////////////////////////////////////////////////
// TopBar RTPS indicator
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kRtpsPollMs = 250;

