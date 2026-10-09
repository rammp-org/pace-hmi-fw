// L1: the C3 wiring's pure parts (hazard-c3-spec.md §6.3): the ADC task's rest-window
// accumulator (POST-034..038), the POST runner on a fake port (POST-039..045, 047, 048), the
// persistent indicator (POST-046), the Overall -> PostGate mapping (POST-049) and the reset
// reason's names against the self test's (POST-051). Expected values are written from the spec.

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

#include "alloc_guard.hpp"
#include "fixtures.hpp"
#include "post/indicator.hpp"
#include "post/rest_window.hpp"
#include "post/runner.hpp"
#include "test_case.hpp"

namespace {

using hmi::post::AxisRest;
using hmi::post::Facts;
using hmi::post::Id;
using hmi::post::Indicator;
using hmi::post::IndicatorColour;
using hmi::post::IndicatorKind;
using hmi::post::Overall;
using hmi::post::PostGate;
using hmi::post::ResetReason;
using hmi::post::RestWindow;
using hmi::post::StickWindow;
using post_test::good_facts;

constexpr std::uint32_t N = hmi::post::WINDOW_MIN_SAMPLES;
static_assert(N == 30, "D4 approved 2026-10-08: the window is 30 cycles");

// ---------------------------------------------------------------- the accumulator

/// Feeds one cycle with all three reads at the given mV.
std::optional<StickWindow> feed(RestWindow &w, float h, float v, float t, bool button = false) {
  return w.add(h, v, t, button);
}

/// Feeds one cycle with the horizontal read failed.
std::optional<StickWindow> feed_invalid(RestWindow &w) {
  return w.add(std::nullopt, 1500.0f, 1500.0f, false);
}

void expect_axis(const AxisRest &a, std::int32_t mean, std::int32_t lo, std::int32_t hi,
                 std::uint32_t samples) {
  TEST_ASSERT_EQUAL_INT32(mean, a.mean_mv);
  TEST_ASSERT_EQUAL_INT32(lo, a.min_mv);
  TEST_ASSERT_EQUAL_INT32(hi, a.max_mv);
  TEST_ASSERT_EQUAL_UINT32(samples, a.samples);
}

// ---------------------------------------------------------------- the runner's fake port

/// What the board hands the runner, and what the runner did with it.
struct World {
  Facts board = good_facts(); ///< the facts the port gathers (its window is ignored)
  int reset_calls = 0;
  int image_calls = 0;
  int cal_calls = 0;
  int memory_calls = 0;
  int stacks_calls = 0;
  int i2c_calls = 0;
  bool stick_running = true; ///< Read ADC exists
  int stick_running_calls = 0;
  std::array<PostGate, 16> stored{};
  std::size_t stores = 0;
  std::array<std::array<char, 128>, 48> lines{};
  std::size_t line_count = 0;

  [[nodiscard]] std::string_view line(std::size_t i) const { return lines[i].data(); }
  [[nodiscard]] bool printed(std::string_view text) const {
    for (std::size_t i = 0; i < line_count; ++i) {
      if (line(i) == text) {
        return true;
      }
    }
    return false;
  }
};

struct FakePort {
  World *world;
  ResetReason reset_reason() {
    ++world->reset_calls;
    return *world->board.reset;
  }
  hmi::post::ImageFacts image() {
    ++world->image_calls;
    return *world->board.image;
  }
  hmi::post::Calibration calibration() {
    ++world->cal_calls;
    return *world->board.cal;
  }
  std::optional<hmi::post::MemoryFacts> memory() {
    ++world->memory_calls;
    return world->board.memory;
  }
  hmi::post::StackFacts stacks() {
    ++world->stacks_calls;
    return *world->board.stacks;
  }
  hmi::post::I2cSet i2c() {
    ++world->i2c_calls;
    return *world->board.i2c;
  }
  bool stick_task_running() {
    ++world->stick_running_calls;
    return world->stick_running;
  }
  void store_gate(PostGate gate) {
    if (world->stores < world->stored.size()) {
      world->stored[world->stores] = gate;
    }
    ++world->stores;
  }
  void print(std::string_view text) {
    if (world->line_count < world->lines.size()) {
      auto &dst = world->lines[world->line_count];
      const std::size_t n = std::min(text.size(), dst.size() - 1);
      text.copy(dst.data(), n);
      dst[n] = '\0';
    }
    ++world->line_count;
  }
};

using Runner = hmi::post::PostRunner<FakePort>;

/// A healthy window (the fixture's).
StickWindow good_window() { return *good_facts().window; }

/// The fixture's window with the y axis moved `offset_mv` off the calibrated centre.
StickWindow window_off_centre(std::int32_t offset_mv) {
  Facts f = good_facts();
  (void)post_test::set_measured(f, Id::REST_Y, offset_mv);
  return *f.window;
}

constexpr std::uint32_t T0 = 8400; // the UI's clock at the runner's first tick (board 2: ~8.4 s)

} // namespace

