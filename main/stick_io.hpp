#pragma once

/**
 * @file stick_io.hpp
 * @brief The stick task's wiring ("Read ADC", control's StickIsland): AdcStickIo, everything one
 *        stick cycle reads and writes outside the pipeline (the settings, the latches, the
 *        calibration, the permit's channels, the POST's rest window, XYTwist, the self test);
 *        StickPorts, its clock and task watchdog; the island's Config and the calibration it
 *        starts on. Composition: it joins the stick to main's UiApp, joystick_cal, net_link and
 *        the self test. Moved from main.cpp unchanged; main.cpp builds and starts the island.
 */

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "simple_lowpass_filter.hpp"

#include "app_instances.hpp"
#include "control/bench_stall.hpp"
#include "control/stick_island.hpp"
#include "hmi_ui/app_state.hpp"
#include "hmi_ui/post_stage.hpp"
#include "joystick_cal.hpp"
#include "post/rest_window.hpp"
#include "rtps_comms.hpp"
#include "selftest.hpp"
#include "settings.hpp"
#include "settings_applied.hpp"
#include "stick/monitor_cycle.hpp"
#include "stick/output_permit.hpp"
#include "stick_inject.hpp"

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

static_assert(hmi::stick::SENSITIVITY_MIN == SETTINGS_STICK_SENSITIVITY_MIN &&
                  hmi::stick::SENSITIVITY_MAX == SETTINGS_STICK_SENSITIVITY_MAX,
              "hmi::stick clamps the stick sensitivity to the Settings range");
static_assert(hmi::stick::DRIVE_SPEED_MIN == SETTINGS_DRIVE_SPEED_MIN &&
                  hmi::stick::DRIVE_SPEED_MAX == SETTINGS_DRIVE_SPEED_MAX,
              "hmi::stick clamps the drive speed to the Settings range");

/// @brief A joystick_cal record as the stick pipeline takes it (raw mV per axis).
/// @param cal The calibration record.
/// @return Its three axes, as hmi::stick's CalibrationMv.
inline hmi::stick::CalibrationMv stick_cal_mv(const JoystickCal &cal) {
  const auto axis = [](const JoystickAxisCal &a) {
    return hmi::stick::AxisCalMv{.min_mv = a.min_mv, .center_mv = a.center_mv, .max_mv = a.max_mv};
  };
  return {.horizontal = axis(cal[JOY_HORIZONTAL]),
          .vertical = axis(cal[JOY_VERTICAL]),
          .twist = axis(cal[JOY_TWIST])};
}

/// @brief The stick pipeline's Config on a calibration. How far each axis travels is measured
/// per unit (joystick_cal.cpp, the CALIBRATE button on the JoystickTest screen); the dead zones
/// are the stick's (hmi::stick::pipeline_config); the keys are LVGL's arrow keys.
/// @param cal The calibration in use.
/// @return The pipeline's Config.
inline hmi::stick::StickPipeline::Config stick_pipeline_config(const JoystickCal &cal) {
  return hmi::stick::pipeline_config(
      stick_cal_mv(cal),
      {.up = LV_KEY_UP, .down = LV_KEY_DOWN, .right = LV_KEY_RIGHT, .left = LV_KEY_LEFT});
}

/// @brief The ADC side's clock (hazard-fixes.md §10 item 22): one uint32 ms count, from this one
/// function, for the output permit, C4's motion guard and its writers (the UI heartbeat, the
/// MibStatus stamp), and C2's monitor. Durations on it are taken modulo 2^32. Any task.
/// @return Milliseconds since boot, truncated to 32 bits.
inline uint32_t adc_clock_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

/// @brief Puts the calling task on the task watchdog (hazard-c4-spec.md §4.3; phase 1: a trip
/// reports only), at the task's first cycle.
/// @return Whether esp_task_wdt_add accepted it.
inline bool task_watchdog_subscribe() { return esp_task_wdt_add(nullptr) == ESP_OK; }
/// @brief Resets the task watchdog for the calling task, at the end of every cycle. A reset's
/// error (the task was never subscribed) changes nothing: the guard already holds WDT_MISSING.
inline void task_watchdog_reset() { static_cast<void>(esp_task_wdt_reset()); }

