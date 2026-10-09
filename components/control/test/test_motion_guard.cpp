// L1-CTL: the motion guard (hazard fix C4, docs/plans/hazard-c4-spec.md §2, §3, §7).
// Expected values come from the spec's tables, never from the code under test. Fake time only.

#include <array>
#include <atomic>
#include <cstdint>

#include "control/motion_guard.hpp"
#include "test_case.hpp"

namespace {

using hmi::control::age_ms;
using hmi::control::GuardFlags;
using hmi::control::GuardInputs;
using hmi::control::GuardReason;
using hmi::control::GuardSources;
using hmi::control::GuardTelemetry;
using hmi::control::kGuardReasonCount;
using hmi::control::MotionGuard;

constexpr std::uint8_t kInitializing = static_cast<std::uint8_t>(MIB::MibSystemState::INITIALIZING);
constexpr std::uint8_t kIdle = static_cast<std::uint8_t>(MIB::MibSystemState::IDLE);
constexpr std::uint8_t kEnabled = static_cast<std::uint8_t>(MIB::MibSystemState::ENABLED);
constexpr std::uint8_t kError = static_cast<std::uint8_t>(MIB::MibSystemState::ERROR);

// A time well away from 0 and from the wrap, so "fresh" stamps are plainly written.
constexpr std::uint32_t kT0 = 100'000;

// Every condition fresh at now_ms: both WDT flags, the heartbeat 5 ms old, the link up, the
// newest MibStatus 100 ms old and ENABLED.
GuardInputs fresh(std::uint32_t now_ms) {
  return GuardInputs{.adc_wdt_ok = true,
                     .ui_wdt_ok = true,
                     .ui_heartbeat_ms = now_ms - 5,
                     .net_failed = false,
                     .link_up = true,
                     .got_ip = true,
                     .mcb_rx_ms = now_ms - 100,
                     .mcb_state = kEnabled,
                     .stick_drives = true};
}

void expect_reason(GuardReason want, GuardReason got) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(want), static_cast<std::uint8_t>(got));
}

} // namespace

TEST_CASE("CTL-001 all fresh, carrier up, ENABLED, both WDT flags: the verdict is OK",
          "[control][REQ-CTL-03][REQ-CTL-04][REQ-CTL-05][REQ-CTL-06]") {
  MotionGuard g;
  expect_reason(GuardReason::OK, g.evaluate(fresh(kT0), kT0));
  expect_reason(GuardReason::OK, g.reason());
}

TEST_CASE("CTL-002 heartbeat age 0, 199, 200, 5000 ms: OK, OK, UI_STALE, UI_STALE",
          "[control][REQ-CTL-03]") {
  MotionGuard g;
  const std::uint32_t stamp = kT0;
  const std::array<std::uint32_t, 4> ages{0, 199, 200, 5000};
  const std::array<GuardReason, 4> want{GuardReason::OK, GuardReason::OK, GuardReason::UI_STALE,
                                        GuardReason::UI_STALE};
  for (std::size_t i = 0; i < ages.size(); ++i) {
    const std::uint32_t now = stamp + ages[i];
    GuardInputs in = fresh(now);
    in.ui_heartbeat_ms = stamp;
    expect_reason(want[i], g.evaluate(in, now));
  }
}

TEST_CASE("CTL-003 MibStatus age 0, 1999, 2000 ms: OK, OK, MIB_STALE", "[control][REQ-CTL-04]") {
  MotionGuard g;
  const std::uint32_t stamp = kT0;
  const std::array<std::uint32_t, 3> ages{0, 1999, 2000};
  const std::array<GuardReason, 3> want{GuardReason::OK, GuardReason::OK, GuardReason::MIB_STALE};
  for (std::size_t i = 0; i < ages.size(); ++i) {
    const std::uint32_t now = stamp + ages[i];
    GuardInputs in = fresh(now);
    in.mcb_rx_ms = stamp;
    expect_reason(want[i], g.evaluate(in, now));
  }
}