TEST_CASE("POST-034 the rest window starts at the first all-valid cycle: none until 30 cycles "
          "after it",
          "[post][window][REQ-POST-14]") {
  RestWindow w;
  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_FALSE(feed_invalid(w).has_value());
  }
  for (std::uint32_t i = 1; i < N; ++i) {
    TEST_ASSERT_FALSE(feed(w, 1650.0f, 1650.0f, 1650.0f).has_value());
  }
  const std::optional<StickWindow> got = feed(w, 1650.0f, 1650.0f, 1650.0f);
  TEST_ASSERT_TRUE(got.has_value());
  TEST_ASSERT_EQUAL_UINT32(N, got->cycles); // the 5 invalid cycles before it do not count
  TEST_ASSERT_EQUAL_UINT32(N, got->valid_cycles);
}

TEST_CASE("POST-035 thirty valid cycles give one window with exact statistics; cycle 31 starts "
          "a new one",
          "[post][window][REQ-POST-14]") {
  RestWindow w;
  std::optional<StickWindow> got;
  for (std::uint32_t i = 0; i < N; ++i) {
    // x: 1640 and 1660 alternating (mean 1650); y: 1000 + i (1000..1029, mean 1014.5 -> 1015);
    // twist: 1500 throughout
    const float x = (i % 2 == 0) ? 1640.0f : 1660.0f;
    got = feed(w, x, 1000.0f + static_cast<float>(i), 1500.0f);
    TEST_ASSERT_EQUAL(i + 1 == N, got.has_value());
  }
  TEST_ASSERT_EQUAL_UINT32(30, got->cycles);
  TEST_ASSERT_EQUAL_UINT32(30, got->valid_cycles);
  expect_axis(got->x, 1650, 1640, 1660, 30);
  expect_axis(got->y, 1015, 1000, 1029, 30);
  expect_axis(got->twist, 1500, 1500, 1500, 30);
  TEST_ASSERT_TRUE(got->button_idle);
  // Cycle 31 starts the next window: it holds only the cycles after the first.
  for (std::uint32_t i = 0; i < N; ++i) {
    got = feed(w, 2000.0f, 2100.0f, 2200.0f);
    TEST_ASSERT_EQUAL(i + 1 == N, got.has_value());
  }
  expect_axis(got->x, 2000, 2000, 2000, 30);
  expect_axis(got->y, 2100, 2100, 2100, 30);
  expect_axis(got->twist, 2200, 2200, 2200, 30);
}

TEST_CASE("POST-036 an invalid cycle inside a window counts as a cycle, not as a sample",
          "[post][window][REQ-POST-14]") {
  RestWindow w;
  std::optional<StickWindow> got;
  for (std::uint32_t i = 0; i < N; ++i) {
    got = (i == 10) ? feed_invalid(w) : feed(w, 1650.0f + static_cast<float>(i), 1600.0f, 1500.0f);
  }
  TEST_ASSERT_TRUE(got.has_value());
  TEST_ASSERT_EQUAL_UINT32(30, got->cycles);
  TEST_ASSERT_EQUAL_UINT32(29, got->valid_cycles);
  // x over the 29 valid cycles: 1650..1679 without 1660: (49935 - 1660) / 29 = 1664.66 -> 1665
  expect_axis(got->x, 1665, 1650, 1679, 29);
  expect_axis(got->y, 1600, 1600, 1600, 29);
}

TEST_CASE("POST-037 the button pressed on one cycle makes the window not idle; the next one is",
          "[post][window][REQ-POST-14]") {
  RestWindow w;
  std::optional<StickWindow> got;
  for (std::uint32_t i = 0; i < N; ++i) {
    got = feed(w, 1650.0f, 1650.0f, 1650.0f, i == 7);
  }
  TEST_ASSERT_FALSE(got->button_idle);
  for (std::uint32_t i = 0; i < N; ++i) {
    got = feed(w, 1650.0f, 1650.0f, 1650.0f, false);
  }
  TEST_ASSERT_TRUE(got->button_idle);
}

