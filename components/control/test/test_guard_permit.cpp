// L1-CTL: the motion guard as condition 2 of C1's output permit (hazard-c4-spec.md §2.1, §7:
// CTL-012, 013, 015, 016, 020, 021). A deterministic simulation of both tasks on fake time: the
// UI writes its heartbeat every 8 ms, the MCB a MibStatus every 500 ms, the ADC task cycles
// every 35 ms through ControlCycle, the real StickPipeline and the real OutputPermit, the way
// main's AdcStickIo wires them (the verdict is the permit's motion_guard_ok). Expected values
// come from the spec.

#include <atomic>
#include <cstdint>
#include <optional>

#include "control/cycle.hpp"
#include "stick/output_permit.hpp"
#include "stick/stick_pipeline.hpp"
#include "test_case.hpp"

namespace {

using hmi::control::ControlCycle;
using hmi::control::GuardFlags;
using hmi::control::GuardReason;
using hmi::control::GuardSources;
using hmi::control::GuardTelemetry;
using hmi::stick::HoldReason;
using hmi::stick::RawReadsMv;
using hmi::stick::StickPipeline;

constexpr std::uint8_t kEnabled = static_cast<std::uint8_t>(MIB::MibSystemState::ENABLED);
constexpr std::uint8_t kIdle = static_cast<std::uint8_t>(MIB::MibSystemState::IDLE);
constexpr std::uint32_t kAdcPeriodMs = 35;
constexpr std::uint32_t kUiPeriodMs = 8;
constexpr std::uint32_t kMibPeriodMs = 500;

constexpr hmi::stick::CalibrationMv kBoard2{.horizontal = {11.0f, 1507.0f, 2971.0f},
                                            .vertical = {6.0f, 1510.0f, 2962.0f},
                                            .twist = {10.0f, 1477.0f, 2960.0f}};
// Full forward: the vertical pot at its calibrated min; centred: every pot at its centre.
constexpr RawReadsMv kForward{.horizontal_mv = 1507.0f, .vertical_mv = 6.0f, .twist_mv = 1477.0f};
constexpr RawReadsMv kCentre{.horizontal_mv = 1507.0f, .vertical_mv = 1510.0f, .twist_mv = 1477.0f};

struct NoWatchdog {
  bool subscribe() { return true; }
  void reset() {}
};

// main's AdcStickIo, reduced to what the permit needs: every other permit condition holds
// (gate open, no calibration, measured, POST PASS, health OK), as CTL-012 sets up.
struct PermitIo {
  hmi::stick::OutputPermit *permit;
  const std::uint32_t *now_ms;
  GuardReason verdict = GuardReason::WDT_MISSING;
  std::optional<hmi::stick::Command> last{};
  HoldReason reason = HoldReason::GATE_SHUT;

  void set_motion_verdict(GuardReason v) { verdict = v; }
  std::optional<hmi::stick::CalibrationMv> take_new_calibration() { return std::nullopt; }
  float smooth_twist_mv(float twist_mv) { return twist_mv; }
  void note_raw_mv(float, float, float) {}
  bool calibrating() { return false; }
  bool swap() { return false; }
  bool invert_x() { return false; }
  bool invert_y() { return false; }
  int sensitivity() { return 5; }
  std::uint32_t joy_key() { return 0; }
  std::uint32_t remote_key() { return 0; }
  void set_joy_key(std::uint32_t) {}
  void set_joy_flick(std::uint32_t) {}
  void show(const hmi::stick::Position &) {}
  int drive_speed() { return 10; }
  hmi::stick::Permit output_permit(const hmi::stick::Position &mounted) {
    const hmi::stick::PermitInputs in{.gate_open = true,
                                      .motion_guard_ok = verdict == GuardReason::OK,
                                      .calibrating = false,
                                      .calibration_measured = true,
                                      .post = hmi::stick::PostGate::PASS,
                                      .health = hmi::stick::StickHealth::OK};
    const hmi::stick::Permit p = permit->cycle(in, mounted, *now_ms);
    reason = p.reason;
    return p;
  }
  bool button_pressed() { return false; }
  bool publish(const hmi::stick::Command &c, bool) {
    last = c;
    return true;
  }
  void note_cycle(bool, float, float, float, bool) {}
};

struct Clock {
  const std::uint32_t *now_ms;
  std::uint32_t operator()() const { return *now_ms; }
};

// Both tasks and the MCB on one fake clock.
struct Sim {
  GuardSources sources;
  std::atomic<bool> net_failed{false};
  std::atomic<bool> link_up{true};
  std::atomic<bool> got_ip{true};
  std::atomic<bool> stick_drives{true};
  GuardTelemetry telemetry;
  std::uint32_t now_ms = 20'000;
  bool ui_alive = true;
  std::uint8_t mcb_state = kEnabled;
  NoWatchdog wdt;
  hmi::stick::OutputPermit permit;
  StickPipeline stick{hmi::stick::pipeline_config(kBoard2, {1, 2, 3, 4})};
  PermitIo io{.permit = &permit, .now_ms = &now_ms};
  ControlCycle<NoWatchdog, Clock> cycle{wdt, Clock{&now_ms}, sources,
                                        GuardFlags{net_failed, link_up, got_ip, stick_drives},
                                        telemetry};

