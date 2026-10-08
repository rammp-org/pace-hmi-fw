// L1-CTL: the control island's per-cycle sequence (hazard fix C4, docs/plans/hazard-c4-spec.md
// §3.2, §4.3, §7): the motion guard, the real stick pipeline, note_cycle and the watchdog reset,
// with fakes for the Io, the watchdog and the clock. Fake time only (TS-DET-02).

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

#include "alloc_guard.hpp"
#include "control/cycle.hpp"
#include "stick/stick_pipeline.hpp"
#include "test_case.hpp"

namespace {

using hmi::control::ControlCycle;
using hmi::control::GuardFlags;
using hmi::control::GuardReason;
using hmi::control::GuardSources;
using hmi::control::GuardTelemetry;
using hmi::stick::RawReadsMv;
using hmi::stick::StickPipeline;

constexpr std::uint8_t kEnabled = static_cast<std::uint8_t>(MIB::MibSystemState::ENABLED);

// Board 2's calibration (post-limits-proposal.md; the stick goldens' CAL_BOARD2).
constexpr hmi::stick::CalibrationMv kBoard2{.horizontal = {11.0f, 1507.0f, 2971.0f},
                                            .vertical = {6.0f, 1510.0f, 2962.0f},
                                            .twist = {10.0f, 1477.0f, 2960.0f}};

// Full forward: the vertical pot at its calibrated min (it reads lower moving up).
constexpr RawReadsMv kForward{.horizontal_mv = 1507.0f, .vertical_mv = 6.0f, .twist_mv = 1477.0f};
constexpr RawReadsMv kInvalid{
    .horizontal_mv = 1507.0f, .vertical_mv = std::nullopt, .twist_mv = 1477.0f};

// The order of calls one cycle makes, one letter each: S subscribe, V the verdict handed to the
// Io, P publish, N note_cycle, R the watchdog reset. Fixed size (no allocation).
struct Trace {
  std::array<char, 16> text{};
  std::size_t len = 0;
  void add(char c) {
    if (len + 1 < text.size()) {
      text[len] = c;
      ++len;
    }
  }
  void clear() {
    text.fill('\0');
    len = 0;
  }
};

struct FakeWatchdog {
  Trace *trace;
  bool subscribe_result = true;
  unsigned subscribes = 0;
  unsigned resets = 0;
  bool subscribe() {
    trace->add('S');
    ++subscribes;
    return subscribe_result;
  }
  void reset() {
    trace->add('R');
    ++resets;
  }
};

// The ADC task's surroundings as the pipeline and the cycle see them.
struct FakeIo {
  Trace *trace;
  bool is_calibrating = false;
  bool drives = true;
  std::uint32_t key = 0;
  GuardReason verdict = GuardReason::OK;
  unsigned verdicts = 0;
  unsigned publishes = 0;
  hmi::stick::Command last{};
  bool noted_valid = false;
  bool noted_published = false;

  void set_motion_verdict(GuardReason v) {
    trace->add('V');
    verdict = v;
    ++verdicts;
  }
  std::optional<hmi::stick::CalibrationMv> take_new_calibration() { return std::nullopt; }
  float smooth_twist_mv(float twist_mv) { return twist_mv; }
  void note_raw_mv(float, float, float) {}
  bool calibrating() { return is_calibrating; }
  bool swap() { return false; }
  bool invert_x() { return false; }
  bool invert_y() { return false; }
  int sensitivity() { return 5; }
  std::uint32_t joy_key() { return key; }
  std::uint32_t remote_key() { return 0; }
  void set_joy_key(std::uint32_t k) { key = k; }
  void set_joy_flick(std::uint32_t) {}
  void show(const hmi::stick::Position &) {}
  int drive_speed() { return 10; }
  bool stick_drives() { return drives; }
  bool button_pressed() { return false; }
  bool publish(const hmi::stick::Command &c, bool) {
    trace->add('P');
    last = c;
    ++publishes;
    return true;
  }
  void note_cycle(bool valid, float, float, float, bool published) {
    trace->add('N');
    noted_valid = valid;
    noted_published = published;
  }
};

// The world around the ADC task: the UI and RTPS writers and the link, on a fake clock.
struct World {
  GuardSources sources;
  std::atomic<bool> net_failed{false};
  std::atomic<bool> link_up{true};
  std::atomic<bool> got_ip{true};
  std::atomic<bool> stick_drives{true};
  GuardTelemetry telemetry;
  std::uint32_t now_ms = 10'000;