TEST_CASE("POST-038 a NaN read is a failed read; 0 and 4095 mV do not overflow; a half rounds "
          "away from zero",
          "[post][window][hostile][REQ-POST-14]") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  RestWindow w;
  // A NaN read never starts a window.
  TEST_ASSERT_FALSE(w.add(nan, 1650.0f, 1650.0f, false).has_value());
  std::optional<StickWindow> got;
  for (std::uint32_t i = 0; i < N; ++i) {
    // x: 0 and 4095 alternating (mean 2047.5 -> 2048); y: 1650 and 1651 (1650.5 -> 1651);
    // twist NaN on cycle 3
    const float x = (i % 2 == 0) ? 0.0f : 4095.0f;
    const float y = (i % 2 == 0) ? 1650.0f : 1651.0f;
    got = w.add(x, y, i == 3 ? nan : 1500.0f, false);
  }
  TEST_ASSERT_TRUE(got.has_value());
  TEST_ASSERT_EQUAL_UINT32(30, got->cycles);
  TEST_ASSERT_EQUAL_UINT32(29, got->valid_cycles); // the NaN cycle is invalid
  // valid x: 15 at 0 (i even) and 14 at 4095 (i odd, without i = 3): 57330 / 29 = 1976.9 -> 1977
  expect_axis(got->x, 1977, 0, 4095, 29);
  // valid y: 15 at 1650 and 14 at 1651: 47864 / 29 = 1650.48 -> 1650
  expect_axis(got->y, 1650, 1650, 1651, 29);
  // An exact half: 1650 and 1651, 15 each.
  RestWindow half;
  for (std::uint32_t i = 0; i < N; ++i) {
    got = half.add(1650.0f + static_cast<float>(i % 2), 1650.0f, 1650.0f, false);
  }
  TEST_ASSERT_EQUAL_INT32(1651, got->x.mean_mv); // 1650.5 rounds away from zero
  // A read at 0 throughout and at 4095 throughout.
  RestWindow ends;
  for (std::uint32_t i = 0; i < N; ++i) {
    got = ends.add(0.0f, 4095.0f, 0.0f, false);
  }
  expect_axis(got->x, 0, 0, 0, 30);
  expect_axis(got->y, 4095, 4095, 4095, 30);
}

TEST_CASE("POST-039 the runner: NOT_RUN before its first tick, PENDING at it, the boot facts "
          "gathered exactly once at the first window",
          "[post][runner][REQ-POST-15]") {
  World world;
  Runner runner{FakePort{&world}};
  TEST_ASSERT_TRUE(runner.gate() == PostGate::NOT_RUN);
  TEST_ASSERT_EQUAL_size_t(0, world.stores);
  runner.tick(T0, std::nullopt);
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  TEST_ASSERT_EQUAL_size_t(1, world.stores);
  TEST_ASSERT_TRUE(world.stored[0] == PostGate::PENDING);
  TEST_ASSERT_EQUAL_INT(0, world.reset_calls); // nothing gathered before the first window
  // A stick off centre keeps it PENDING, so later windows keep coming.
  for (int i = 1; i <= 4; ++i) {
    runner.tick(T0 + 250U * static_cast<std::uint32_t>(i), window_off_centre(400));
  }
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  TEST_ASSERT_EQUAL_INT(1, world.reset_calls);
  TEST_ASSERT_EQUAL_INT(1, world.image_calls);
  TEST_ASSERT_EQUAL_INT(1, world.cal_calls);
  TEST_ASSERT_EQUAL_INT(1, world.memory_calls);
  TEST_ASSERT_EQUAL_INT(1, world.stacks_calls);
  TEST_ASSERT_EQUAL_INT(1, world.i2c_calls);
  TEST_ASSERT_EQUAL_size_t(1, world.stores); // stored only on a change
}

TEST_CASE("POST-040 a stick about 25 % forward keeps the POST PENDING on joy.y_cal_off with the "
          "indicator WAITING; a centred window passes it",
          "[post][runner][REQ-POST-15][REQ-POST-18]") {
  World world;
  Runner runner{FakePort{&world}};
  runner.tick(T0, std::nullopt);
  runner.tick(T0 + 250, window_off_centre(400));
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  TEST_ASSERT_TRUE(hmi::post::blocking_check(runner.report()) == Id::REST_Y);
  const Indicator shown = hmi::post::post_indicator(runner.gate(), runner.report(),
                                                    runner.timed_out(), runner.since_start_ms(T0));
  TEST_ASSERT_TRUE(shown.kind == IndicatorKind::WAITING);
  TEST_ASSERT_TRUE(shown.colour == IndicatorColour::AMBER);
  TEST_ASSERT_TRUE(shown.check == Id::REST_Y);
  TEST_ASSERT_TRUE(world.printed("POST waiting: joy.y_cal_off 400 mV"));
  runner.tick(T0 + 500, good_window());
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PASS);
  TEST_ASSERT_TRUE(world.stored[world.stores - 1] == PostGate::PASS);
}

TEST_CASE("POST-041 a missing I2C device (0x5a) fails the POST, and good windows never clear it",
          "[post][runner][REQ-POST-15]") {
  World world;
  hmi::post::I2cSet found;
  for (const std::uint8_t a : hmi::post::EXPECTED_I2C) {
    if (a != 0x5a) {
      found.add(a);
    }
  }
  world.board.i2c = found;
  Runner runner{FakePort{&world}};
  runner.tick(T0, std::nullopt);
  runner.tick(T0 + 250, good_window());
  TEST_ASSERT_TRUE(runner.gate() == PostGate::FAIL);
  TEST_ASSERT_TRUE(hmi::post::blocking_check(runner.report()) == Id::I2C_MISSING);
  for (std::uint32_t i = 2; i < 40; ++i) {
    runner.tick(T0 + 250U * i, good_window());
  }
  TEST_ASSERT_TRUE(runner.gate() == PostGate::FAIL);
  TEST_ASSERT_TRUE(hmi::post::blocking_check(runner.report()) == Id::I2C_MISSING);
}

