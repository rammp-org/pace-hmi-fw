// Host L1 tests for components/seat, the seat gate and the seat reading (L1-SEAT; AGENT_BRIEF
// host L1 contract, TS-UNIT-02). spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the
// IDF linux target (owner, 2026-10-06). Built with UBSan and ASan (tests/host/common.mk): a
// case that reached undefined behaviour stops the run.
//
// Expected values are written from docs/plans/hazard-seat-spec.md (§2.2-§2.4, §4.2 REQ-SEAT-01..06,
// §5.1 SEAT-001..010) and RAMMP_SEAT_AXIS_TABLE's rows, never from the code's output.

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

#include "alloc_guard.hpp"
#include "seat/seat_gate.hpp"
#include "seat/seat_reading.hpp"
#include "test_case.hpp"

namespace {

using hmi::seat::SeatAsk;
using hmi::seat::SeatDecision;
using hmi::seat::SeatInputs;
using hmi::seat::SeatLink;
using hmi::seat::SeatPress;
using hmi::seat::SeatReason;
using hmi::stick::PostGate;
using hmi::stick::StickHealth;
using rammp::kSeatAxes;
using rammp::SeatAxisSpec;

constexpr int32_t kUnknown = INT32_MIN; // hmi_format's VALUE_UNKNOWN, as the spec names it

// 10^decimals, written out from the table's decimals column (every axis has 1 decimal).
int32_t scale(const SeatAxisSpec &spec) {
  int32_t s = 1;
  for (uint8_t i = 0; i < spec.decimals; ++i) {
    s *= 10;
  }
  return s;
}

float units(const SeatAxisSpec &spec, int32_t raw) {
  return static_cast<float>(raw) / static_cast<float>(scale(spec));
}

// The inputs the spec's every-step start has: CONNECTED, IDLE, POST PASS, stick OK.
SeatInputs good(int32_t value) {
  return SeatInputs{.link = SeatLink::CONNECTED,
                    .mcb_state = static_cast<uint8_t>(MIB::MibSystemState::IDLE),
                    .post = PostGate::PASS,
                    .health = StickHealth::OK,
                    .value = value};
}

void expect_unknown(const SeatAxisSpec &spec, float value, const char *what) {
  char msg[96];
  std::snprintf(msg, sizeof msg, "%s on %s (%g)", what, spec.label, static_cast<double>(value));
  TEST_ASSERT_EQUAL_INT32_MESSAGE(kUnknown, hmi::seat::seat_reading(spec, value), msg);
}

} // namespace

TEST_CASE("SEAT-001 a NaN, an infinity or a huge value reads unknown on every axis, without "
          "undefined behaviour",
          "[seat][REQ-SEAT-01][H8]") {
  const float qnan = std::numeric_limits<float>::quiet_NaN();
  const float snan = std::bit_cast<float>(uint32_t{0x7fa00000}); // a signalling NaN's bits
  const float inf = std::numeric_limits<float>::infinity();
  const float hostile[] = {qnan, -qnan, snan, inf, -inf, 1e30f, -1e30f, 3e8f, FLT_MAX, -FLT_MAX};
  for (const SeatAxisSpec &spec : kSeatAxes) {
    for (const float v : hostile) {
      expect_unknown(spec, v, "hostile");
    }
  }
}

TEST_CASE("SEAT-002 the conversion bound: |value x scale| at 2^23 - 1, 2^23, 2^24, 2^31, 2^32 "
          "reads unknown, both signs",
          "[seat][REQ-SEAT-01][H8]") {
  const double scaled[] = {8388607.0, 8388608.0, 16777216.0, 2147483648.0, 4294967296.0};
  for (const SeatAxisSpec &spec : kSeatAxes) {
    for (const double s : scaled) {
      for (const double sign : {1.0, -1.0}) {
        expect_unknown(spec, static_cast<float>(sign * s / scale(spec)), "bound");
      }
    }
  }
}