TEST_CASE("CTL-004 nothing written since boot: UI_STALE; then a heartbeat only: MIB_STALE",
          "[control][REQ-CTL-03][REQ-CTL-04]") {
  // What a reader loads before any writer ran: every stamp and the state at their initial
  // values (GuardSources' own), the link up and both WDT flags true so only the stamps decide.
  GuardSources sources;
  MotionGuard g;
  const std::uint32_t now = 5000;
  GuardInputs in = fresh(now);
  in.ui_heartbeat_ms = sources.ui_heartbeat.load();
  in.mcb_rx_ms = sources.mcb_rx.load();
  in.mcb_state = sources.mcb_state.load();
  expect_reason(GuardReason::UI_STALE, g.evaluate(in, now));
  sources.note_ui_cycle(now + 1);
  in.ui_heartbeat_ms = sources.ui_heartbeat.load();
  expect_reason(GuardReason::MIB_STALE, g.evaluate(in, now + 1));
}

TEST_CASE("CTL-005 fresh stamps with net failed, no link, or no IP: LINK_DOWN each",
          "[control][REQ-CTL-05]") {
  for (int which = 0; which < 3; ++which) {
    MotionGuard g;
    GuardInputs in = fresh(kT0);
    in.net_failed = which == 0;
    in.link_up = which != 1;
    in.got_ip = which != 2;
    expect_reason(GuardReason::LINK_DOWN, g.evaluate(in, kT0));
  }
}

TEST_CASE("CTL-006 MCB state INITIALIZING, IDLE, ERROR, 4, 255: MCB_NOT_ENABLED each; ENABLED: OK",
          "[control][REQ-CTL-06]") {
  const std::array<std::uint8_t, 5> not_enabled{kInitializing, kIdle, kError, 4, 255};
  for (const std::uint8_t state : not_enabled) {
    MotionGuard g;
    GuardInputs in = fresh(kT0);
    in.mcb_state = state;
    expect_reason(GuardReason::MCB_NOT_ENABLED, g.evaluate(in, kT0));
  }
  MotionGuard g;
  GuardInputs in = fresh(kT0);
  in.mcb_state = kEnabled;
  expect_reason(GuardReason::OK, g.evaluate(in, kT0));
}

TEST_CASE("CTL-007 all 32 subsets of e, a, b, c, d: the first failing in order; OK only for none",
          "[control][REQ-CTL-10]") {
  // Bit i of the subset = condition i of C4 §2.2's order e, a, b, c, d.
  const std::array<GuardReason, 5> order{GuardReason::WDT_MISSING, GuardReason::UI_STALE,
                                         GuardReason::LINK_DOWN, GuardReason::MIB_STALE,
                                         GuardReason::MCB_NOT_ENABLED};
  for (unsigned subset = 0; subset < 32; ++subset) {
    MotionGuard g;
    GuardInputs in = fresh(kT0);
    in.adc_wdt_ok = (subset & 1U) == 0;
    in.ui_heartbeat_ms = (subset & 2U) != 0 ? kT0 - 200 : kT0 - 5;
    in.link_up = (subset & 4U) == 0;
    in.mcb_rx_ms = (subset & 8U) != 0 ? kT0 - 2000 : kT0 - 100;
    in.mcb_state = (subset & 16U) != 0 ? kIdle : kEnabled;
    GuardReason want = GuardReason::OK;
    for (std::size_t i = 0; i < order.size(); ++i) {
      if ((subset & (1U << i)) != 0) {
        want = order[i];
        break;
      }
    }
    expect_reason(want, g.evaluate(in, kT0));
  }
}

TEST_CASE("CTL-008 stamp 2^32 - 50 with the clock wrapped to 100: age 150, OK",
          "[control][REQ-CTL-07]") {
  const std::uint32_t stamp = 0xFFFF'FFFFU - 49U; // 2^32 - 50
  const std::uint32_t now = 100;
  TEST_ASSERT_EQUAL_UINT32(150, age_ms(now, stamp));
  MotionGuard g;
  GuardInputs in = fresh(now);
  in.ui_heartbeat_ms = stamp;
  in.mcb_rx_ms = stamp;
  expect_reason(GuardReason::OK, g.evaluate(in, now));
}

TEST_CASE("CTL-009 seen stale at age 2000, then 2^32 ms later with the same stamp: still "
          "MIB_STALE; then a new stamp: OK",
          "[control][REQ-CTL-07]") {
  MotionGuard g;
  const std::uint32_t stamp = kT0;
  std::uint32_t now = stamp + 2000;
  GuardInputs in = fresh(now);
  in.mcb_rx_ms = stamp;
  expect_reason(GuardReason::MIB_STALE, g.evaluate(in, now));
  // 2^32 ms later: the same uint32 clock value, the same stamp.
  now = static_cast<std::uint32_t>(std::uint64_t{stamp} + 2000U + (std::uint64_t{1} << 32U));
  in = fresh(now);
  in.mcb_rx_ms = stamp;
  expect_reason(GuardReason::MIB_STALE, g.evaluate(in, now));
  // And where the modular age would alias to fresh (100 ms), the latch still holds.
  now = stamp + 100;
  in = fresh(now);
  in.mcb_rx_ms = stamp;
  expect_reason(GuardReason::MIB_STALE, g.evaluate(in, now));
  // A new stamp ends it.
  in = fresh(now);
  in.mcb_rx_ms = now - 1;
  expect_reason(GuardReason::OK, g.evaluate(in, now));
}