  Sim() { sources.note_ui_wdt(true); }

  // The writers up to now_ms: the UI's last cycle, the MCB's last MibStatus.
  void writers() {
    if (ui_alive) {
      sources.note_ui_cycle(now_ms - now_ms % kUiPeriodMs);
    }
    sources.note_mib_status(mcb_state, now_ms - now_ms % kMibPeriodMs);
  }
  // One ADC cycle at @p t on @p raw.
  void adc_at(std::uint32_t t, const RawReadsMv &raw) {
    now_ms = t;
    writers();
    static_cast<void>(cycle.run(stick, io, raw));
  }
  // ADC cycles every 35 ms over [from, to).
  void run(std::uint32_t from, std::uint32_t to, const RawReadsMv &raw) {
    for (std::uint32_t t = from; t < to; t += kAdcPeriodMs) {
      adc_at(t, raw);
    }
  }
  // Neutral 400 ms (the latch), then forward 200 ms: driving full forward from here.
  std::uint32_t start_driving() {
    run(now_ms, now_ms + 400, kCentre);
    const std::uint32_t t = now_ms + kAdcPeriodMs;
    run(t, t + 200, kForward);
    return now_ms + kAdcPeriodMs;
  }
  [[nodiscard]] bool forward_out() const {
    return io.last && io.last->x == 0.0f && io.last->y == 1.0f && io.last->twist == 0.0f;
  }
  [[nodiscard]] bool zero_out() const {
    return io.last && io.last->x == 0.0f && io.last->y == 0.0f && io.last->twist == 0.0f;
  }
};

} // namespace

TEST_CASE("CTL-012 UI stall: the stick drives before; every cycle from t0 + 200 ms publishes "
          "0, the first 0 by t0 + 240 ms; XYTwist keeps flowing",
          "[control][REQ-CTL-02][REQ-CTL-03]") {
  Sim s;
  std::uint32_t t = s.start_driving();
  TEST_ASSERT_TRUE(s.forward_out()); // full forward x speed 10/10
  const std::uint32_t t0 = t + 3;    // the UI stops here (its last cycle at or before t0)
  s.adc_at(t, kForward);
  TEST_ASSERT_TRUE(s.forward_out());
  s.ui_alive = false;
  std::optional<std::uint32_t> first_zero;
  for (t += kAdcPeriodMs; t < t0 + 1000; t += kAdcPeriodMs) {
    s.io.last.reset();
    s.adc_at(t, kForward);
    TEST_ASSERT_TRUE(s.io.last.has_value()); // published every cycle (neutral, not silence)
    if (s.zero_out() && !first_zero) {
      first_zero = t;
    }
    if (t >= t0 + 200) {
      TEST_ASSERT_TRUE(s.zero_out());
      TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(HoldReason::MOTION_GUARD),
                              static_cast<std::uint8_t>(s.io.reason));
    }
  }
  TEST_ASSERT_TRUE(first_zero.has_value());
  TEST_ASSERT_TRUE(*first_zero <= t0 + 240);
}

TEST_CASE("CTL-013 the UI resumes at t1 with the stick held forward: output stays 0; it returns "
          "only after the stick read neutral for kNeutralHold after t1",
          "[control][REQ-CTL-14]") {
  Sim s;
  std::uint32_t t = s.start_driving();
  s.ui_alive = false;
  s.run(t, t + 600, kForward);
  TEST_ASSERT_TRUE(s.zero_out());
  s.ui_alive = true; // t1
  t = s.now_ms + kAdcPeriodMs;
  s.run(t, t + 1000, kForward);
  TEST_ASSERT_TRUE(s.zero_out()); // still held: C1's latch was cleared by the trip
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(HoldReason::CENTRE_FIRST),
                          static_cast<std::uint8_t>(s.io.reason));
  t = s.now_ms + kAdcPeriodMs;
  s.run(t, t + 350, kCentre); // neutral >= 300 ms
  s.adc_at(s.now_ms + kAdcPeriodMs, kForward);
  TEST_ASSERT_TRUE(s.forward_out());
}