TEST_CASE("POST-042 with no window at all the POST is PENDING at 2999 ms and FAIL at 3000 ms, "
          "adc.valid blocking, timed out",
          "[post][runner][budget][REQ-POST-16]") {
  static_assert(hmi::post::POST_BUDGET_MS == 3000, "decision C4: 3000 ms");
  World world;
  Runner runner{FakePort{&world}};
  runner.tick(T0, std::nullopt);
  runner.tick(T0 + 2999, std::nullopt);
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  TEST_ASSERT_FALSE(runner.timed_out());
  runner.tick(T0 + 3000, std::nullopt);
  TEST_ASSERT_TRUE(runner.gate() == PostGate::FAIL);
  TEST_ASSERT_TRUE(runner.timed_out());
  TEST_ASSERT_TRUE(hmi::post::blocking_check(runner.report()) == Id::ADC_VALID);
  const Indicator shown =
      hmi::post::post_indicator(runner.gate(), runner.report(), runner.timed_out(), 3000);
  TEST_ASSERT_TRUE(shown.kind == IndicatorKind::FAILED);
  TEST_ASSERT_TRUE(shown.timed_out);
  TEST_ASSERT_TRUE(shown.check == Id::ADC_VALID);
}

TEST_CASE("POST-043 a stick held off centre for 60 s keeps the POST PENDING and never fails it",
          "[post][runner][budget][REQ-POST-16]") {
  World world;
  Runner runner{FakePort{&world}};
  runner.tick(T0, std::nullopt);
  // 60 s of 250 ms ticks; a window every fourth tick (about the 1.05 s window)
  for (std::uint32_t i = 1; i <= 240; ++i) {
    const std::optional<StickWindow> w =
        (i % 4 == 0) ? std::optional<StickWindow>{window_off_centre(400)} : std::nullopt;
    runner.tick(T0 + 250U * i, w);
    TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  }
  TEST_ASSERT_FALSE(runner.timed_out());
  TEST_ASSERT_EQUAL_size_t(1, world.stores);
}

TEST_CASE("POST-044 no sequence the runner can produce moves the gate back from PASS or FAIL",
          "[post][runner][REQ-POST-15]") {
  std::mt19937 rng(44);
  for (int run = 0; run < 400; ++run) {
    World world;
    switch (run % 4) { // the boot: healthy, a device missing, a panic reset, never calibrated
    case 1:
      world.board.i2c = hmi::post::I2cSet{};
      break;
    case 2:
      world.board.reset = ResetReason::PANIC;
      break;
    case 3:
      world.board.cal->saved = false;
      break;
    default:
      break;
    }
    Runner runner{FakePort{&world}};
    std::uint32_t now = T0 + static_cast<std::uint32_t>(rng() % 1000U);
    for (int tick = 0; tick < 40; ++tick) {
      std::optional<StickWindow> w;
      switch (rng() % 4U) {
      case 0:
        break; // no window this tick
      case 1:
        w = good_window();
        break;
      case 2:
        w = window_off_centre(static_cast<std::int32_t>(rng() % 900U));
        break;
      default:
        w = good_window();
        w->valid_cycles = w->cycles - 1; // one failed read
        break;
      }
      now += 1U + static_cast<std::uint32_t>(rng() % 600U);
      runner.tick(now, w);
    }
    // The stored gates: PENDING first, then at most one of PASS or FAIL, then nothing.
    TEST_ASSERT_TRUE(world.stores >= 1 && world.stores <= 2);
    TEST_ASSERT_TRUE(world.stored[0] == PostGate::PENDING);
    if (world.stores == 2) {
      TEST_ASSERT_TRUE(world.stored[1] == PostGate::PASS || world.stored[1] == PostGate::FAIL);
      TEST_ASSERT_TRUE(runner.gate() == world.stored[1]);
    }
  }
}