/// The stick task's side of the board (StickIsland's Ports, hazard fix C4): its clock and the
/// task watchdog.
struct StickPorts {
  /// @brief The ADC side's clock (adc_clock_ms). @return ms since boot, 32 bits.
  static uint32_t now_ms() { return adc_clock_ms(); }
  /// @brief The task on the watchdog (task_watchdog_subscribe). @return Whether it is on.
  static bool watchdog_subscribe() { return task_watchdog_subscribe(); }
  /// @brief The watchdog reset (task_watchdog_reset), every cycle.
  static void watchdog_reset() { task_watchdog_reset(); }
  /// @brief After every cycle (after the watchdog reset): the bench's STALL ADC, a no-op in
  /// other builds. Out of line (noinline), as it was when main.cpp defined it: the cycle calls
  /// it.
  [[gnu::noinline]] static inline void after_cycle();
};
/// Bench inject builds add STALL CADC's request (C2 O14): the island suspends espp's
/// ContinuousAdc task for its time. A release build's Ports has none, so the island compiles no
/// suspend.
struct BenchStickPorts : StickPorts {
  /// @brief STALL CADC's pending request, taken (bench stall, C2 O14).
  /// @return How long to suspend the ContinuousAdc task, ms; 0 for none.
  static inline uint32_t cadc_stall_ms();
};
/// The island's Ports: BenchStickPorts in a stick-injection build, else StickPorts.
using StickPortsSlot = std::conditional_t<BENCH_STICK_INJECT, BenchStickPorts, StickPorts>;

/// The ADC task's side of StickPipeline::cycle. Runs on the ADC task only; every
/// member is what the stick block of adc_task_fn did inline before.
struct AdcStickIo {
  /// What the ADC task keeps across cycles beside the pipeline: app_main's, handed to the
  /// island at start.
  struct State {
    hmi::stick::OutputPermit permit; ///< the output permit and its neutral latch (C1 §3)
    hmi::post::RestWindow rest;      ///< the POST's rest window (C3 §2.2)
    hmi::fw::Writer<hmi::ui::RestWindowMailbox> windows; ///< to the POST runner (UI task)
    hmi::post::RestWindowMsg window{}; ///< the window being sent: here, not on the task's stack
    hmi::stick::PostGate post_seen = hmi::stick::PostGate::NOT_RUN; ///< the gate, last cycle
    /// C4's motion guard: this cycle's verdict (ControlCycle hands it over before the pipeline)
    hmi::control::GuardReason guard = hmi::control::GuardReason::WDT_MISSING;
    /// joystick_cal_running(), sampled once per cycle (note_sample) for every reader of it in
    /// that cycle (hazard fix C2, REQ-CTL-15)
    bool calibrating = false;
    /// C2's stick monitor (hazard-c2-spec.md §3): stepped once per cycle in note_sample, on the
    /// limits of the calibration in use (set at start and by take_new_calibration)
    hmi::stick::MonitorCycle monitor{};
  };

  espp::SimpleLowpassFilter &twist_lowpass; ///< the twist's lowpass, the island's
  State &state;                             ///< the task's state across cycles

  /// @brief The motion guard's verdict for this cycle (hazard-c4-spec.md §2.1): the output
  /// permit's condition 2. Every cycle, valid or not, before the pipeline.
  /// @param verdict The guard's reason; OK lets the permit's condition 2 pass.
  void set_motion_verdict(hmi::control::GuardReason verdict) { state.guard = verdict; }

  /// @brief Every cycle, valid or not, first: the raw sample (after the bench injection), the
  /// one `calibrating` of the cycle, and C2's monitor stepped on them (REQ-CTL-15): its stick
  /// health to the permit's channel, its telemetry to the UiApp. Defined below the struct.
  /// @param sample This cycle's raw sample.
  [[gnu::noinline]] inline void note_sample(const hmi::stick::RawSample &sample);