TEST_CASE("SEAT-003 range edges: one step of tolerance each side is known, one raw unit more is "
          "not; -47.54 deg is -475, -47.56 deg unknown",
          "[seat][REQ-SEAT-01]") {
  for (const SeatAxisSpec &spec : kSeatAxes) {
    const int32_t lo = spec.min_value - spec.step;
    const int32_t hi = spec.max_value + spec.step;
    TEST_ASSERT_EQUAL_INT32_MESSAGE(lo, hmi::seat::seat_reading(spec, units(spec, lo)), spec.label);
    expect_unknown(spec, units(spec, lo - 1), "min - step - 1");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(hi, hmi::seat::seat_reading(spec, units(spec, hi)), spec.label);
    expect_unknown(spec, units(spec, hi + 1), "max + step + 1");
  }
  const SeatAxisSpec &fb = kSeatAxes[rammp::index_of(rammp::SeatAxis::FRONT_BACK_TILT)];
  TEST_ASSERT_EQUAL_INT32(-475, hmi::seat::seat_reading(fb, -47.54f));
  TEST_ASSERT_EQUAL_INT32(kUnknown, hmi::seat::seat_reading(fb, -47.56f));
  // Elevation's band ends at 255.0 mm (B5s-4): 255.0 known, 255.1 (raw 2551) not.
  const SeatAxisSpec &el = kSeatAxes[rammp::index_of(rammp::SeatAxis::ELEVATION)];
  TEST_ASSERT_EQUAL_INT32(2550, hmi::seat::seat_reading(el, 255.0f));
  TEST_ASSERT_EQUAL_INT32(kUnknown, hmi::seat::seat_reading(el, 255.1f));
}