TEST_CASE("POST-045 the POST lines at PASS, at FAIL and at a timeout are TS-POST-05's text",
          "[post][runner][lines][REQ-POST-17]") {
  // PASS: the fixture's healthy boot, first window 1300 ms after the first tick.
  World pass;
  Runner ok{FakePort{&pass}};
  ok.tick(T0, std::nullopt);
  ok.tick(T0 + 1300, good_window());
  constexpr std::array<std::string_view, 21> PASS_LINES{
      "POST reset reason: power-on (1)",
      "POST adc.valid PASS 1000 0.1% [990,1000]",
      "POST joy.cal_saved PASS 1 [1,1]",
      "POST joy.cal_span PASS 1600 mV [1000,2147483647]",
      "POST i2c.missing PASS 0 [0,0]",
      "POST sys.clean_reset PASS 1 [1,1]",
      "POST img.ok PASS 1 [1,1]",
      "POST mem.int_min PASS 30000 B [12288,2147483647]",
      "POST mem.int_block PASS 22000 B [12288,2147483647]",
      "POST mem.dma_min PASS 2400 B [1536,2147483647]",
      "POST mem.psram_free PASS 28000000 B [8388608,2147483647]",
      "POST stk.adc PASS 1496 B [1024,2147483647]",
      "POST stk.ui PASS 11060 B [2048,2147483647]",
      "POST joy.x_cal_off PASS 0 mV [0,40]",
      "POST joy.y_cal_off PASS 0 mV [0,40]",
      "POST joy.twist_cal_off PASS 0 mV [0,50]",
      "POST joy.x_noise PASS 2 mV [0,30]",
      "POST joy.y_noise PASS 2 mV [0,30]",
      "POST joy.twist_noise PASS 60 mV [0,120]",
      "POST joy.button_idle PASS 1 [1,1]",
      "POST RESULT PASS 19/19 1300",
  };
  TEST_ASSERT_EQUAL_size_t(PASS_LINES.size(), pass.line_count);
  for (std::size_t i = 0; i < PASS_LINES.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(std::string(PASS_LINES[i]).c_str(), std::string(pass.line(i)).c_str());
  }
  // A second decision never prints again.
  ok.tick(T0 + 1550, good_window());
  TEST_ASSERT_EQUAL_size_t(PASS_LINES.size(), pass.line_count);

  // FAIL: never calibrated, with the stick off centre at the time (a LIVE FAIL prints FAIL).
  World fail;
  fail.board.cal->saved = false;
  Runner failed{FakePort{&fail}};
  failed.tick(T0, std::nullopt);
  failed.tick(T0 + 1250, window_off_centre(400));
  TEST_ASSERT_TRUE(fail.printed("POST joy.cal_saved FAIL 0 [1,1]"));
  TEST_ASSERT_TRUE(fail.printed("POST joy.y_cal_off FAIL 400 mV [0,40]"));
  TEST_ASSERT_TRUE(fail.printed("POST RESULT FAIL 17/19 1250"));

  // Timeout with no window: the reset reason is still logged; every LATCHED check prints FAIL
  // (no measurement is a FAIL), every LIVE one SKIP.
  World timeout;
  timeout.board.reset = ResetReason::SW;
  Runner timed{FakePort{&timeout}};
  timed.tick(T0, std::nullopt);
  timed.tick(T0 + 3000, std::nullopt);
  TEST_ASSERT_EQUAL_STRING("POST reset reason: software (3)", std::string(timeout.line(0)).c_str());
  TEST_ASSERT_EQUAL_STRING("POST adc.valid FAIL 0 0.1% [990,1000]",
                           std::string(timeout.line(1)).c_str());
  TEST_ASSERT_EQUAL_STRING("POST stk.ui FAIL 0 B [2048,2147483647]",
                           std::string(timeout.line(12)).c_str());
  TEST_ASSERT_EQUAL_STRING("POST joy.x_cal_off SKIP 0 mV [0,40]",
                           std::string(timeout.line(13)).c_str());
  TEST_ASSERT_EQUAL_STRING("POST joy.button_idle SKIP 0 [1,1]",
                           std::string(timeout.line(19)).c_str());
  TEST_ASSERT_EQUAL_STRING("POST RESULT FAIL 0/19 3000", std::string(timeout.line(20)).c_str());
  TEST_ASSERT_EQUAL_size_t(21, timeout.line_count);
}

