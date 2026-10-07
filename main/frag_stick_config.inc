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

// The twist pot's reading for the Read ADC task: `oversample` oneshot reads on
// ADC2, averaged; nullopt if none succeeded.
//
// Out of line on purpose (noinline). OneshotAdc::read_mv brings espp Logger's
// error paths with it, and once GCC also inlined Logger::get_time there (an fmt
// buffer of ~500 B) the ADC task's own frame grew from 256 to 736 B, and every
// call it makes (RTPS publish, the bars' observers) sat that much deeper: the
// bench's mem.stk_adc fell ~500 B (B3, image a6ff6de, 2026-10-07). Here that cold
// path holds stack only while the twist is read. Which way the inliner goes
// depends on the rest of the unit, so this pins it instead of relying on it.
[[gnu::noinline]] static std::optional<float>
read_twist_mv(espp::OneshotAdc &twist_adc, const espp::AdcConfig &twist_channel, int oversample) {
  std::optional<float> twist_mv;
  float sum = 0.0f;
  int reads = 0;
  for (int i = 0; i < oversample; ++i) {
    if (auto mv = twist_adc.read_mv(twist_channel)) {
      sum += *mv;
      ++reads;
    }
  }
  if (reads > 0) {
    twist_mv = sum / static_cast<float>(reads);
  }
  return twist_mv;
}

/////////////////////////////////////////////////////////////////////////////
// TopBar RTPS indicator
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kRtpsPollMs = 250;