  GuardFlags flags() { return GuardFlags{net_failed, link_up, got_ip, stick_drives}; }
  // Everything alive at now_ms: the UI just finished a cycle, a MibStatus ENABLED just came.
  void all_alive() {
    sources.note_ui_wdt(true);
    sources.note_ui_cycle(now_ms);
    sources.note_mib_status(kEnabled, now_ms);
  }
};

struct Clock {
  const World *world;
  std::uint32_t operator()() const { return world->now_ms; }
};

StickPipeline make_pipeline() {
  return StickPipeline(
      hmi::stick::pipeline_config(kBoard2, {.up = 1, .down = 2, .right = 3, .left = 4}));
}

} // namespace

TEST_CASE("CTL-014 a UI stall during invalid-read cycles and during a calibration run: the guard "
          "still advances (onset counted, stale latched)",
          "[control][REQ-CTL-01]") {
  Trace trace;
  World w;
  FakeWatchdog wdt{&trace};
  FakeIo io{&trace};
  StickPipeline stick = make_pipeline();
  ControlCycle cycle(wdt, Clock{&w}, w.sources, w.flags(), w.telemetry);
  w.all_alive();
  static_cast<void>(cycle.run(stick, io, kForward));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::OK),
                          static_cast<std::uint8_t>(io.verdict));
  // Invalid reads only, and the UI stops: 35 ms cycles, the heartbeat stays where it was; the
  // MibStatus keeps coming.
  const std::uint32_t last_heartbeat = w.now_ms;
  for (int i = 0; i < 10; ++i) {
    w.now_ms += 35;
    w.sources.note_mib_status(kEnabled, w.now_ms);
    TEST_ASSERT_FALSE(cycle.run(stick, io, kInvalid));
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::UI_STALE),
                          static_cast<std::uint8_t>(io.verdict));
  TEST_ASSERT_EQUAL_UINT32(1, cycle.guard().trips(GuardReason::UI_STALE));
  TEST_ASSERT_EQUAL_UINT32(350, cycle.guard().ui_age_max_ms());
  TEST_ASSERT_EQUAL_UINT(11, io.verdicts); // one verdict per cycle, invalid ones included
  TEST_ASSERT_EQUAL_UINT32(w.now_ms - last_heartbeat,
                           w.telemetry.ui_age_max_ms.load()); // published every cycle
  // The UI resumes, then stalls again during a calibration run (valid reads).
  w.sources.note_ui_cycle(w.now_ms);
  w.now_ms += 35;
  w.sources.note_ui_cycle(w.now_ms - 8);
  static_cast<void>(cycle.run(stick, io, kForward));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::OK),
                          static_cast<std::uint8_t>(io.verdict));
  io.is_calibrating = true;
  for (int i = 0; i < 10; ++i) {
    w.now_ms += 35;
    w.sources.note_mib_status(kEnabled, w.now_ms);
    static_cast<void>(cycle.run(stick, io, kForward));
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::UI_STALE),
                          static_cast<std::uint8_t>(io.verdict));
  TEST_ASSERT_EQUAL_UINT32(2, cycle.guard().trips(GuardReason::UI_STALE));
  TEST_ASSERT_EQUAL_UINT32(
      2, w.telemetry.trips[static_cast<std::size_t>(GuardReason::UI_STALE)].load());
}