TEST_CASE("POST-046 the indicator follows section 2.8's table for every gate, blocking kind and "
          "verdict, and NOT_RUN before and after the budget",
          "[post][indicator][REQ-POST-18]") {
  using hmi::post::post_indicator;
  using hmi::post::Report;
  const Report nothing = hmi::post::evaluate(Facts{}); // every check PENDING
  const Report passed = hmi::post::evaluate(good_facts());
  Facts off = good_facts();
  (void)post_test::set_measured(off, Id::REST_Y, 400);
  const Report live_fail = hmi::post::evaluate(off); // latched all PASS, a LIVE FAIL
  Facts few = good_facts();
  few.window->y.samples = 10; // a LIVE check PENDING (too few samples), latched all PASS
  const Report live_pending = hmi::post::evaluate(few);
  Facts unsaved = good_facts();
  unsaved.cal->saved = false;
  const Report latched_fail = hmi::post::evaluate(unsaved);

  Indicator i = post_indicator(PostGate::PASS, passed, false, 0);
  TEST_ASSERT_TRUE(i.kind == IndicatorKind::NONE && i.colour == IndicatorColour::NONE);
  TEST_ASSERT_FALSE(i.check.has_value());

  i = post_indicator(PostGate::PENDING, nothing, false, 500); // a LATCHED check PENDING
  TEST_ASSERT_TRUE(i.kind == IndicatorKind::CHECKING && i.colour == IndicatorColour::GREY);
  TEST_ASSERT_TRUE(i.check == Id::ADC_VALID);

  i = post_indicator(PostGate::PENDING, live_fail, false, 500); // a LIVE check, FAIL
  TEST_ASSERT_TRUE(i.kind == IndicatorKind::WAITING && i.colour == IndicatorColour::AMBER);
  TEST_ASSERT_TRUE(i.check == Id::REST_Y);

  i = post_indicator(PostGate::PENDING, live_pending, false, 500); // a LIVE check, PENDING
  TEST_ASSERT_TRUE(i.kind == IndicatorKind::WAITING && i.colour == IndicatorColour::AMBER);
  TEST_ASSERT_TRUE(i.check == Id::REST_Y);

  i = post_indicator(PostGate::FAIL, latched_fail, false, 1200); // a LATCHED check, FAIL
  TEST_ASSERT_TRUE(i.kind == IndicatorKind::FAILED && i.colour == IndicatorColour::RED);
  TEST_ASSERT_TRUE(i.check == Id::CAL_SAVED);
  TEST_ASSERT_FALSE(i.timed_out);

  i = post_indicator(PostGate::FAIL, nothing, true, 3000); // a LATCHED check timed out
  TEST_ASSERT_TRUE(i.kind == IndicatorKind::FAILED && i.timed_out && i.check == Id::ADC_VALID);

  for (const std::uint32_t since : {0U, 2999U}) { // NOT_RUN before the budget: checking
    i = post_indicator(PostGate::NOT_RUN, nothing, false, since);
    TEST_ASSERT_TRUE(i.kind == IndicatorKind::CHECKING && i.colour == IndicatorColour::GREY);
    TEST_ASSERT_FALSE(i.check.has_value());
  }
  for (const std::uint32_t since : {3000U, 600000U}) { // from the budget on: not run, red
    i = post_indicator(PostGate::NOT_RUN, nothing, false, since);
    TEST_ASSERT_TRUE(i.kind == IndicatorKind::NOT_RUN && i.colour == IndicatorColour::RED);
  }
  i = post_indicator(static_cast<PostGate>(7), passed, false, 0); // a byte outside the enum
  TEST_ASSERT_TRUE(i.kind == IndicatorKind::FAILED && i.colour == IndicatorColour::RED);
  TEST_ASSERT_FALSE(i.check.has_value());
}

TEST_CASE("POST-047 the reset reason: TASK_WDT and UNKNOWN fail sys.clean_reset, SW passes",
          "[post][runner][reset][REQ-POST-15]") {
  struct Case {
    ResetReason reason;
    PostGate gate;
  };
  for (const Case c :
       {Case{ResetReason::TASK_WDT, PostGate::FAIL}, Case{ResetReason::UNKNOWN, PostGate::FAIL},
        Case{ResetReason::SW, PostGate::PASS}}) {
    World world;
    world.board.reset = c.reason;
    Runner runner{FakePort{&world}};
    runner.tick(T0, std::nullopt);
    runner.tick(T0 + 250, good_window());
    TEST_ASSERT_TRUE(runner.gate() == c.gate);
    if (c.gate == PostGate::FAIL) {
      TEST_ASSERT_TRUE(hmi::post::blocking_check(runner.report()) == Id::RESET_CLEAN);
    }
  }
}

TEST_CASE("POST-048 1000 accumulator cycles and 100 runner ticks allocate nothing",
          "[post][alloc][REQ-POST-20]") {
  RestWindow w;
  unsigned windows = 0;
  unsigned window_allocs = 0;
  {
    host_test::NoAlloc scope;
    for (int i = 0; i < 1000; ++i) {
      const std::optional<float> twist = i % 13 == 0 ? std::nullopt : std::optional<float>{1500.0f};
      if (w.add(1650.0f, 1650.0f + static_cast<float>(i % 7), twist, i % 97 == 0)) {
        ++windows;
      }
    }
    window_allocs = scope.count();
  }
  TEST_ASSERT_EQUAL_UINT(0U, window_allocs);
  TEST_ASSERT_EQUAL_UINT(1000U / N, windows);
  World world;
  world.board.reset = ResetReason::POWERON;
  Runner runner{FakePort{&world}};
  unsigned runner_allocs = 0;
  {
    host_test::NoAlloc scope;
    for (std::uint32_t i = 0; i < 100; ++i) {
      std::optional<StickWindow> window;
      if (i % 4 == 3) {
        window = window_off_centre(i < 60 ? 400 : 0);
      }
      runner.tick(T0 + 250U * i, window);
    }
    runner_allocs = scope.count();
  }
  TEST_ASSERT_EQUAL_UINT(0U, runner_allocs);
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PASS);
}