  /// @brief Every cycle, valid or not, before the pipeline: the reads it gets (after the bench
  /// injection), to the POST's rest window. An invalid cycle restarts the permit's neutral wait
  /// (REQ-STK-12).
  /// @param reads The three reads, a failed one empty.
  void note_reads(const hmi::stick::RawReadsMv &reads) {
    if (!(reads.horizontal_mv && reads.vertical_mv && reads.twist_mv)) {
      state.permit.invalid_cycle();
    }
    // A failed read goes over as NaN, which the rest window counts as a failed read: three
    // floats in registers, so the task's frame holds no copy of the reads.
    constexpr float FAILED = std::numeric_limits<float>::quiet_NaN();
    feed_rest_window(state, reads.horizontal_mv.value_or(FAILED),
                     reads.vertical_mv.value_or(FAILED), reads.twist_mv.value_or(FAILED),
                     joy_button_pressed.load());
  }
  /// @brief The POST's rest window (C3 §2.2, §2.4, REQ-POST-20): every cycle's reads, a window
  /// to the runner once full. Defined below the struct (the UiApp owns the POST gate).
  /// @param state The task's state (its rest window and the window's mailbox).
  /// @param horizontal_mv The horizontal read, NaN if it failed.
  /// @param vertical_mv The vertical read, NaN if it failed.
  /// @param twist_mv The twist read, NaN if it failed.
  /// @param button The stick button's level.
  [[gnu::noinline]] static inline void feed_rest_window(State &state, float horizontal_mv,
                                                        float vertical_mv, float twist_mv,
                                                        bool button);

  /// @brief A calibration run just finished: the pipeline switches to it between two
  /// samples, on the task that owns the stick; the monitor's limits follow it.
  /// @return The new calibration, or nothing if no run finished since the last call.
  std::optional<hmi::stick::CalibrationMv> take_new_calibration() {
    if (auto cal = joystick_cal_take_new()) {
      const hmi::stick::CalibrationMv mv = stick_cal_mv(*cal);
      state.monitor.use_calibration(mv); // the monitor's limits follow the calibration in use
      return mv;
    }
    return std::nullopt;
  }
  /// @brief The twist's lowpass, one step. @param twist_mv The averaged twist read.
  /// @return The smoothed twist, mV.
  float smooth_twist_mv(float twist_mv) { return twist_lowpass(twist_mv); }
  /// @brief This cycle's raw reads, for a calibration run (joystick_cal_note_raw).
  /// @param horizontal_mv The horizontal axis, mV.
  /// @param vertical_mv The vertical axis, mV.
  /// @param twist_mv The twist, mV.
  void note_raw_mv(float horizontal_mv, float vertical_mv, float twist_mv) {
    joystick_cal_note_raw(horizontal_mv, vertical_mv, twist_mv);
  }
  /// @brief The stick's keys are silent while a calibration run owns the stick (the user is
  /// being told to push it to every end in turn), and while C2's monitor is outside OK and
  /// RECOVERING (hazard-c2-spec.md §3.1, REQ-STK-20).
  /// @return Whether the keys are silent this cycle.
  bool keys_silent() { return state.calibrating || !state.monitor.keys_live(); }
  /// @brief Settings: swap X and Y. @return The applied setting.
  bool swap() { return applied_settings.stick_swap(); }
  /// @brief Settings: invert X. @return The applied setting.
  bool invert_x() { return applied_settings.stick_invert_x(); }
  /// @brief Settings: invert Y. @return The applied setting.
  bool invert_y() { return applied_settings.stick_invert_y(); }
  /// @brief Settings: the stick sensitivity. @return The applied setting.
  int sensitivity() { return applied_settings.stick_sensitivity(); }
  /// @brief The stick's keypad latch. @return The key latched, 0 for none.
  uint32_t joy_key() { return ::joy_key.load(); }
  /// @brief The remote UI's key override. @return The key, 0 for none.
  uint32_t remote_key() { return ::remote_key.load(); }
  /// @brief Latches the stick's key for the keypad read. @param key The LVGL key, 0 for none.
  void set_joy_key(uint32_t key) { ::joy_key.store(key); }
  /// @brief Latches a flick for the keypad read. @param key The LVGL key, 0 for none.
  void set_joy_flick(uint32_t key) { joy_flick.store(key); }
  /// @brief The Joystick screen's bars, if the LVGL lock is free this cycle. Defined in
  /// main.cpp, which owns the LVGL lock (and so the only place the stick task touches LVGL);
  /// inlined into the stick cycle there, where the StickIsland is built.
  /// @param mounted The calibrated, mounted stick position, -1..+1 per axis.
  inline void show(const hmi::stick::Position &mounted);
  /// @brief Settings: the drive speed. @return The applied setting.
  int drive_speed() { return applied_settings.drive_speed(); }
  /// @brief The output permit (hazard-c1-spec.md §3): the stick drives only with every condition
  /// met, in the hold-reason order, else the MCB gets a literal 0 and XYTwist keeps flowing.
  /// 1 the gate: the stick may drive only from Drive with the menu shut (`stick_drives`); a
  ///   push meant for the next row must never move the chair. The bars keep moving.
  /// 2 C4's motion guard (hazard-c4-spec.md §2): the UI heartbeat, the link, the MibStatus age
  ///   and the MCB state, judged by the ADC task itself this cycle (set_motion_verdict).
  /// 3 no calibration run (the user is pushing it to every end in turn); 4 a measured
  ///   calibration (G5); 5 POST passed (G3, C3); 6, 7 stick health OK (C2's monitor, stepped
  ///   this cycle in note_sample); 8 the stick centred for kNeutralHold since the last hold (G1).
  /// The first failing one goes to the Drive notice (hold_reason). Lock-free reads only.
  /// Defined below the struct (the UiApp owns the permit's channels).
  /// @param mounted The calibrated, mounted stick position.
  /// @return The permit: whether the stick drives, and the first failing condition.
  hmi::stick::Permit output_permit(const hmi::stick::Position &mounted);
  /// @brief An invalid cycle's button bit (H9 fixed, REQ-STK-17): released until POST passes
  /// (REQ-STK-15), as the permit gives it on a valid cycle. Defined below the struct.
  /// @return Whether the button bit may pass.
  inline bool button_passes();
  /// @brief The stick button's level. @return Whether it is pressed.
  bool button_pressed() { return joy_button_pressed.load(); }
  /// @brief XYTwist to the MCB: the same calibrated -1..+1 values the bars show (+Y forward,
  /// deadzones applied), scaled by Settings "Speed sensitivity" (0 when held), so it needs
  /// no calibration of its own. Quiet no-op until RTPS is up and a subscriber is discovered.
  /// @param command The command (0 when held).
  /// @param button The button bit.
  /// @return Whether it was published.
  bool publish(const hmi::stick::Command &command, bool button) {
    return rtps_comms_publish_adc(command.x, command.y, command.twist,
                                  button ? rammp::Buttons::JOYSTICK : rammp::Buttons::NONE);
  }
  /// @brief After every cycle, valid or not: the self test measures the loop's cadence and
  /// how often a read fails, as well as the values. A no-op unless a run is capturing.
  /// @param valid Whether every read of the cycle succeeded.
  /// @param horizontal_mv The horizontal read, mV.
  /// @param vertical_mv The vertical read, mV.
  /// @param twist_mv The twist read, mV.
  /// @param published Whether XYTwist was published.
  void note_cycle(bool valid, float horizontal_mv, float vertical_mv, float twist_mv,
                  bool published) {
    selftest_note_adc(valid, horizontal_mv, vertical_mv, twist_mv, published,
                      joy_button_pressed.load());
  }
};