TEST_CASE("CTL-015 MCB -> IDLE mid-drive with stick_drives still true: the first cycle after the "
          "IDLE stamp publishes 0",
          "[control][REQ-CTL-02][REQ-CTL-06]") {
  Sim s;
  const std::uint32_t t = s.start_driving();
  s.adc_at(t, kForward);
  TEST_ASSERT_TRUE(s.forward_out());
  // The IDLE MibStatus arrives between two cycles; the gate (UI) has not ticked.
  s.mcb_state = kIdle;
  s.sources.note_mib_status(kIdle, t + 10);
  s.now_ms = t + kAdcPeriodMs;
  static_cast<void>(s.cycle.run(s.stick, s.io, kForward)); // no new writers: the stamp at t+10
  TEST_ASSERT_TRUE(s.stick_drives.load());
  TEST_ASSERT_TRUE(s.zero_out());
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::MCB_NOT_ENABLED),
                          static_cast<std::uint8_t>(s.io.verdict));
}

TEST_CASE("CTL-016 G4: MCB ENABLED and fresh, UI alive, stick_drives true, a DISABLE sent (exit): "
          "the output follows the stick",
          "[control][REQ-CTL-09]") {
  Sim s;
  std::uint32_t t = s.start_driving();
  // The HMI asked the MCB to stop (exit hold, DISABLE re-sent); the MCB stays ENABLED. The guard
  // reads none of that: the stick keeps driving.
  for (int i = 0; i < 100; ++i, t += kAdcPeriodMs) {
    s.adc_at(t, kForward);
    TEST_ASSERT_TRUE(s.forward_out());
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::OK),
                          static_cast<std::uint8_t>(s.io.verdict));
}

namespace {
// A trip (MCB IDLE), then the cause cleared (ENABLED again); returns the time of the first cycle
// after the clear.
std::uint32_t trip_and_clear(Sim &s) {
  std::uint32_t t = s.start_driving();
  s.mcb_state = kIdle;
  s.run(t, t + 600, kForward);
  TEST_ASSERT_TRUE(s.zero_out());
  s.mcb_state = kEnabled;
  t = s.now_ms + kMibPeriodMs; // the next MibStatus, ENABLED
  s.adc_at(t, kForward);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::OK),
                          static_cast<std::uint8_t>(s.io.verdict));
  TEST_ASSERT_TRUE(s.zero_out());
  return t + kAdcPeriodMs;
}
} // namespace

TEST_CASE("CTL-020 trip, cause cleared, then the stick neutral 299 ms vs 300 ms: 0 with hold "
          "reason CENTRE_FIRST; then the output follows the stick",
          "[control][REQ-CTL-14]") {
  Sim s;
  const std::uint32_t ts = trip_and_clear(s);
  s.adc_at(ts, kCentre); // the neutral wait starts here
  s.adc_at(ts + 299, kCentre);
  TEST_ASSERT_TRUE(s.zero_out());
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(HoldReason::CENTRE_FIRST),
                          static_cast<std::uint8_t>(s.io.reason));
  s.adc_at(ts + 300, kCentre);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(HoldReason::NONE),
                          static_cast<std::uint8_t>(s.io.reason));
  s.adc_at(ts + 335, kForward);
  TEST_ASSERT_TRUE(s.forward_out());
}

TEST_CASE("CTL-021 as CTL-020, the neutral broken at 200 ms, then 300 ms neutral: the 300 ms "
          "restarts",
          "[control][REQ-CTL-14]") {
  Sim s;
  const std::uint32_t ts = trip_and_clear(s);
  s.adc_at(ts, kCentre);
  s.adc_at(ts + 200, kForward); // broken
  TEST_ASSERT_TRUE(s.zero_out());
  const std::uint32_t ts2 = ts + 235;
  s.adc_at(ts2, kCentre);      // the wait restarts here
  s.adc_at(ts + 300, kCentre); // 300 ms after the first start: not enough
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(HoldReason::CENTRE_FIRST),
                          static_cast<std::uint8_t>(s.io.reason));
  s.adc_at(ts2 + 299, kCentre);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(HoldReason::CENTRE_FIRST),
                          static_cast<std::uint8_t>(s.io.reason));
  s.adc_at(ts2 + 300, kCentre);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(HoldReason::NONE),
                          static_cast<std::uint8_t>(s.io.reason));
  s.adc_at(ts2 + 335, kForward);
  TEST_ASSERT_TRUE(s.forward_out());
}