TEST_CASE("POST-049 Overall maps onto PostGate one to one; a value outside the enum is FAIL",
          "[post][gate][REQ-POST-15]") {
  static_assert(hmi::post::gate_of(Overall::PENDING) == PostGate::PENDING);
  static_assert(hmi::post::gate_of(Overall::PASS) == PostGate::PASS);
  static_assert(hmi::post::gate_of(Overall::FAIL) == PostGate::FAIL);
  TEST_ASSERT_TRUE(hmi::post::gate_of(Overall::PENDING) == PostGate::PENDING);
  TEST_ASSERT_TRUE(hmi::post::gate_of(Overall::PASS) == PostGate::PASS);
  TEST_ASSERT_TRUE(hmi::post::gate_of(Overall::FAIL) == PostGate::FAIL);
  TEST_ASSERT_TRUE(hmi::post::gate_of(static_cast<Overall>(9)) == PostGate::FAIL);
}

TEST_CASE("POST-051 the reset reason names are the self test's (main/selftest.cpp), value for "
          "value",
          "[post][reset][REQ-POST-17]") {
  // The self test's reset_reason_name: `case ESP_RST_<X>: return "<name>";`, default "other".
  std::ifstream in(std::string(POST_REPO_ROOT) + "/main/selftest.cpp");
  std::ostringstream text;
  text << in.rdbuf();
  const std::string src = text.str();
  const std::size_t start = src.find("const char *reset_reason_name(esp_reset_reason_t reason)");
  TEST_ASSERT_TRUE_MESSAGE(start != std::string::npos, "reset_reason_name not found");
  const std::string body = src.substr(start, src.find("\n}", start) - start);
  const std::regex item(R"re(case ESP_RST_([A-Z_]+):\s*return "([^"]*)";)re");
  struct Named {
    std::string_view idf;
    ResetReason reason;
  };
  constexpr std::array<Named, 16> ALL{{
      {"UNKNOWN", ResetReason::UNKNOWN},
      {"POWERON", ResetReason::POWERON},
      {"EXT", ResetReason::EXT},
      {"SW", ResetReason::SW},
      {"PANIC", ResetReason::PANIC},
      {"INT_WDT", ResetReason::INT_WDT},
      {"TASK_WDT", ResetReason::TASK_WDT},
      {"WDT", ResetReason::WDT},
      {"DEEPSLEEP", ResetReason::DEEPSLEEP},
      {"BROWNOUT", ResetReason::BROWNOUT},
      {"SDIO", ResetReason::SDIO},
      {"USB", ResetReason::USB},
      {"JTAG", ResetReason::JTAG},
      {"EFUSE", ResetReason::EFUSE},
      {"PWR_GLITCH", ResetReason::PWR_GLITCH},
      {"CPU_LOCKUP", ResetReason::CPU_LOCKUP},
  }};
  int named = 0;
  for (const Named &n : ALL) {
    std::string want = "other";
    for (auto it = std::sregex_iterator(body.begin(), body.end(), item);
         it != std::sregex_iterator(); ++it) {
      if ((*it)[1].str() == n.idf) {
        want = (*it)[2].str();
        ++named;
      }
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want.c_str(),
                                     std::string(hmi::post::reset_reason_name(n.reason)).c_str(),
                                     std::string(n.idf).c_str());
    // The bench's STATE names it as ESP-IDF does, without ESP_RST_.
    TEST_ASSERT_EQUAL_STRING(std::string(n.idf).c_str(),
                             std::string(hmi::post::reset_reason_id(n.reason)).c_str());
  }
  TEST_ASSERT_EQUAL_INT(13, named); // the self test names 13; the rest read "other"
  TEST_ASSERT_EQUAL_STRING(
      "other", std::string(hmi::post::reset_reason_name(static_cast<ResetReason>(99))).c_str());
}

TEST_CASE("POST-052 bench only: PERMIT POST forces the gate through the runner and holds it; "
          "POST RERUN restarts from NOT_RUN and gathers the boot facts again",
          "[post][runner][bench][REQ-POST-15]") {
  World world;
  Runner runner{FakePort{&world}};
  runner.tick(T0, std::nullopt);
  runner.tick(T0 + 250, good_window());
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PASS);
  runner.force(PostGate::PENDING);
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  TEST_ASSERT_TRUE(world.stored[world.stores - 1] == PostGate::PENDING);
  runner.tick(T0 + 500, good_window()); // held: the override wins over the facts
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  runner.rerun();
  TEST_ASSERT_TRUE(runner.gate() == PostGate::NOT_RUN);
  TEST_ASSERT_TRUE(world.stored[world.stores - 1] == PostGate::NOT_RUN);
  runner.tick(T0 + 750, std::nullopt);
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PENDING);
  runner.tick(T0 + 1000, good_window());
  TEST_ASSERT_TRUE(runner.gate() == PostGate::PASS);
  TEST_ASSERT_EQUAL_INT(2, world.reset_calls); // the boot facts gathered again
}