TEST_CASE("CTL-018 1000 cycles of guard + pipeline + gate under the armed allocation guard: 0 "
          "allocations",
          "[control][REQ-CTL-11]") {
  Trace trace;
  World w;
  FakeWatchdog wdt{&trace};
  FakeIo io{&trace};
  StickPipeline stick = make_pipeline();
  ControlCycle cycle(wdt, Clock{&w}, w.sources, w.flags(), w.telemetry);
  unsigned published = 0;
  unsigned hits = 0;
  {
    host_test::NoAlloc scope;
    for (int i = 0; i < 1000; ++i) {
      trace.clear();
      w.now_ms += 35;
      // The UI writes every 8 ms, but stalls for a while every 200 cycles; the reads fail every
      // 7th cycle; the gate opens and shuts.
      if ((i / 20) % 10 != 9) {
        w.all_alive();
      }
      w.stick_drives.store((i / 50) % 2 == 0);
      io.drives = w.stick_drives.load();
      published += cycle.run(stick, io, i % 7 == 0 ? kInvalid : kForward) ? 1U : 0U;
    }
    hits = scope.count();
  }
  TEST_ASSERT_EQUAL_UINT(0, hits);
  TEST_ASSERT_EQUAL_UINT(1000 - 143, published); // every valid cycle publishes
  TEST_ASSERT_TRUE(cycle.guard().trips(GuardReason::UI_STALE) > 0);
}

TEST_CASE("CTL-019 per-cycle sequence with a fake watchdog: subscribe once, reset once per cycle "
          "after the publish, invalid reads included; a failing subscribe: WDT_MISSING forever",
          "[control][REQ-CTL-08][REQ-CTL-12]") {
  {
    Trace trace;
    World w;
    FakeWatchdog wdt{&trace};
    FakeIo io{&trace};
    StickPipeline stick = make_pipeline();
    ControlCycle cycle(wdt, Clock{&w}, w.sources, w.flags(), w.telemetry);
    TEST_ASSERT_FALSE(cycle.adc_wdt_ok());
    w.all_alive();
    TEST_ASSERT_TRUE(cycle.run(stick, io, kForward));
    TEST_ASSERT_EQUAL_STRING("SVPNR", trace.text.data()); // first cycle subscribes first
    TEST_ASSERT_TRUE(cycle.adc_wdt_ok());
    TEST_ASSERT_TRUE(io.noted_valid && io.noted_published);
    // Each read missing in turn: no publish, noted invalid, still reset.
    const std::array<RawReadsMv, 3> invalid{RawReadsMv{std::nullopt, 6.0f, 1477.0f}, kInvalid,
                                            RawReadsMv{1507.0f, 6.0f, std::nullopt}};
    for (const RawReadsMv &raw : invalid) {
      trace.clear();
      io.noted_valid = true;
      w.now_ms += 35;
      w.all_alive();
      TEST_ASSERT_FALSE(cycle.run(stick, io, raw));
      TEST_ASSERT_EQUAL_STRING("VNR", trace.text.data());
      TEST_ASSERT_FALSE(io.noted_valid || io.noted_published);
    }
    trace.clear();
    w.now_ms += 35;
    w.all_alive();
    TEST_ASSERT_TRUE(cycle.run(stick, io, kForward));
    TEST_ASSERT_EQUAL_STRING("VPNR", trace.text.data());
    TEST_ASSERT_EQUAL_UINT(1, wdt.subscribes);
    TEST_ASSERT_EQUAL_UINT(5, wdt.resets);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::OK),
                            static_cast<std::uint8_t>(io.verdict));
  }
  {
    Trace trace;
    World w;
    FakeWatchdog wdt{&trace, false};
    FakeIo io{&trace};
    StickPipeline stick = make_pipeline();
    ControlCycle cycle(wdt, Clock{&w}, w.sources, w.flags(), w.telemetry);
    for (int i = 0; i < 100; ++i) {
      trace.clear();
      w.now_ms += 35;
      w.all_alive();
      static_cast<void>(cycle.run(stick, io, kForward));
      TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(GuardReason::WDT_MISSING),
                              static_cast<std::uint8_t>(io.verdict));
    }
    TEST_ASSERT_EQUAL_UINT(1, wdt.subscribes);
    TEST_ASSERT_EQUAL_UINT(100, wdt.resets);
    TEST_ASSERT_FALSE(cycle.adc_wdt_ok());
  }
}
