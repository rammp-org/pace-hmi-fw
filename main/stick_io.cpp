/**
 * @file stick_io.cpp
 * @brief The stick task's wiring (stick_io.hpp): the control island's Config and the
 *        calibration it boots on, the parts that run once, at start-up. Moved from app_main
 *        unchanged.
 */

#include "stick_io.hpp"

// The control island ("Read ADC"): its task, the continuous ADC (X/Y) and the motion guard.
MainStickIsland::Config stick_island_config() {
  // The motion guard's link flags (C4-b): rtps_comms' atomics, alive as long as the firmware.
  const RtpsLinkFlags link_flags = rtps_comms_link_flags();
  return {
      .task =
          {
              .name = "Read ADC",
              // Hazard fix C4 (H11, hazard-c4-spec.md §5, REQ-CTL-13): above every
              // application task and sys_evt/tcpip on core 0, below IDF's timing services
              // (esp_timer 22, esp_hosted 23, ipc0 24); pinned to core 0, away from lv_task
              // and tab5_audio (20, core 1); 6144 B = room over 1.5x the stress high-water
              // mark (2908 B used of 4096, CS-MEM-04) for the guard and C2.
              .stack_size_bytes = 6144,
              .priority = 21,
              .core_id = 0,
          },
      .task_log_level = espp::Logger::Verbosity::INFO,
      // this initailizes the DMA and filter task for the continuous adc
      .adc = {.sample_rate_hz = 1 * 1000,
              .channels =
                  {{.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0, .attenuation = ADC_ATTEN_DB_12},
                   {.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_1, .attenuation = ADC_ATTEN_DB_12}},
              .convert_mode = ADC_CONV_SINGLE_UNIT_1,
              // Approved by the owner (E5, 2026-10-11; docs/plans/hazard-c2-cadc-priority.md):
              // the X/Y producer at lv_task's priority, so a busy UI task on its core cannot
              // starve it into C2's STALE fault. espp's default is 5.
              .task_priority = 20,
              .window_size_bytes = 1024,
              .log_level = espp::Logger::Verbosity::WARN},
      .twist_lowpass = {.time_constant = 0.08f},
      .guard = {.sources = &ui_app.guard_sources(),
                .flags = {.net_failed = link_flags.net_failed,
                          .link_up = link_flags.link_up,
                          .got_ip = link_flags.got_ip,
                          .stick_drives = stick_drives},
                .telemetry = &ui_app.guard_telemetry()},
  };
}

JoystickCal stick_calibration_at_boot(AdcStickIo::State &state) {
  // Joystick calibration: where each axis rests and the two ends of its travel,
  // in raw mV. Measured per unit by CALIBRATE on the JoystickTest screen and
  // kept in flash (joystick_cal.cpp); these ideal-divider values are only what
  // a unit that was never calibrated runs on, and WILL be off on real hardware.
  // How the travel is mapped (deadzones, axis wiring) is under "AXIS WIRING"
  // in stick_io.hpp.
  static constexpr JoystickAxisCal kIdealAxis{
      .min_mv = 0.0f, .center_mv = 1650.0f, .max_mv = 3300.0f};
  const JoystickCal joystick_cal = joystick_cal_load({kIdealAxis, kIdealAxis, kIdealAxis});
  state.monitor.use_calibration(stick_cal_mv(joystick_cal)); // before the task starts
  return joystick_cal;
}