inline bool AdcStickIo::button_passes() {
  return hmi::stick::post_passed(ui_app.permit_hooks().post_gate.read());
}

// AdcStickIo's output permit (its comment above): ADC task, lock-free reads of the UiApp's
// permit channels only.
inline hmi::stick::Permit AdcStickIo::output_permit(const hmi::stick::Position &mounted) {
  hmi::stick::PermitHooks &hooks = ui_app.permit_hooks();
  const hmi::stick::PermitInputs in{
      .gate_open = ::stick_drives.load(),
      .motion_guard_ok = state.guard == hmi::control::GuardReason::OK,
      .calibrating = state.calibrating,
      .calibration_measured = joystick_cal_measured(),
      .post = hooks.post_gate.read(),
      .health = hooks.stick_health.read(),
  };
  const hmi::stick::Permit permit = state.permit.cycle(in, mounted, adc_clock_ms());
  hooks.hold_reason.write(permit.reason);
  return permit;
}

// The stick cycle's out-of-line steps are defined here, in the header, on purpose: the TU that
// builds the StickIsland (main.cpp) then has their bodies, as it did before they left main.cpp,
// so GCC compiles the cycle exactly as before (its interprocedural register allocation sees
// them; with the bodies in another TU the cycle's register use and frame changed).

// AdcStickIo's monitor step (its comment above, hazard-c2-spec.md §3.2, §3.7): ADC task, every
// cycle, before the pipeline. The stick health goes to the permit's channel (release), so the
// permit reads this cycle's value; the telemetry to the UiApp (relaxed, diagnostics).
void AdcStickIo::note_sample(const hmi::stick::RawSample &sample) {
  state.calibrating = joystick_cal_running();
  const hmi::stick::StickHealth health =
      state.monitor.step(state.calibrating, sample, joystick_cal_generation(), adc_clock_ms(),
                         ui_app.stick_telemetry());
  ui_app.permit_hooks().stick_health.write(health);
}