TEST_CASE("POST-053 at the budget with no window, a stick task that is not running is named as "
          "the cause in its own line; asked only at the budget",
          "[post][runner][budget][REQ-POST-16]") {
  World world;
  world.stick_running = false; // Read ADC never started
  Runner runner{FakePort{&world}};
  runner.tick(T0, std::nullopt);
  for (std::uint32_t t = 250; t < 3000; t += 250) {
    runner.tick(T0 + t, std::nullopt);
  }
  TEST_ASSERT_EQUAL_INT(0, world.stick_running_calls); // early ticks never ask
  runner.tick(T0 + 3000, std::nullopt);
  TEST_ASSERT_TRUE(runner.gate() == PostGate::FAIL);
  TEST_ASSERT_TRUE(runner.stick_task_missing());
  TEST_ASSERT_TRUE(hmi::post::blocking_check(runner.report()) == Id::ADC_VALID);
  // the 19 check lines are TS-POST-05's as ever, then the cause, then the result
  TEST_ASSERT_EQUAL_size_t(22, world.line_count);
  TEST_ASSERT_EQUAL_STRING("POST adc.valid FAIL 0 0.1% [990,1000]",
                           std::string(world.line(1)).c_str());
  TEST_ASSERT_EQUAL_STRING("POST cause: Read ADC not running", std::string(world.line(20)).c_str());
  TEST_ASSERT_EQUAL_STRING("POST RESULT FAIL 0/19 3000", std::string(world.line(21)).c_str());

  World running; // the same boot with the task running: no cause line
  Runner other{FakePort{&running}};
  other.tick(T0, std::nullopt);
  other.tick(T0 + 3000, std::nullopt);
  TEST_ASSERT_FALSE(other.stick_task_missing());
  TEST_ASSERT_FALSE(running.printed("POST cause: Read ADC not running"));
  TEST_ASSERT_EQUAL_INT(1, running.stick_running_calls);

  World windowed; // a window came: the task runs, nothing to ask
  windowed.board.cal->saved = true;
  Runner third{FakePort{&windowed}};
  third.tick(T0, std::nullopt);
  third.tick(T0 + 250, window_off_centre(400));
  windowed.board.memory.reset(); // not reached: the facts are gathered at the first window
  third.tick(T0 + 3000, std::nullopt);
  TEST_ASSERT_EQUAL_INT(0, windowed.stick_running_calls);
}

TEST_CASE("POST-054 a gate that leaves PASS or FAIL restarts the rest window: after a rerun, "
          "failed reads only never start one (C3 section 2.4)",
          "[post][window][bench][REQ-POST-20]") {
  using hmi::post::rest_window_feeds;
  using hmi::post::rest_window_restarts;
  // The feeding rule: NOT_RUN and PENDING feed, PASS and FAIL stop it.
  TEST_ASSERT_TRUE(rest_window_feeds(PostGate::NOT_RUN));
  TEST_ASSERT_TRUE(rest_window_feeds(PostGate::PENDING));
  TEST_ASSERT_FALSE(rest_window_feeds(PostGate::PASS));
  TEST_ASSERT_FALSE(rest_window_feeds(PostGate::FAIL));
  // The restart rule, every pair: leaving PASS or FAIL, or coming back to NOT_RUN.
  const std::array<PostGate, 4> gates{PostGate::NOT_RUN, PostGate::PENDING, PostGate::PASS,
                                      PostGate::FAIL};
  for (const PostGate seen : gates) {
    for (const PostGate now : gates) {
      const bool decided_before = seen == PostGate::PASS || seen == PostGate::FAIL;
      const bool decided_now = now == PostGate::PASS || now == PostGate::FAIL;
      const bool want = (decided_before && !decided_now) ||
                        (now == PostGate::NOT_RUN && seen != PostGate::NOT_RUN);
      TEST_ASSERT_EQUAL(want, rest_window_restarts(seen, now));
    }
  }
  // The board's sequence (B5''-22, run l3-c3d-9e0fb77): a window part full when POST passed,
  // then POST RERUN seen as PASS -> PENDING (the runner stores NOT_RUN and PENDING on one UI
  // tick), then every read fails (fail_mask 7): no window may ever come (the budget fails it).
  RestWindow w;
  for (int i = 0; i < 12; ++i) {
    TEST_ASSERT_FALSE(feed(w, 1507.0f, 1510.0f, 1477.0f).has_value());
  }
  TEST_ASSERT_TRUE(rest_window_restarts(PostGate::PASS, PostGate::PENDING));
  w.reset();
  for (int i = 0; i < 200; ++i) {
    TEST_ASSERT_FALSE(feed_invalid(w).has_value());
  }
}
