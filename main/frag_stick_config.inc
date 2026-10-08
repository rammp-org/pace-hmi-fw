// split_main.py fragment of main.cpp (frag_stick_config: original lines 480-518); one TU, see main.cpp.
// AXIS WIRING: the gimbal pots are cross-wired relative to the channel names.
// ADC1_CH1 (GPIO17) is the HORIZONTAL axis and reads higher to the right, so
// it feeds the joystick's X with no inversion. ADC1_CH0 (GPIO16) is the
// VERTICAL axis and reads *lower* moving up, so it feeds Y with invert_output
// — after which +Y is up, as the rest of the code assumes. Twist reads higher
// clockwise. Fixed in hmi::stick (components/stick: horizontal_config,
// vertical_config, twist_config) so every consumer (UI bars, the keypad, RTPS)
// sees correct axes without compensating itself; joystick_cal.cpp's step
// prompts rely on the same directions.
//
// The stick math itself (mapping, mount, keys, gate, speed scale) is
// hmi::stick::StickPipeline. The ADC task runs one cycle of it per sample and
// AdcStickIo below is everything that cycle reads and writes here, in the order
// the code ran inline before it was extracted. stick_inject.hpp includes it, and
// adds StickSlot: the pipeline, or with CONFIG_HMI_BENCH_STICK_INJECT the bench
// stick injection in front of it.
#include "stick_inject.hpp"

static_assert(hmi::stick::SENSITIVITY_MIN == SETTINGS_STICK_SENSITIVITY_MIN &&
                  hmi::stick::SENSITIVITY_MAX == SETTINGS_STICK_SENSITIVITY_MAX,
              "hmi::stick clamps the stick sensitivity to the Settings range");
static_assert(hmi::stick::DRIVE_SPEED_MIN == SETTINGS_DRIVE_SPEED_MIN &&
                  hmi::stick::DRIVE_SPEED_MAX == SETTINGS_DRIVE_SPEED_MAX,
              "hmi::stick clamps the drive speed to the Settings range");

static hmi::stick::CalibrationMv stick_cal_mv(const JoystickCal &cal) {
  const auto axis = [](const JoystickAxisCal &a) {
    return hmi::stick::AxisCalMv{.min_mv = a.min_mv, .center_mv = a.center_mv, .max_mv = a.max_mv};
  };
  return {.horizontal = axis(cal[JOY_HORIZONTAL]),
          .vertical = axis(cal[JOY_VERTICAL]),
          .twist = axis(cal[JOY_TWIST])};
}

// How far each axis travels is measured per unit (joystick_cal.cpp, the CALIBRATE button on
// the JoystickTest screen); the dead zones are the stick's (hmi::stick::pipeline_config).
static hmi::stick::StickPipeline::Config stick_pipeline_config(const JoystickCal &cal) {
  return hmi::stick::pipeline_config(
      stick_cal_mv(cal),
      {.up = LV_KEY_UP, .down = LV_KEY_DOWN, .right = LV_KEY_RIGHT, .left = LV_KEY_LEFT});
}

// The ADC task's side of StickPipeline::cycle. Runs on the ADC task only; every
// member is what the stick block of adc_task_fn did inline before.
struct AdcStickIo {
  espp::SimpleLowpassFilter &twist_lowpass;

  // A calibration run just finished: the pipeline switches to it between two
  // samples, on the task that owns the stick.
  std::optional<hmi::stick::CalibrationMv> take_new_calibration() {
    if (auto cal = joystick_cal_take_new()) {
      return stick_cal_mv(*cal);
    }
    return std::nullopt;
  }
  float smooth_twist_mv(float twist_mv) { return twist_lowpass(twist_mv); }
  void note_raw_mv(float horizontal_mv, float vertical_mv, float twist_mv) {
    joystick_cal_note_raw(horizontal_mv, vertical_mv, twist_mv);
  }
  // While a calibration run owns the stick nothing downstream may act on it:
  // the user is being told to push it to every end in turn.
  bool calibrating() { return joystick_cal_running(); }
  bool swap() { return stick_swap.load(); }
  bool invert_x() { return stick_invert_x.load(); }
  bool invert_y() { return stick_invert_y.load(); }
  int sensitivity() { return stick_sensitivity.load(); }
  uint32_t joy_key() { return ::joy_key.load(); }
  uint32_t remote_key() { return ::remote_key.load(); }
  void set_joy_key(uint32_t key) { ::joy_key.store(key); }
  void set_joy_flick(uint32_t key) { joy_flick.store(key); }
  void show(const hmi::stick::Position &mounted) {
    // lv_subject_set_int runs the bar's observer callback synchronously,
    // which touches the widget, so it needs the LVGL lock. Tried, not
    // waited for: the UI holds it for a whole frame (a full redraw is
    // ~100 ms, more with Flip screen), and the stick's path to the MCB
    // must not queue behind a render. A busy UI just gets the bars
    // one cycle later.
    std::unique_lock<std::recursive_mutex> lock(lvgl_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
      lv_subject_set_int(joystick_view.x(), static_cast<int32_t>(mounted.x * 100.0f));
      lv_subject_set_int(joystick_view.y(), static_cast<int32_t>(mounted.y * 100.0f));
      lv_subject_set_int(joystick_view.twist(), static_cast<int32_t>(mounted.twist * 100.0f));
    }
  }
  int drive_speed() { return ::drive_speed.load(); }
  // The gate. The MCB gets a centred stick whenever the stick is doing something
  // else: while a calibration run sweeps it (the pipeline does not read this
  // then), and whenever it is walking the UI rather than driving -- any screen
  // but Drive, or Drive with the menu open. Drive stays ACTIVE across the menu
  // and the other screens, as the spec draws it, so this is what keeps a push
  // meant for the next row from moving the chair. The bars keep moving.
  bool stick_drives() { return ::stick_drives.load(); }
  bool button_pressed() { return joy_button_pressed.load(); }
  // The MCB gets the same calibrated -1..+1 values the bars show (+Y forward,
  // deadzones applied), scaled by Settings "Speed sensitivity" and the gate, so
  // it needs no calibration of its own. Quiet no-op until RTPS is up and a
  // subscriber is discovered.
  bool publish(const hmi::stick::Command &command, bool button) {
    return rtps_comms_publish_adc(command.x, command.y, command.twist,
                                  button ? rammp::Buttons::JOYSTICK : rammp::Buttons::NONE);
  }
};

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