// The POST's rest window on the ADC task (hazard-c3-spec.md §2.2, §2.4, REQ-POST-20): every
// cycle, valid or not, the reads the pipeline gets (a failed one as NaN); a window to the runner
// once per WINDOW_MIN_SAMPLES cycles, never waiting (a one-slot mailbox). It stops once the gate is
// PASS or FAIL; a gate that leaves PASS or FAIL, or comes back to NOT_RUN (bench builds:
// POST RERUN), drops any partial window and waits again for a first all-valid cycle. Allocates
// nothing, logs nothing, no lock.
void AdcStickIo::feed_rest_window(State &state, float horizontal_mv, float vertical_mv,
                                  float twist_mv, bool button) {
  using hmi::stick::PostGate;
  const PostGate gate = ui_app.permit_hooks().post_gate.read();
  const bool rerun = hmi::post::rest_window_restarts(state.post_seen, gate);
  state.post_seen = gate;
  if (rerun) {
    state.rest.reset();
  }
  if (!hmi::post::rest_window_feeds(gate)) {
    return;
  }
  if (state.rest.add_into(horizontal_mv, vertical_mv, twist_mv, button, state.window.window)) {
    state.windows.write(state.window);
  }
}

// Bench builds: STALL ADC, at the end of a stick cycle (after the watchdog reset): the task
// neither cycles nor resets its watchdog for the stall's length (C4 §8 B5j).
void StickPorts::after_cycle() {
  if constexpr (BENCH_STICK_INJECT) {
    hmi::control::spin_for_ms(ui_app.bench_stall().take(hmi::control::StallTask::ADC),
                              adc_clock_ms);
  }
}

uint32_t BenchStickPorts::cadc_stall_ms() {
  return ui_app.bench_stall().take(hmi::control::StallTask::CADC);
}

/// The control island's cadence and twist channel: template constants of the island, so they
/// stay constants in the task's code. 30 Hz; the twist on ADC2_CH3, 8 oneshot reads a cycle.
// X/Y: ADC1_CH0/CH1 = GPIO16/GPIO17 on the M5-Bus header.
// Twist: ADC2_CH3 = GPIO52 on the M5-Bus (the W5500 INT moved to GPIO4 to
// free it — analog inputs can't be re-routed through the GPIO matrix).
// The twist channel is sampled oneshot rather than through the continuous
// driver: mixing both units via ADC_CONV_BOTH_UNIT produced a stream of
// invalid DMA frames on the P4 (log spam that starved LVGL's first frame).
inline constexpr hmi::control::Cycle kStickCycle{
    // 30 Hz: ADC read, LVGL bars, RTPS publish
    .period_ms = 33,
    .twist_channel = {.unit = ADC_UNIT_2, .channel = ADC_CHANNEL_3, .attenuation = ADC_ATTEN_DB_12},
    // Twist is the noisy axis (~50 mV peak-to-peak at rest, where X/Y read ~1
    // mV): each cycle averages this many oneshot reads of it, which costs no
    // lag, then lowpasses the result to iron out what is left. 80 ms is short
    // enough that the chair does not feel late to turn.
    .twist_oversample = 8,
};

/// The control island ("Read ADC") as main builds it: the stick slot (the pipeline, or with
/// CONFIG_HMI_BENCH_STICK_INJECT the bench injection in front of it), this Io and Ports.
using MainStickIsland =
    hmi::control::StickIsland<StickSlot, AdcStickIo, kStickCycle, StickPortsSlot>;

/// @brief The island's Config: the "Read ADC" task (priority 21, core 0, 6144 B), the continuous
/// ADC for X/Y, the twist's lowpass and the motion guard's channels (the UiApp's sources and
/// telemetry, net_link's link flags, the stick gate). app_main, once.
/// @return The Config for MainStickIsland's constructor.
MainStickIsland::Config stick_island_config();

/// @brief The calibration the stick starts on: the saved one if it passes its check, else the
/// ideal divider's (joystick_cal_load logs which); C2's monitor takes its limits before the task
/// starts. app_main, once, after the island's ADC drivers are up.
/// @param state The ADC task's state, whose monitor follows the calibration in use.
/// @return The calibration, for stick_pipeline_config.
JoystickCal stick_calibration_at_boot(AdcStickIo::State &state);