TEST_CASE("CTL-010 a stamp 1 ms ahead of the clock is stale", "[control][REQ-CTL-07]") {
  {
    MotionGuard g;
    GuardInputs in = fresh(kT0);
    in.ui_heartbeat_ms = kT0 + 1;
    expect_reason(GuardReason::UI_STALE, g.evaluate(in, kT0));
  }
  {
    MotionGuard g;
    GuardInputs in = fresh(kT0);
    in.mcb_rx_ms = kT0 + 1;
    expect_reason(GuardReason::MIB_STALE, g.evaluate(in, kT0));
  }
}

TEST_CASE("CTL-011 adc_wdt_ok false; ui_wdt_ok false (all else fresh): WDT_MISSING each",
          "[control][REQ-CTL-08]") {
  {
    MotionGuard g;
    GuardInputs in = fresh(kT0);
    in.adc_wdt_ok = false;
    expect_reason(GuardReason::WDT_MISSING, g.evaluate(in, kT0));
  }
  {
    MotionGuard g;
    GuardInputs in = fresh(kT0);
    in.ui_wdt_ok = false;
    expect_reason(GuardReason::WDT_MISSING, g.evaluate(in, kT0));
  }
}

TEST_CASE("CTL-017 two UI stalls, one with stick_drives false: trips[UI_STALE] 2, "
          "ui_stalls_drive 1, ui_age_max_ms the longest age",
          "[control][REQ-CTL-10]") {
  MotionGuard g;
  GuardTelemetry telemetry;
  // Each step: (now, heartbeat stamp, stick_drives). Stall 1: age 250 while driving. Stall 2:
  // age 500 with the gate shut. The longest age seen is 500.
  struct Step {
    std::uint32_t now;
    std::uint32_t heartbeat;
    bool drives;
    GuardReason want;
  };
  const std::array<Step, 7> steps{{
      {1000, 995, true, GuardReason::OK},
      {1100, 995, true, GuardReason::OK},         // age 105
      {1245, 995, true, GuardReason::UI_STALE},   // age 250: onset 1, driving
      {1300, 995, true, GuardReason::UI_STALE},   // still the same stall (age 305): no new onset
      {1310, 1305, true, GuardReason::OK},        // heartbeat back
      {1805, 1305, false, GuardReason::UI_STALE}, // age 500: onset 2, gate shut
      {1910, 1905, false, GuardReason::OK},
  }};
  for (const Step &s : steps) {
    GuardInputs in = fresh(s.now);
    in.ui_heartbeat_ms = s.heartbeat;
    in.stick_drives = s.drives;
    expect_reason(s.want, g.evaluate(in, s.now));
    g.publish(telemetry);
  }
  TEST_ASSERT_EQUAL_UINT32(2, g.trips(GuardReason::UI_STALE));
  TEST_ASSERT_EQUAL_UINT32(1, g.ui_stalls_drive());
  TEST_ASSERT_EQUAL_UINT32(500, g.ui_age_max_ms());
  for (const GuardReason r : {GuardReason::OK, GuardReason::WDT_MISSING, GuardReason::LINK_DOWN,
                              GuardReason::MIB_STALE, GuardReason::MCB_NOT_ENABLED}) {
    TEST_ASSERT_EQUAL_UINT32(0, g.trips(r));
  }
  // What the UI and the self test read: the same values, from the ADC task's atomics.
  TEST_ASSERT_EQUAL_UINT32(2,
                           telemetry.trips[static_cast<std::size_t>(GuardReason::UI_STALE)].load());
  TEST_ASSERT_EQUAL_UINT32(1, telemetry.ui_stalls_drive.load());
  TEST_ASSERT_EQUAL_UINT32(500, telemetry.ui_age_max_ms.load());
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::OK), telemetry.reason.load());
}