TEST_CASE("SEAT-004 every raw value in the tolerance band survives the wire: "
          "seat_reading(seat_units(raw)) == raw on every axis",
          "[seat][REQ-SEAT-01]") {
  for (const SeatAxisSpec &spec : kSeatAxes) {
    for (int32_t raw = spec.min_value - spec.step; raw <= spec.max_value + spec.step; ++raw) {
      if (hmi::seat::seat_reading(spec, rammp::seat_units(spec, raw)) != raw) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "%s raw %d", spec.label, static_cast<int>(raw));
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
}

namespace {

// REQ-SEAT-02 as the spec words it: ALLOW only when CONNECTED, IDLE, PASS, health in
// {NOT_MONITORED, OK, CHECK} and the value known; else the first failing reason in the order
// LINK_DOWN, MCB_NOT_IDLE, POST_NOT_PASSED, STICK_FAULT, POSITION_UNKNOWN.
SeatReason spec_gate(bool connected, uint8_t state, uint8_t post, uint8_t health, bool known) {
  if (!connected) {
    return SeatReason::LINK_DOWN;
  }
  if (state != 1) { // IDLE is MibSystemState's second enumerator
    return SeatReason::MCB_NOT_IDLE;
  }
  if (post != 2) { // PASS
    return SeatReason::POST_NOT_PASSED;
  }
  if (!(health == 0 || health == 1 || health == 2)) { // NOT_MONITORED, OK, CHECK
    return SeatReason::STICK_FAULT;
  }
  if (!known) {
    return SeatReason::POSITION_UNKNOWN;
  }
  return SeatReason::ALLOW;
}

} // namespace

TEST_CASE("SEAT-005 the gate over every input combination: ALLOW exactly when all five hold, "
          "else the first failing reason in REQ-SEAT-02's order",
          "[seat][REQ-SEAT-02][safety]") {
  // link {CONNECTED, NO_PEER, DOWN}: the gate reads CONNECTED or not (LinkPort::state, live),
  // and a byte outside SeatLink refuses too.
  const SeatLink links[] = {SeatLink::CONNECTED, SeatLink::NOT_CONNECTED, SeatLink::NOT_CONNECTED,
                            static_cast<SeatLink>(7)};
  const uint8_t states[] = {0, 1, 2, 3, 4, 255}; // INITIALIZING, IDLE, ENABLED, ERROR, 4, 255
  const uint8_t posts[] = {0, 1, 2, 3, 7};       // NOT_RUN, PENDING, PASS, FAIL, 7
  const uint8_t healths[] = {0, 1, 2, 3, 9};     // NOT_MONITORED, OK, CHECK, FAULT, 9
  int allowed = 0;
  int cases = 0;
  for (const SeatAxisSpec &spec : kSeatAxes) {
    const int32_t mid = (spec.min_value + spec.max_value) / 2;
    for (const SeatLink link : links) {
      for (const uint8_t state : states) {
        for (const uint8_t post : posts) {
          for (const uint8_t health : healths) {
            for (const bool known : {true, false}) {
              const SeatInputs in{.link = link,
                                  .mcb_state = state,
                                  .post = static_cast<PostGate>(post),
                                  .health = static_cast<StickHealth>(health),
                                  .value = known ? mid : kUnknown};
              const SeatReason want =
                  spec_gate(link == SeatLink::CONNECTED, state, post, health, known);
              ++cases;
              const SeatReason got = hmi::seat::gate(in);
              const SeatDecision d =
                  hmi::seat::decide(spec, in, SeatAsk{.press = SeatPress::STEP, .amount = 1});
              if (got != want || d.reason != want) {
                char msg[128];
                std::snprintf(msg, sizeof msg, "%s link %u state %u post %u health %u known %d",
                              spec.label, static_cast<unsigned>(link), state, post, health, known);
                TEST_FAIL_MESSAGE(msg);
              }
              allowed += got == SeatReason::ALLOW;
            }
          }
        }
      }
    }
  }
  // Per axis: CONNECTED x IDLE x PASS x {NOT_MONITORED, OK, CHECK} x known = 3 ALLOW.
  TEST_ASSERT_EQUAL_INT(3 * static_cast<int>(kSeatAxes.size()), allowed);
  TEST_ASSERT_EQUAL_INT(4 * 6 * 5 * 5 * 2 * static_cast<int>(kSeatAxes.size()), cases);
}

TEST_CASE("SEAT-006 a step moves at most one step and never against the press: AT_LIMIT at an "
          "end pressed outward and in the band past it",
          "[seat][REQ-SEAT-03][safety]") {
  for (const SeatAxisSpec &spec : kSeatAxes) {
    const int32_t mn = spec.min_value;
    const int32_t mx = spec.max_value;
    const int32_t values[] = {mn - spec.step, mn, mn + 1,        (mn + mx) / 2,
                              mx - 1,         mx, mx + spec.step};
    for (const int32_t v : values) {
      for (const int32_t dir : {-1, +1}) {
        const SeatDecision d =
            hmi::seat::decide(spec, good(v), SeatAsk{.press = SeatPress::STEP, .amount = dir});
        const bool limit = (v == mn && dir < 0) || (v == mx && dir > 0) ||
                           (v == mn - spec.step && dir < 0) || (v == mx + spec.step && dir > 0);
        char msg[64];
        std::snprintf(msg, sizeof msg, "%s v %d dir %d", spec.label, static_cast<int>(v),
                      static_cast<int>(dir));
        if (limit) {
          TEST_ASSERT_TRUE_MESSAGE(d.reason == SeatReason::AT_LIMIT, msg);
          continue;
        }
        int32_t want = v + dir * spec.step;
        want = want < mn ? mn : (want > mx ? mx : want);
        TEST_ASSERT_TRUE_MESSAGE(d.reason == SeatReason::ALLOW, msg);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(want, d.target, msg);
        TEST_ASSERT_TRUE_MESSAGE(std::abs(d.target - v) <= spec.step, msg);
        TEST_ASSERT_TRUE_MESSAGE((d.target - v > 0) == (dir > 0) && d.target != v, msg);
      }
    }
  }
}

TEST_CASE("SEAT-007 a preset is clamp(preset) with the value known, and POSITION_UNKNOWN without",
          "[seat][REQ-SEAT-04]") {
  for (const SeatAxisSpec &spec : kSeatAxes) {
    for (const int32_t preset : {0, 150, 250, -10, 99999}) {
      const int32_t want = preset < spec.min_value
                               ? spec.min_value
                               : (preset > spec.max_value ? spec.max_value : preset);
      const SeatAsk ask{.press = SeatPress::PRESET, .amount = preset};
      const SeatDecision known = hmi::seat::decide(spec, good(spec.min_value), ask);
      TEST_ASSERT_TRUE_MESSAGE(known.reason == SeatReason::ALLOW, spec.label);
      TEST_ASSERT_EQUAL_INT32_MESSAGE(want, known.target, spec.label);
      const SeatDecision unknown = hmi::seat::decide(spec, good(kUnknown), ask);
      TEST_ASSERT_TRUE_MESSAGE(unknown.reason == SeatReason::POSITION_UNKNOWN, spec.label);
    }
  }
}

TEST_CASE("SEAT-008 property: 100 000 random values and inputs, every allowed target is finite "
          "and within the axis range in whole units",
          "[seat][REQ-SEAT-05][safety]") {
  std::mt19937 rng(20261011U); // fixed: a failure reproduces
  std::uniform_int_distribution<uint32_t> bits;
  std::uniform_int_distribution<int> pick(0, 9);
  int allowed = 0;
  for (int i = 0; i < 100000; ++i) {
    const SeatAxisSpec &spec = kSeatAxes[static_cast<size_t>(i) % kSeatAxes.size()];
    // Half raw bit patterns (NaNs, infinities, denormals, huge), half values near the range.
    const float wire =
        (i & 1) != 0
            ? std::bit_cast<float>(bits(rng))
            : units(spec, spec.min_value - 3 * spec.step +
                              static_cast<int32_t>(bits(rng) % static_cast<uint32_t>(
                                                                   spec.max_value - spec.min_value +
                                                                   6 * spec.step + 1)));
    SeatInputs in = good(hmi::seat::seat_reading(spec, wire));
    in.link = pick(rng) != 0 ? SeatLink::CONNECTED : SeatLink::NOT_CONNECTED;
    in.mcb_state = static_cast<uint8_t>(pick(rng) < 8 ? 1 : bits(rng));
    in.post = static_cast<PostGate>(pick(rng) < 8 ? 2 : bits(rng) % 256);
    in.health = static_cast<StickHealth>(pick(rng) < 8 ? 1 : bits(rng) % 256);
    const SeatAsk ask =
        pick(rng) < 5
            ? SeatAsk{.press = SeatPress::STEP, .amount = pick(rng) < 5 ? -1 : 1}
            : SeatAsk{.press = SeatPress::PRESET, .amount = static_cast<int32_t>(bits(rng))};
    const SeatDecision d = hmi::seat::decide(spec, in, ask);
    if (d.reason != SeatReason::ALLOW) {
      continue;
    }
    ++allowed;
    const float sent = rammp::seat_units(spec, d.target);
    const float lo = units(spec, spec.min_value);
    const float hi = units(spec, spec.max_value);
    if (!std::isfinite(sent) || sent < lo || sent > hi) {
      char msg[96];
      std::snprintf(msg, sizeof msg, "case %d: %s target %d (%g)", i, spec.label,
                    static_cast<int>(d.target), static_cast<double>(sent));
      TEST_FAIL_MESSAGE(msg);
    }
  }
  TEST_ASSERT_TRUE(allowed > 1000); // the property was exercised, not vacuously true
}

// SEAT-009: the reason order and a few cases at compile time. The case "builds" is the test;
// the run only records it.
namespace {
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr const SeatAxisSpec &kElevation = kSeatAxes[2];
static_assert(hmi::seat::kGateOrder[0] == SeatReason::LINK_DOWN &&
              hmi::seat::kGateOrder[1] == SeatReason::MCB_NOT_IDLE &&
              hmi::seat::kGateOrder[2] == SeatReason::POST_NOT_PASSED &&
              hmi::seat::kGateOrder[3] == SeatReason::STICK_FAULT &&
              hmi::seat::kGateOrder[4] == SeatReason::POSITION_UNKNOWN);
static_assert(hmi::seat::seat_reading(kElevation, 125.0f) == 1250);
static_assert(hmi::seat::seat_reading(kElevation, kNan) == INT32_MIN);
static_assert(hmi::seat::seat_reading(kElevation, 1e30f) == INT32_MIN);
static_assert(hmi::seat::gate(SeatInputs{.link = SeatLink::CONNECTED,
                                         .mcb_state = 1,
                                         .post = PostGate::PASS,
                                         .health = StickHealth::CHECK,
                                         .value = 1250}) == SeatReason::ALLOW);
static_assert(hmi::seat::gate(SeatInputs{.link = SeatLink::CONNECTED,
                                         .mcb_state = 2,
                                         .post = PostGate::FAIL,
                                         .health = StickHealth::FAULT,
                                         .value = INT32_MIN}) == SeatReason::MCB_NOT_IDLE);
static_assert(hmi::seat::step_target(kElevation, 2500, +1).reason == SeatReason::AT_LIMIT);
static_assert(hmi::seat::step_target(kElevation, -50, -1).reason == SeatReason::AT_LIMIT);
static_assert(hmi::seat::step_target(kElevation, 2550, -1).target == 2500);
} // namespace

TEST_CASE("SEAT-009 the reason order and some gate and reading cases hold at compile time",
          "[seat][REQ-SEAT-02][REQ-SEAT-06]") {
  TEST_ASSERT_TRUE(true); // the static_asserts above are the case
}

TEST_CASE("SEAT-010 10 000 gate decisions and readings make no allocation", "[seat][REQ-SEAT-06]") {
  int32_t sum = 0;
  TEST_ASSERT_NO_ALLOC({
    for (int i = 0; i < 10000; ++i) {
      const SeatAxisSpec &spec = kSeatAxes[static_cast<size_t>(i) % kSeatAxes.size()];
      const int32_t raw = hmi::seat::seat_reading(spec, static_cast<float>(i % 300) - 50.0f);
      const SeatDecision d = hmi::seat::decide(
          spec, good(raw), SeatAsk{.press = SeatPress::STEP, .amount = (i & 1) != 0 ? 1 : -1});
      sum += d.target;
    }
  });
  TEST_ASSERT_TRUE(sum != 0);
}

TEST_CASE("SEAT-012 a step with a direction of 0 never moves: AT_LIMIT on every axis and value",
          "[seat][REQ-SEAT-03]") {
  for (const SeatAxisSpec &spec : kSeatAxes) {
    for (const int32_t v :
         {spec.min_value, (spec.min_value + spec.max_value) / 2, spec.max_value}) {
      const SeatDecision d =
          hmi::seat::decide(spec, good(v), SeatAsk{.press = SeatPress::STEP, .amount = 0});
      TEST_ASSERT_TRUE_MESSAGE(d.reason == SeatReason::AT_LIMIT, spec.label);
    }
  }
}