// Not a spec case: the remaining counter rules of REQ-CTL-10 at their edges, so every branch of
// the guard is taken (100 % branch coverage of motion_guard.hpp, C4 §7).
TEST_CASE("CTL-025 onsets: a source stale from boot is no trip until fresh once; each condition "
          "counts its own onsets, whichever is reported; counters never go down",
          "[control][REQ-CTL-10]") {
  MotionGuard g;
  GuardTelemetry telemetry;
  expect_reason(GuardReason::WDT_MISSING, g.reason()); // never OK before the first cycle
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::WDT_MISSING),
                          telemetry.reason.load());
  // Boot: nothing written, the link down, the MCB silent, a WDT flag false.
  GuardInputs boot{};
  expect_reason(GuardReason::WDT_MISSING, g.evaluate(boot, 50));
  TEST_ASSERT_EQUAL_UINT32(0, g.ui_age_max_ms()); // no heartbeat yet: no age
  for (std::size_t i = 0; i < kGuardReasonCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(0, g.trips(static_cast<GuardReason>(i)));
  }
  // Everything fresh, then everything fails at once: one onset each, WDT_MISSING reported.
  expect_reason(GuardReason::OK, g.evaluate(fresh(kT0), kT0));
  GuardInputs all_bad = fresh(kT0 + 2000);
  all_bad.ui_wdt_ok = false;
  all_bad.ui_heartbeat_ms = kT0 - 5;
  all_bad.got_ip = false;
  all_bad.mcb_rx_ms = kT0 - 100;
  all_bad.mcb_state = kError;
  expect_reason(GuardReason::WDT_MISSING, g.evaluate(all_bad, kT0 + 2000));
  expect_reason(GuardReason::WDT_MISSING, g.evaluate(all_bad, kT0 + 2035)); // held: no new onset
  for (const GuardReason r :
       {GuardReason::WDT_MISSING, GuardReason::UI_STALE, GuardReason::LINK_DOWN,
        GuardReason::MIB_STALE, GuardReason::MCB_NOT_ENABLED}) {
    TEST_ASSERT_EQUAL_UINT32(1, g.trips(r));
  }
  TEST_ASSERT_EQUAL_UINT32(1, g.ui_stalls_drive());
  TEST_ASSERT_EQUAL_UINT32(2040, g.ui_age_max_ms());
  // A shorter age later leaves the maximum where it was.
  expect_reason(GuardReason::OK, g.evaluate(fresh(kT0 + 3000), kT0 + 3000));
  TEST_ASSERT_EQUAL_UINT32(2040, g.ui_age_max_ms());
  g.publish(telemetry);
  TEST_ASSERT_EQUAL_UINT32(
      1, telemetry.trips[static_cast<std::size_t>(GuardReason::LINK_DOWN)].load());
}

TEST_CASE("CTL-026 the ordered load: stamps, then the MCB state, then the clock; the writers "
          "store the state before the stamp",
          "[control][REQ-CTL-04][REQ-CTL-11]") {
  GuardSources sources;
  std::atomic<bool> net_failed{false};
  std::atomic<bool> link_up{true};
  std::atomic<bool> got_ip{true};
  std::atomic<bool> drives{true};
  const GuardFlags flags{net_failed, link_up, got_ip, drives};
  sources.note_ui_wdt(true);
  sources.note_ui_cycle(kT0 - 3);
  sources.note_mib_status(kEnabled, kT0 - 40);
  // The clock is read last: by the time it runs, both stamps were already loaded.
  unsigned clock_calls = 0;
  auto clock = [&clock_calls] {
    ++clock_calls;
    return kT0;
  };
  std::uint32_t now = 0;
  const GuardInputs in = hmi::control::load_guard_inputs(sources, flags, true, clock, now);
  TEST_ASSERT_EQUAL_UINT(1, clock_calls);
  TEST_ASSERT_EQUAL_UINT32(kT0, now);
  TEST_ASSERT_EQUAL_UINT32(kT0 - 3, in.ui_heartbeat_ms);
  TEST_ASSERT_EQUAL_UINT32(kT0 - 40, in.mcb_rx_ms);
  TEST_ASSERT_EQUAL_UINT8(kEnabled, in.mcb_state);
  TEST_ASSERT_TRUE(in.adc_wdt_ok && in.ui_wdt_ok && in.link_up && in.got_ip && in.stick_drives);
  TEST_ASSERT_FALSE(in.net_failed);
  MotionGuard g;
  expect_reason(GuardReason::OK, g.evaluate(in, now));
}
