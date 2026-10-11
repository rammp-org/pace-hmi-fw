// L1 host tests for the stick monitor (hazard fix C2, docs/plans/hazard-c2-spec.md §2-§4, §8).
//
// STK-070..072  the oracle: the code (decide, transition) against the table, row by row, and
//               the table's invariants
// STK-073..081  classification of one sample (§2.2, P1..P6)
// STK-082..099  the state machine over time, on 35 ms fake cycles; STK-084, 092..098 through
//               the wiring as the ADC task runs it (MonitorCycle, the pipeline, C1's permit)
// STK-100       no allocation over 10,000 cycles of monitor + pipeline
// STK-101       the hold reason with the monitor writing stick health
// STK-102       health per state, and through the permit: only OK allows
//
// Expected values come from the spec, never from the code under test. Fake time only.

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "alloc_guard.hpp"
#include "stick/monitor_cycle.hpp"
#include "stick/output_permit.hpp"
#include "stick/stick_monitor.hpp"
#include "stick/stick_pipeline.hpp"
#include "test_case.hpp"

namespace hmi::stick {
// Reaches into the monitor to start it in a given state, or a corrupted one (STK-070, STK-072).
struct StickMonitorTestPeer {
  static void set_state(StickMonitor &m, std::uint8_t raw) {
    m.state_ = static_cast<MonitorState>(raw);
  }
  static void set_latched(StickMonitor &m, bool latched) { m.latched_ = latched; }
  // Applies a decision whose first action is a raw byte (a corrupted copy of a decision).
  static void apply_raw_action(StickMonitor &m, std::uint8_t raw_action) {
    m.apply({6, MonitorState::OK, {static_cast<MonitorAction>(raw_action), MonitorAction::NONE}},
            kPlausible, 0);
  }
  // Every member, so "no change at all" means exactly that (STK-070).
  static bool same(const StickMonitor &a, const StickMonitor &b) {
    return a.state_ == b.state_ && a.latched_ == b.latched_ && a.window_ == b.window_ &&
           a.clean_run_ == b.clean_run_ && a.generation_at_start_ == b.generation_at_start_ &&
           a.last_reason_.kind == b.last_reason_.kind &&
           a.last_reason_.axis == b.last_reason_.axis &&
           a.fault_reason_.kind == b.fault_reason_.kind &&
           a.fault_reason_.axis == b.fault_reason_.axis &&
           a.counters_.suspect_onsets == b.counters_.suspect_onsets &&
           a.counters_.faults == b.counters_.faults &&
           a.counters_.implausible == b.counters_.implausible &&
           a.counters_.xy_age_max_ms == b.counters_.xy_age_max_ms && a.has_first_ == b.has_first_ &&
           a.first_ms_ == b.first_ms_ && a.seen_seq_ == b.seen_seq_ &&
           a.changed_ms_ == b.changed_ms_ && a.started_ == b.started_;
  }
};
} // namespace hmi::stick

namespace {

using namespace hmi::stick;
using Peer = hmi::stick::StickMonitorTestPeer;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// Board 2's calibrated maxima (H 2971, V 2962, twist 2960 mV) give the spec's limits.
constexpr HighLimitsMv kBoard2Limits{3071.0f, 3062.0f, 3060.0f};
constexpr HighLimitsMv kIdealLimits{3150.0f, 3150.0f, 3150.0f};

// A plausible sample at rest on board 2, with the X/Y window sequences given.
RawSample rest(std::uint32_t seq_h, std::uint32_t seq_v) {
  return RawSample{.horizontal = {1507.0f, 1510.0f, seq_h},
                   .vertical = {1510.0f, 1513.0f, seq_v},
                   .twist_mean_mv = 1477.0f,
                   .twist_max_mv = 1490.0f,
                   .twist_reads = 8};
}
// The same, failing P1 on the vertical axis (a failed read).
RawSample failed_read(std::uint32_t seq) {
  RawSample s = rest(seq, seq);
  s.vertical.mean_mv = std::nullopt;
  return s;
}

void expect_reason(SampleReason want, SampleReason got) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(want.kind),
                          static_cast<std::uint8_t>(got.kind));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(want.axis),
                          static_cast<std::uint8_t>(got.axis));
}
void expect_state(MonitorState want, MonitorState got) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(want), static_cast<std::uint8_t>(got));
}
SampleReason classify_fresh(const RawSample &s, const HighLimitsMv &l) {
  return classify(s, l, false, false);
}

// The monitor on 35 ms fake cycles: each cycle a new X/Y window (sequence + 1) unless told.
struct Run {
  StickMonitor m;
  std::uint32_t now_ms = 0;
  std::uint32_t seq = 0;
  std::uint32_t generation = 0;

  void sample(const RawSample &s) {
    m.step(select_input(false, m.state()), s, kBoard2Limits, generation, now_ms);
    now_ms += 35;
  }
  void good() {
    ++seq;
    sample(rest(seq, seq));
  }
  void bad() {
    ++seq;
    sample(failed_read(seq));
  }
  void calibrating(bool on, const RawSample &s) {
    m.step(select_input(on, m.state()), s, kBoard2Limits, generation, now_ms);
    now_ms += 35;
  }
  // From power-on to OK: three plausible windows (rows 3, 13, 14).
  void to_ok() {
    good();
    good();
    good();
    expect_state(MonitorState::OK, m.state());
  }
  void to_fault() {
    to_ok();
    bad();
    bad();
    bad();
    expect_state(MonitorState::FAULT, m.state());
  }
};

} // namespace

// ---------------------------------------------------------------------------------------------
// The oracle (§8.1)
// ---------------------------------------------------------------------------------------------

namespace {
// C2 §3.5 transcribed again, independently of the header: (row, from, input, required true,
// required false, to, actions). Guards: P S T F C L V.
struct SpecRow {
  int row;
  MonitorState from;
  MonitorInput input;
  const char *need_true;
  const char *need_false;
  MonitorState to;
  std::array<MonitorAction, 2> actions;
};
using enum MonitorState;
using enum MonitorInput;
using enum MonitorAction;
const std::array<SpecRow, 26> kSpec{{
    {1, INIT, SAMPLE, "", "ST", INIT, {NONE, NONE}},
    {2, INIT, SAMPLE, "T", "S", FAULT, {LATCH_NO_SAMPLES, NONE}},
    {3, INIT, SAMPLE, "SP", "", RECOVERING, {PUSH_GOOD, NONE}},
    {4, INIT, SAMPLE, "S", "PF", SUSPECT, {PUSH_BAD, NOTE_SUSPECT}},
    {5, INIT, SAMPLE, "SF", "P", FAULT, {PUSH_BAD, LATCH}},
    {6, OK, SAMPLE, "P", "", OK, {PUSH_GOOD, NONE}},
    {7, OK, SAMPLE, "", "PF", SUSPECT, {PUSH_BAD, NOTE_SUSPECT}},
    {8, OK, SAMPLE, "F", "P", FAULT, {PUSH_BAD, LATCH}},
    {9, SUSPECT, SAMPLE, "P", "C", RECOVERING, {PUSH_GOOD, NONE}},
    {10, SUSPECT, SAMPLE, "PC", "", OK, {PUSH_GOOD, NONE}},
    {11, SUSPECT, SAMPLE, "", "PF", SUSPECT, {PUSH_BAD, NONE}},
    {12, SUSPECT, SAMPLE, "F", "P", FAULT, {PUSH_BAD, LATCH}},
    {13, RECOVERING, SAMPLE, "P", "C", RECOVERING, {PUSH_GOOD, NONE}},
    {14, RECOVERING, SAMPLE, "PC", "", OK, {PUSH_GOOD, NONE}},
    {15, RECOVERING, SAMPLE, "", "PF", SUSPECT, {PUSH_BAD, NOTE_SUSPECT}},
    {16, RECOVERING, SAMPLE, "F", "P", FAULT, {PUSH_BAD, LATCH}},
    {17, FAULT, SAMPLE, "", "", FAULT, {COUNT_KIND, NONE}},
    {18, INIT, CAL_START, "", "", CALIBRATING, {SUSPEND, NONE}},
    {19, OK, CAL_START, "", "", CALIBRATING, {SUSPEND, NONE}},
    {20, SUSPECT, CAL_START, "", "", CALIBRATING, {SUSPEND, NONE}},
    {21, RECOVERING, CAL_START, "", "", CALIBRATING, {SUSPEND, NONE}},
    {22, FAULT, CAL_START, "", "", CALIBRATING, {SUSPEND, NONE}},
    {23, CALIBRATING, SAMPLE, "", "", CALIBRATING, {NONE, NONE}},
    {24, CALIBRATING, CAL_END, "V", "", RECOVERING, {RESUME, UNLATCH}},
    {25, CALIBRATING, CAL_END, "L", "V", FAULT, {RESUME, NONE}},
    {26, CALIBRATING, CAL_END, "", "VL", RECOVERING, {RESUME, NONE}},
}};

GuardBits letters(const char *s) {
  GuardBits g = 0;
  for (; *s != '\0'; ++s) {
    switch (*s) {
    case 'P':
      g |= gbit(MonitorGuard::PLAUSIBLE);
      break;
    case 'S':
      g |= gbit(MonitorGuard::STARTED);
      break;
    case 'T':
      g |= gbit(MonitorGuard::START_TIMED_OUT);
      break;
    case 'F':
      g |= gbit(MonitorGuard::REACHES_FAULT);
      break;
    case 'C':
      g |= gbit(MonitorGuard::CLEAN);
      break;
    case 'L':
      g |= gbit(MonitorGuard::LATCHED);
      break;
    default: // 'V'
      g |= gbit(MonitorGuard::NEW_VALID_CAL);
      break;
    }
  }
  return g;
}

// The spec's row for a combination, or 0.
const SpecRow *spec_row(MonitorState s, MonitorInput in, GuardBits g) {
  const SpecRow *hit = nullptr;
  for (const SpecRow &r : kSpec) {
    const GuardBits t = letters(r.need_true);
    const GuardBits f = letters(r.need_false);
    if (r.from == s && r.input == in && (g & t) == t && (g & f) == 0) {
      TEST_ASSERT_NULL_MESSAGE(hit, "two spec rows match one combination");
      hit = &r;
    }
  }
  return hit;
}
} // namespace

TEST_CASE("STK-070 every state x input x 128 guard sets through the transition function: the "
          "table's row, or no change at all",
          "[stick][monitor]") {
  const SampleReason reason{SampleFault::HIGH, StickAxis::VERTICAL};
  for (std::size_t s = 0; s < kMonitorStateCount; ++s) {
    for (std::size_t in = 0; in < kMonitorInputCount; ++in) {
      for (unsigned g = 0; g < 128; ++g) {
        const auto state = static_cast<MonitorState>(s);
        const auto input = static_cast<MonitorInput>(in);
        const auto guards = static_cast<GuardBits>(g);
        const SpecRow *want = spec_row(state, input, guards);
        const MonitorDecision got = decide(state, input, guards);
        const MonitorDecision table = find_row(state, input, guards);
        TEST_ASSERT_EQUAL_INT(table.row, got.row); // code == table
        expect_state(table.to, got.to);
        TEST_ASSERT_TRUE(table.actions == got.actions);
        StickMonitor before;
        Peer::set_state(before, static_cast<std::uint8_t>(s));
        Peer::set_latched(before, (g & gbit(MonitorGuard::LATCHED)) != 0);
        StickMonitor after = before;
        after.transition(input, guards, reason, 7);
        if (want == nullptr) {
          TEST_ASSERT_EQUAL_INT(0, got.row);
          TEST_ASSERT_TRUE(
              Peer::same(after, before)); // state, latch, window, counters: all unchanged
          continue;
        }
        TEST_ASSERT_EQUAL_INT(want->row, got.row);
        expect_state(want->to, got.to);
        expect_state(want->to, after.state());
        TEST_ASSERT_TRUE(want->actions == got.actions);
      }
    }
  }
}

TEST_CASE("STK-071 table invariants: one row per listed combination, rows exclusive, a way out of "
          "every state but FAULT, FAULT left only by CAL_START, LATCH only into FAULT, UNLATCH "
          "only on row 24",
          "[stick][monitor]") {
  TEST_ASSERT_EQUAL_UINT(kSpec.size(), STICK_MONITOR_TRANSITIONS.size());
  for (std::size_t i = 0; i < kSpec.size(); ++i) {
    const SpecRow &w = kSpec[i];
    const MonitorTransition &t = STICK_MONITOR_TRANSITIONS[i];
    TEST_ASSERT_EQUAL_INT(w.row, t.row);
    expect_state(w.from, t.from);
    TEST_ASSERT_TRUE(w.input == t.input);
    TEST_ASSERT_EQUAL_UINT8(letters(w.need_true), t.guard.need_true);
    TEST_ASSERT_EQUAL_UINT8(letters(w.need_false), t.guard.need_false);
    expect_state(w.to, t.to);
    TEST_ASSERT_TRUE(w.actions == t.actions);
  }
  // The invariants hold at compile time (the header static_asserts them too).
  static_assert(monitor_detail::rows_numbered());
  static_assert(monitor_detail::rows_exclusive());
  static_assert(monitor_detail::every_non_fault_state_has_a_way_out());
  static_assert(monitor_detail::fault_left_only_by_cal_start());
  static_assert(monitor_detail::latch_only_into_fault());
  static_assert(monitor_detail::unlatch_only_on_row_24());
}

TEST_CASE("STK-072 a corrupted state 99 and a corrupted input 99: FAULT, latched, reason INTERNAL",
          "[stick][monitor]") {
  {
    StickMonitor m;
    Peer::set_state(m, 99);
    m.step(MonitorInput::SAMPLE, rest(1, 1), kBoard2Limits, 0, 0);
    expect_state(MonitorState::FAULT, m.state());
    TEST_ASSERT_TRUE(m.latched());
    expect_reason({SampleFault::INTERNAL, StickAxis::NONE}, m.fault_reason());
    TEST_ASSERT_EQUAL_UINT32(1, m.counters().faults);
    // decide() on the corrupted state changes nothing by itself (the caller screens it).
    for (std::size_t in = 0; in < kMonitorInputCount; ++in) {
      TEST_ASSERT_EQUAL_INT(
          0, decide(static_cast<MonitorState>(99), static_cast<MonitorInput>(in), 0).row);
    }
  }
  {
    StickMonitor m;
    m.step(static_cast<MonitorInput>(99), rest(1, 1), kBoard2Limits, 0, 0);
    expect_state(MonitorState::FAULT, m.state());
    TEST_ASSERT_TRUE(m.latched());
    expect_reason({SampleFault::INTERNAL, StickAxis::NONE}, m.fault_reason());
    TEST_ASSERT_EQUAL_INT(0, decide(MonitorState::OK, static_cast<MonitorInput>(99), 0).row);
  }
  {
    // The same rule for an action outside its enum (CS-TYP-06), which only a decision corrupted
    // in memory can carry: FAULT, latched, INTERNAL.
    StickMonitor m;
    Peer::set_state(m, static_cast<std::uint8_t>(MonitorState::OK));
    Peer::apply_raw_action(m, 99);
    expect_state(MonitorState::FAULT, m.state());
    TEST_ASSERT_TRUE(m.latched());
    expect_reason({SampleFault::INTERNAL, StickAxis::NONE}, m.fault_reason());
  }
}

// ---------------------------------------------------------------------------------------------
// Classification (§8.2). Board 2's calibration unless stated. "OK" = plausible.
// ---------------------------------------------------------------------------------------------

TEST_CASE("STK-073 each axis mean -0.001, 0, 6, cal min, centre, cal max: NEGATIVE; OK x5",
          "[stick][monitor]") {
  // Board 2: (min, centre, max) per axis.
  const std::array<std::array<float, 3>, 3> cal{
      {{11.0f, 1507.0f, 2971.0f}, {6.0f, 1510.0f, 2962.0f}, {10.0f, 1477.0f, 2960.0f}}};
  const std::array<StickAxis, 3> axes{StickAxis::HORIZONTAL, StickAxis::VERTICAL, StickAxis::TWIST};
  for (std::size_t a = 0; a < 3; ++a) {
    const std::array<float, 6> means{-0.001f, 0.0f, 6.0f, cal[a][0], cal[a][1], cal[a][2]};
    for (std::size_t i = 0; i < means.size(); ++i) {
      RawSample s = rest(1, 1);
      float *mean = a == 0   ? &*s.horizontal.mean_mv
                    : a == 1 ? &*s.vertical.mean_mv
                             : &*s.twist_mean_mv;
      float *max = a == 0 ? &s.horizontal.max_mv : a == 1 ? &s.vertical.max_mv : &s.twist_max_mv;
      *mean = means[i];
      *max = means[i];
      expect_reason(i == 0 ? SampleReason{SampleFault::NEGATIVE, axes[a]} : kPlausible,
                    classify_fresh(s, kBoard2Limits));
    }
  }
}

TEST_CASE("STK-074 max at the limit and 0.01 mV above: H 3071, V 3062, twist 3060: OK; HIGH",
          "[stick][monitor]") {
  TEST_ASSERT_EQUAL_FLOAT(3071.0f, high_limit_mv(2971.0f));
  TEST_ASSERT_EQUAL_FLOAT(3062.0f, high_limit_mv(2962.0f));
  TEST_ASSERT_EQUAL_FLOAT(3060.0f, high_limit_mv(2960.0f));
  RawSample s = rest(1, 1);
  s.horizontal.max_mv = 3071.0f;
  s.vertical.max_mv = 3062.0f;
  s.twist_max_mv = 3060.0f;
  expect_reason(kPlausible, classify_fresh(s, kBoard2Limits));
  RawSample h = s;
  h.horizontal.max_mv = 3071.01f;
  expect_reason({SampleFault::HIGH, StickAxis::HORIZONTAL}, classify_fresh(h, kBoard2Limits));
  RawSample v = s;
  v.vertical.max_mv = 3062.01f;
  expect_reason({SampleFault::HIGH, StickAxis::VERTICAL}, classify_fresh(v, kBoard2Limits));
  RawSample t = s;
  t.twist_max_mv = 3060.01f;
  expect_reason({SampleFault::HIGH, StickAxis::TWIST}, classify_fresh(t, kBoard2Limits));
}

TEST_CASE("STK-075 ideal defaults: 3150 / 3150.01 on each axis: OK; HIGH", "[stick][monitor]") {
  TEST_ASSERT_EQUAL_FLOAT(3150.0f, high_limit_mv(3300.0f)); // never calibrated: 0/1650/3300
  RawSample s = rest(1, 1);
  s.horizontal.max_mv = 3150.0f;
  s.vertical.max_mv = 3150.0f;
  s.twist_max_mv = 3150.0f;
  expect_reason(kPlausible, classify_fresh(s, kIdealLimits));
  RawSample h = s;
  h.horizontal.max_mv = 3150.01f;
  expect_reason({SampleFault::HIGH, StickAxis::HORIZONTAL}, classify_fresh(h, kIdealLimits));
  RawSample v = s;
  v.vertical.max_mv = 3150.01f;
  expect_reason({SampleFault::HIGH, StickAxis::VERTICAL}, classify_fresh(v, kIdealLimits));
  RawSample t = s;
  t.twist_max_mv = 3150.01f;
  expect_reason({SampleFault::HIGH, StickAxis::TWIST}, classify_fresh(t, kIdealLimits));
}

TEST_CASE("STK-076 NaN, +inf, -inf in each mean and each max: NAN (axis)", "[stick][monitor]") {
  const std::array<float, 3> bad{kNaN, kInf, -kInf};
  const std::array<StickAxis, 3> axes{StickAxis::HORIZONTAL, StickAxis::VERTICAL, StickAxis::TWIST};
  for (const float v : bad) {
    for (std::size_t a = 0; a < 3; ++a) {
      for (int which = 0; which < 2; ++which) { // 0 the mean, 1 the max
        RawSample s = rest(1, 1);
        float *mean = a == 0   ? &*s.horizontal.mean_mv
                      : a == 1 ? &*s.vertical.mean_mv
                               : &*s.twist_mean_mv;
        float *max = a == 0 ? &s.horizontal.max_mv : a == 1 ? &s.vertical.max_mv : &s.twist_max_mv;
        *(which == 0 ? mean : max) = v;
        expect_reason({SampleFault::NAN_VALUE, axes[a]}, classify_fresh(s, kBoard2Limits));
      }
    }
  }
}

TEST_CASE("STK-077 each of the 7 missing patterns: MISSING, the first missing axis",
          "[stick][monitor]") {
  for (unsigned mask = 1; mask < 8; ++mask) { // bit 0 H, 1 V, 2 twist
    RawSample s = rest(1, 1);
    if ((mask & 1U) != 0) {
      s.horizontal.mean_mv = std::nullopt;
    }
    if ((mask & 2U) != 0) {
      s.vertical.mean_mv = std::nullopt;
    }
    if ((mask & 4U) != 0) {
      s.twist_mean_mv = std::nullopt;
      s.twist_reads = 0;
    }
    const StickAxis first = (mask & 1U) != 0   ? StickAxis::HORIZONTAL
                            : (mask & 2U) != 0 ? StickAxis::VERTICAL
                                               : StickAxis::TWIST;
    expect_reason({SampleFault::MISSING, first}, classify_fresh(s, kBoard2Limits));
  }
}

TEST_CASE("STK-078 an X/Y mean below the limit with the window max above: HIGH",
          "[stick][monitor]") {
  RawSample h = rest(1, 1);
  h.horizontal.mean_mv = 3000.0f;
  h.horizontal.max_mv = 3100.0f;
  expect_reason({SampleFault::HIGH, StickAxis::HORIZONTAL}, classify_fresh(h, kBoard2Limits));
  RawSample v = rest(1, 1);
  v.vertical.mean_mv = 3000.0f;
  v.vertical.max_mv = 3100.0f;
  expect_reason({SampleFault::HIGH, StickAxis::VERTICAL}, classify_fresh(v, kBoard2Limits));
}

TEST_CASE("STK-079 twist 7 of 8 reads: TWIST_PARTIAL; 8 of 8 with one read above the limit: "
          "HIGH twist",
          "[stick][monitor]") {
  RawSample s = rest(1, 1);
  s.twist_reads = 7;
  expect_reason({SampleFault::TWIST_PARTIAL, StickAxis::TWIST}, classify_fresh(s, kBoard2Limits));
  s.twist_reads = 8;
  s.twist_max_mv = 3060.5f;
  expect_reason({SampleFault::HIGH, StickAxis::TWIST}, classify_fresh(s, kBoard2Limits));
}

TEST_CASE("STK-080 X unchanged 299 ms then 300 ms (Y fresh): OK, STALE H; the same for Y; a "
          "change resets; 2^32-1 -> 0 counts as a change",
          "[stick][monitor]") {
  // Through the monitor, which judges P5: in OK a plausible sample keeps OK, an implausible one
  // goes to SUSPECT and is stored as the last reason.
  for (int axis = 0; axis < 2; ++axis) {
    Run r;
    r.to_ok(); // windows 1..3 at t = 0, 35, 70
    const std::uint32_t frozen = r.seq;
    const std::uint32_t changed_at = r.now_ms - 35;
    auto sample_at = [&r, axis, frozen](std::uint32_t t, std::uint32_t moving) {
      r.now_ms = t;
      r.sample(axis == 0 ? rest(frozen, moving) : rest(moving, frozen));
    };
    sample_at(changed_at + 299, 100);
    expect_state(MonitorState::OK, r.m.state());
    sample_at(changed_at + 300, 101);
    expect_state(MonitorState::SUSPECT, r.m.state());
    expect_reason({SampleFault::STALE, axis == 0 ? StickAxis::HORIZONTAL : StickAxis::VERTICAL},
                  r.m.last_reason());
  }
  {
    // A change resets the age: X changes at 299 ms, then 299 ms later it is still fresh.
    Run r;
    r.to_ok();
    const std::uint32_t t0 = r.now_ms - 35;
    r.now_ms = t0 + 299;
    r.sample(rest(50, 50));
    r.now_ms = t0 + 299 + 299;
    r.sample(rest(50, 51));
    expect_state(MonitorState::OK, r.m.state());
  }
  {
    // 2^32 - 1 -> 0 is a change.
    Run r;
    r.to_ok();
    const std::uint32_t t0 = r.now_ms;
    r.now_ms = t0;
    r.sample(rest(0xFFFF'FFFFU, 0xFFFF'FFFFU));
    r.now_ms = t0 + 200;
    r.sample(rest(0, 0));
    r.now_ms = t0 + 450; // 250 ms after the wrap, 450 ms after the last other change
    r.sample(rest(0, 0));
    expect_state(MonitorState::OK, r.m.state());
  }
}

TEST_CASE("STK-081 two failures at once (missing V and HIGH twist): reason MISSING V (order)",
          "[stick][monitor]") {
  RawSample s = rest(1, 1);
  s.vertical.mean_mv = std::nullopt;
  s.twist_max_mv = 3200.0f;
  expect_reason({SampleFault::MISSING, StickAxis::VERTICAL}, classify_fresh(s, kBoard2Limits));
  // And the later checks in their order on one sample: NEGATIVE before HIGH before STALE before
  // TWIST_PARTIAL.
  RawSample t = rest(1, 1);
  t.twist_reads = 7;
  expect_reason({SampleFault::STALE, StickAxis::VERTICAL}, classify(t, kBoard2Limits, false, true));
  t.vertical.max_mv = 3100.0f;
  expect_reason({SampleFault::HIGH, StickAxis::VERTICAL}, classify(t, kBoard2Limits, true, true));
  t.twist_mean_mv = -1.0f;
  expect_reason({SampleFault::NEGATIVE, StickAxis::TWIST}, classify(t, kBoard2Limits, true, true));
}

// ---------------------------------------------------------------------------------------------
// The state machine over time (§8.3), on 35 ms fake cycles
// ---------------------------------------------------------------------------------------------

TEST_CASE("STK-082 boot: no window until 128 ms, then windows every 128 ms: INIT until the first "
          "X and Y windows, then RECOVERING, OK on the 3rd good sample",
          "[stick][monitor]") {
  StickMonitor m;
  for (std::uint32_t t = 0; t < 400; t += 35) {
    // Before the first window espp's mean reads 0 mV and the sequence is 0.
    const std::uint32_t seq = t / 128;
    RawSample s = rest(seq, seq);
    if (seq == 0) {
      s.horizontal = {0.0f, 0.0f, 0};
      s.vertical = {0.0f, 0.0f, 0};
    }
    m.step(select_input(false, m.state()), s, kBoard2Limits, 0, t);
    const MonitorState want = t < 128            ? MonitorState::INIT
                              : t < 140 + 2 * 35 ? MonitorState::RECOVERING
                                                 : MonitorState::OK;
    expect_state(want, m.state());
    TEST_ASSERT_TRUE((want == MonitorState::OK) == (m.health() == MonitorHealth::OK));
    if (want == MonitorState::INIT) {
      TEST_ASSERT_FALSE(keys_live(m.state())); // keys silent in INIT
    }
  }
}

TEST_CASE("STK-083 boot: no window at all: INIT until 999 ms; FAULT NO_SAMPLES on the first cycle "
          "at >= 1000 ms",
          "[stick][monitor]") {
  StickMonitor m;
  RawSample none = rest(0, 0);
  none.horizontal = {0.0f, 0.0f, 0};
  none.vertical = {0.0f, 0.0f, 0};
  for (std::uint32_t t = 0; t < 1000; t += 35) {
    m.step(MonitorInput::SAMPLE, none, kBoard2Limits, 0, t);
    expect_state(MonitorState::INIT, m.state());
  }
  m.step(MonitorInput::SAMPLE, none, kBoard2Limits, 0, 999);
  expect_state(MonitorState::INIT, m.state());
  m.step(MonitorInput::SAMPLE, none, kBoard2Limits, 0, 1000);
  expect_state(MonitorState::FAULT, m.state());
  TEST_ASSERT_TRUE(m.latched());
  expect_reason({SampleFault::NO_SAMPLES, StickAxis::NONE}, m.fault_reason());
}

TEST_CASE("STK-085 OK; implausible every cycle: withheld from the 1st, FAULT on the 3rd",
          "[stick][monitor]") {
  Run r;
  r.to_ok();
  r.bad();
  expect_state(MonitorState::SUSPECT, r.m.state());
  TEST_ASSERT_TRUE(r.m.health() == MonitorHealth::CHECK); // output 0 from the 1st
  r.bad();
  expect_state(MonitorState::SUSPECT, r.m.state());
  r.bad();
  expect_state(MonitorState::FAULT, r.m.state());
  TEST_ASSERT_TRUE(r.m.health() == MonitorHealth::FAULT);
  expect_reason({SampleFault::MISSING, StickAxis::VERTICAL}, r.m.fault_reason());
}

TEST_CASE("STK-086 OK; bad, good, bad, good, bad: FAULT on the 5th cycle", "[stick][monitor]") {
  Run r;
  r.to_ok();
  r.bad();
  expect_state(MonitorState::SUSPECT, r.m.state());
  r.good();
  expect_state(MonitorState::RECOVERING, r.m.state());
  r.bad();
  expect_state(MonitorState::SUSPECT, r.m.state());
  r.good();
  expect_state(MonitorState::RECOVERING, r.m.state());
  r.bad();
  expect_state(MonitorState::FAULT, r.m.state());
}

TEST_CASE("STK-087 bad at cycles 0, 15, 29: FAULT at 29; bad at 0, 15, 30: never FAULT",
          "[stick][monitor]") {
  for (const int last : {29, 30}) {
    Run r;
    r.to_ok();
    for (int c = 0; c <= 60; ++c) {
      if (c == 0 || c == 15 || c == last) {
        r.bad();
      } else {
        r.good();
      }
      const bool faulted = last == 29 && c >= 29;
      TEST_ASSERT_EQUAL(faulted, r.m.state() == MonitorState::FAULT);
    }
  }
}

TEST_CASE("STK-088 FAULT; 1000 good cycles, stick centred: still FAULT, withheld, keys silent",
          "[stick][monitor]") {
  Run r;
  r.to_fault();
  for (int i = 0; i < 1000; ++i) {
    r.good();
  }
  expect_state(MonitorState::FAULT, r.m.state());
  TEST_ASSERT_TRUE(r.m.health() == MonitorHealth::FAULT);
  TEST_ASSERT_FALSE(keys_live(r.m.state()));
  TEST_ASSERT_TRUE(r.m.latched());
}

TEST_CASE("STK-089 FAULT; CAL_START; 100 implausible samples; CAL_END with no new record: "
          "CALIBRATING with nothing counted, then FAULT",
          "[stick][monitor]") {
  Run r;
  r.to_fault();
  r.calibrating(true, rest(r.seq, r.seq));
  expect_state(MonitorState::CALIBRATING, r.m.state());
  const StickMonitor::Counters before = r.m.counters();
  const std::uint32_t window_before = r.m.window_bad();
  // Nothing counted: onsets, faults and implausible samples (xy_age_max_ms is a diagnostic of
  // the ADC producer and keeps tracking).
  auto same_counts = [&before](const StickMonitor::Counters &c) {
    return c.suspect_onsets == before.suspect_onsets && c.faults == before.faults &&
           c.implausible == before.implausible;
  };
  for (int i = 0; i < 100; ++i) {
    ++r.seq;
    r.calibrating(true, failed_read(r.seq));
    expect_state(MonitorState::CALIBRATING, r.m.state());
  }
  TEST_ASSERT_TRUE(same_counts(r.m.counters()));
  TEST_ASSERT_EQUAL_UINT32(window_before, r.m.window_bad());
  r.calibrating(false, rest(r.seq, r.seq)); // the run ends, the generation did not move
  expect_state(MonitorState::FAULT, r.m.state());
  TEST_ASSERT_TRUE(r.m.latched());
  TEST_ASSERT_EQUAL_UINT32(0, r.m.window_bad()); // RESUME emptied it
}

TEST_CASE("STK-090 FAULT; CAL_START; generation + 1; CAL_END: RECOVERING, window empty, latch "
          "clear; OK after 3 good",
          "[stick][monitor]") {
  Run r;
  r.to_fault();
  r.calibrating(true, rest(r.seq, r.seq));
  expect_state(MonitorState::CALIBRATING, r.m.state());
  ++r.generation; // a validated record was handed over during the run
  r.calibrating(false, rest(r.seq, r.seq));
  expect_state(MonitorState::RECOVERING, r.m.state());
  TEST_ASSERT_EQUAL_UINT32(0, r.m.window_bad());
  TEST_ASSERT_EQUAL_UINT32(0, r.m.clean_run());
  TEST_ASSERT_FALSE(r.m.latched());
  r.good();
  r.good();
  expect_state(MonitorState::RECOVERING, r.m.state());
  r.good();
  expect_state(MonitorState::OK, r.m.state());
}

TEST_CASE("STK-091 OK; CAL_START; CAL_END with no new record: RECOVERING (not FAULT), window empty",
          "[stick][monitor]") {
  Run r;
  r.to_ok();
  r.bad();
  r.good(); // one bad sample in the window, to see it emptied
  TEST_ASSERT_EQUAL_UINT32(1, r.m.window_bad());
  r.calibrating(true, rest(r.seq, r.seq));
  r.calibrating(false, rest(r.seq, r.seq));
  expect_state(MonitorState::RECOVERING, r.m.state());
  TEST_ASSERT_EQUAL_UINT32(0, r.m.window_bad());
  TEST_ASSERT_FALSE(r.m.latched());
}

TEST_CASE("STK-099 counters and reasons over a scripted mix: each as scripted, never down",
          "[stick][monitor]") {
  Run r;
  r.to_ok();
  StickMonitor::Counters last = r.m.counters();
  auto never_down = [&last](const StickMonitor::Counters &now) {
    TEST_ASSERT_TRUE(now.suspect_onsets >= last.suspect_onsets && now.faults >= last.faults);
    for (std::size_t k = 0; k < kSampleFaultCount; ++k) {
      TEST_ASSERT_TRUE(now.implausible[k] >= last.implausible[k]);
    }
    last = now;
  };
  // 1: one failed read (onset 1), recover.
  r.bad();
  never_down(r.m.counters());
  for (int i = 0; i < 40; ++i) { // also pushes the bad one out of the window
    r.good();
  }
  // 2: one HIGH horizontal (onset 2), then a NaN twist while SUSPECT (no new onset), recover.
  ++r.seq;
  RawSample high = rest(r.seq, r.seq);
  high.horizontal.max_mv = 3300.0f;
  r.sample(high);
  ++r.seq;
  RawSample nan = rest(r.seq, r.seq);
  nan.twist_max_mv = kNaN;
  r.sample(nan);
  expect_reason({SampleFault::NAN_VALUE, StickAxis::TWIST}, r.m.last_reason());
  never_down(r.m.counters());
  for (int i = 0; i < 40; ++i) {
    r.good();
  }
  // 3: twist partial three times in a row: onset 3, FAULT (TWIST_PARTIAL), then two more in
  // FAULT (counted by kind, no new onset, no new fault).
  for (int i = 0; i < 5; ++i) {
    ++r.seq;
    RawSample partial = rest(r.seq, r.seq);
    partial.twist_reads = 6;
    r.sample(partial);
    never_down(r.m.counters());
  }
  const StickMonitor::Counters c = r.m.counters();
  TEST_ASSERT_EQUAL_UINT32(3, c.suspect_onsets);
  TEST_ASSERT_EQUAL_UINT32(1, c.faults);
  TEST_ASSERT_EQUAL_UINT32(1, c.implausible[static_cast<std::size_t>(SampleFault::MISSING)]);
  TEST_ASSERT_EQUAL_UINT32(1, c.implausible[static_cast<std::size_t>(SampleFault::HIGH)]);
  TEST_ASSERT_EQUAL_UINT32(1, c.implausible[static_cast<std::size_t>(SampleFault::NAN_VALUE)]);
  TEST_ASSERT_EQUAL_UINT32(5, c.implausible[static_cast<std::size_t>(SampleFault::TWIST_PARTIAL)]);
  expect_reason({SampleFault::TWIST_PARTIAL, StickAxis::TWIST}, r.m.last_reason());
  expect_reason({SampleFault::TWIST_PARTIAL, StickAxis::TWIST}, r.m.fault_reason());
  // A good sample in FAULT counts nothing.
  r.good();
  TEST_ASSERT_EQUAL_UINT32(c.suspect_onsets, r.m.counters().suspect_onsets);
  TEST_ASSERT_EQUAL_UINT32(c.faults, r.m.counters().faults);
  TEST_ASSERT_TRUE(c.implausible == r.m.counters().implausible);
  // xy_age_max_ms: one window per 35 ms cycle all along, except the failed vertical read of
  // part 1, which tells nothing, so the vertical's age reached 35 ms there ...
  TEST_ASSERT_EQUAL_UINT32(35, c.xy_age_max_ms);
  // ... until X stops for four cycles: 140 ms.
  const std::uint32_t frozen = r.seq;
  for (int i = 0; i < 4; ++i) {
    ++r.seq;
    r.sample(rest(frozen, r.seq));
  }
  TEST_ASSERT_EQUAL_UINT32(140, r.m.counters().xy_age_max_ms);
}

TEST_CASE("STK-100 10,000 cycles of monitor + pipeline under the armed allocation guard: 0 "
          "allocations",
          "[stick][monitor]") {
  struct Io {
    std::uint32_t key = 0;
    std::optional<CalibrationMv> take_new_calibration() { return std::nullopt; }
    float smooth_twist_mv(float mv) { return mv; }
    void note_raw_mv(float, float, float) {}
    bool keys_silent() { return false; }
    bool swap() { return false; }
    bool invert_x() { return false; }
    bool invert_y() { return false; }
    int sensitivity() { return 5; }
    std::uint32_t joy_key() const { return key; }
    std::uint32_t remote_key() { return 0; }
    void set_joy_key(std::uint32_t k) { key = k; }
    void set_joy_flick(std::uint32_t) {}
    void show(const Position &) {}
    int drive_speed() { return 10; }
    bool stick_drives() { return true; }
    Permit output_permit(const Position &) const {
      return {.output = true, .button = true, .reason = HoldReason::NONE}; // allowed
    }
    bool button_passes() { return true; }
    bool button_pressed() { return false; }
    bool publish(const Command &, bool) { return true; }
  };
  const CalibrationMv board2{
      {11.0f, 1507.0f, 2971.0f}, {6.0f, 1510.0f, 2962.0f}, {10.0f, 1477.0f, 2960.0f}};
  StickPipeline pipeline(pipeline_config(board2, {1, 2, 3, 4}));
  StickMonitor monitor;
  unsigned hits = 0;
  unsigned published = 0;
  {
    Io io;
    host_test::NoAlloc scope;
    for (std::uint32_t i = 0; i < 10'000; ++i) {
      // A new window every 4th cycle; a failed read every 97th; a calibration now and then.
      const std::uint32_t seq = i / 4 + 1;
      const RawSample s = i % 97 == 0 ? failed_read(seq) : rest(seq, seq);
      const bool cal = (i / 500) % 7 == 3;
      monitor.step(select_input(cal, monitor.state()), s, kBoard2Limits, i / 3500, i * 35U);
      const RawReadsMv raw{s.horizontal.mean_mv, s.vertical.mean_mv, s.twist_mean_mv};
      published += pipeline.cycle(io, raw) ? 1U : 0U;
    }
    hits = scope.count();
  }
  TEST_ASSERT_EQUAL_UINT(0, hits);
  TEST_ASSERT_EQUAL_UINT(10'000, published); // a failed read publishes neutral (H9 fixed)
}

TEST_CASE("STK-102 health per state: OK only in OK, FAULT in FAULT, CHECK otherwise; keys live "
          "only in OK and RECOVERING; through the permit only OK allows (NOT_MONITORED withholds)",
          "[stick][monitor][permit][REQ-STK-27]") {
  const std::array<MonitorHealth, kMonitorStateCount> health{
      MonitorHealth::CHECK, MonitorHealth::OK,    MonitorHealth::CHECK,
      MonitorHealth::CHECK, MonitorHealth::FAULT, MonitorHealth::CHECK};
  const std::array<bool, kMonitorStateCount> keys{false, true, false, true, false, false};
  for (std::size_t s = 0; s < kMonitorStateCount; ++s) {
    const auto state = static_cast<MonitorState>(s); // INIT, OK, SUSPECT, RECOVERING, FAULT, CAL
    TEST_ASSERT_TRUE(health_of(state) == health[s]);
    TEST_ASSERT_EQUAL(keys[s], keys_live(state));
    StickMonitor m;
    Peer::set_state(m, static_cast<std::uint8_t>(s));
    TEST_ASSERT_TRUE(m.health() == health[s]);
  }
  // Through C1's permit (replaces C1's STK-067, E9): only OK allows; CHECK, FAULT and
  // NOT_MONITORED withhold (REQ-STK-27). The rest of STK-067 kept: the POST gate allows only
  // PASS, the motion guard only OK.
  const PermitInputs met{.gate_open = true,
                         .motion_guard_ok = true,
                         .calibrating = false,
                         .calibration_measured = true,
                         .post = PostGate::PASS,
                         .health = StickHealth::OK};
  const auto allowed_within_2s = [](const PermitInputs &in) {
    OutputPermit p;
    for (std::uint32_t t = 0; t < 2000; t += 33) {
      if (p.cycle(in, Position{0.0f, 0.0f, 0.0f}, 1000 + t).output) {
        return true;
      }
    }
    return false;
  };
  struct HealthCase {
    StickHealth health;
    bool allows;
    HoldReason reason;
  };
  for (const HealthCase &h :
       {HealthCase{StickHealth::OK, true, HoldReason::NONE},
        HealthCase{StickHealth::CHECK, false, HoldReason::STICK_CHECK},
        HealthCase{StickHealth::FAULT, false, HoldReason::STICK_FAULT},
        HealthCase{StickHealth::NOT_MONITORED, false, HoldReason::STICK_CHECK},
        HealthCase{static_cast<StickHealth>(9), false, HoldReason::STICK_CHECK}}) {
    PermitInputs in = met;
    in.health = h.health;
    TEST_ASSERT_EQUAL(h.allows, allowed_within_2s(in));
    OutputPermit p;
    TEST_ASSERT_TRUE(p.cycle(in, Position{0.0f, 0.0f, 0.0f}, 1000).reason ==
                     (h.allows ? HoldReason::CENTRE_FIRST : h.reason));
  }
  for (std::size_t s = 0; s < kMonitorStateCount; ++s) { // the monitor's health per state
    PermitInputs in = met;
    in.health = stick_health(health_of(static_cast<MonitorState>(s)));
    TEST_ASSERT_EQUAL(s == static_cast<std::size_t>(MonitorState::OK), allowed_within_2s(in));
  }
  for (const auto &[post, allows] :
       {std::pair{PostGate::NOT_RUN, false}, std::pair{PostGate::PENDING, false},
        std::pair{PostGate::PASS, true}, std::pair{PostGate::FAIL, false},
        std::pair{static_cast<PostGate>(4), false}, std::pair{static_cast<PostGate>(255), false}}) {
    PermitInputs in = met;
    in.post = post;
    TEST_ASSERT_EQUAL(allows, allowed_within_2s(in));
  }
  PermitInputs guard = met;
  guard.motion_guard_ok = false;
  TEST_ASSERT_FALSE(allowed_within_2s(guard));
  // §3.2's input choice.
  TEST_ASSERT_TRUE(select_input(true, MonitorState::OK) == MonitorInput::CAL_START);
  TEST_ASSERT_TRUE(select_input(true, MonitorState::CALIBRATING) == MonitorInput::SAMPLE);
  TEST_ASSERT_TRUE(select_input(false, MonitorState::CALIBRATING) == MonitorInput::CAL_END);
  TEST_ASSERT_TRUE(select_input(false, MonitorState::FAULT) == MonitorInput::SAMPLE);
}

// ---------------------------------------------------------------------------------------------
// The wiring (§3.7, REQ-STK-17, 20, 23, 27): main's AdcStickIo reduced to what the stick path
// does each cycle, on the real MonitorCycle, StickPipeline and OutputPermit
// ---------------------------------------------------------------------------------------------

namespace {

constexpr CalibrationMv kBoard2Cal{
    {11.0f, 1507.0f, 2971.0f}, {6.0f, 1510.0f, 2962.0f}, {10.0f, 1477.0f, 2960.0f}};
constexpr KeyCodes kKeys{.up = 1, .down = 2, .right = 3, .left = 4};

/// One ADC cycle's raw sample on board 2: the three values as means and maxima, fresh X/Y
/// windows (`seq`), twist 8 of 8.
RawSample at(float h, float v, float tw, std::uint32_t seq) {
  return RawSample{.horizontal = {h, h, seq},
                   .vertical = {v, v, seq},
                   .twist_mean_mv = tw,
                   .twist_max_mv = tw,
                   .twist_reads = 8};
}

/// What main's ADC task does, in its order (note_sample, note_reads, the pipeline), with fakes
/// only for the world around it: POST passed, the gate open, a measured calibration, the guard
/// OK unless a case changes them. 35 ms cycles on a fake clock.
struct Wired {
  MonitorCycle monitor;
  MonitorTelemetry telemetry;
  OutputPermit permit;
  StickPipeline pipeline{pipeline_config(kBoard2Cal, kKeys)};
  std::uint32_t now_ms = 5000;
  std::uint32_t seq = 0;
  std::uint32_t generation = 0;
  bool calibrating = false;
  bool gate_open = true;
  PostGate post = PostGate::PASS;
  bool button = false;
  std::uint32_t remote = 0;
  // the last cycle's outputs
  StickHealth health = StickHealth::NOT_MONITORED;
  bool published = false;
  Command command{};
  bool button_bit = false;
  std::uint32_t key = 0;
  std::uint32_t flick = 0;
  HoldReason reason = HoldReason::GATE_SHUT;

  Wired() { monitor.use_calibration(kBoard2Cal); }

  struct Io {
    Wired *w;
    std::optional<CalibrationMv> take_new_calibration() { return std::nullopt; }
    float smooth_twist_mv(float mv) { return mv; }
    void note_raw_mv(float, float, float) {}
    bool keys_silent() { return w->calibrating || !w->monitor.keys_live(); }
    bool swap() { return false; }
    bool invert_x() { return false; }
    bool invert_y() { return false; }
    int sensitivity() { return 9; }
    std::uint32_t joy_key() const { return w->key; }
    std::uint32_t remote_key() const { return w->remote; }
    void set_joy_key(std::uint32_t k) { w->key = k; }
    void set_joy_flick(std::uint32_t k) { w->flick = k; }
    void show(const Position &) {}
    int drive_speed() { return 10; }
    Permit output_permit(const Position &mounted) {
      const PermitInputs in{.gate_open = w->gate_open,
                            .motion_guard_ok = true,
                            .calibrating = w->calibrating,
                            .calibration_measured = true,
                            .post = w->post,
                            .health = w->health};
      const Permit p = w->permit.cycle(in, mounted, w->now_ms);
      w->reason = p.reason;
      return p;
    }
    bool button_passes() const { return post_passed(w->post); }
    bool button_pressed() const { return w->button; }
    bool publish(const Command &c, bool b) {
      w->command = c;
      w->button_bit = b;
      return true;
    }
  };

  /// One cycle on @p s; the clock moves 35 ms after it.
  void cycle(const RawSample &s) {
    health = monitor.step(calibrating, s, generation, now_ms, telemetry);
    const RawReadsMv reads{s.horizontal.mean_mv, s.vertical.mean_mv, s.twist_mean_mv};
    if (!(reads.horizontal_mv && reads.vertical_mv && reads.twist_mv)) {
      permit.invalid_cycle();
    }
    flick = 0;
    command = {};
    Io io{this};
    published = pipeline.cycle(io, reads);
    now_ms += 35;
  }
  void centred(int n = 1) {
    for (int i = 0; i < n; ++i) {
      cycle(at(1507.0f, 1510.0f, 1477.0f, ++seq));
    }
  }
  void forward(int n = 1) { // vertical at its calibrated min: full forward
    for (int i = 0; i < n; ++i) {
      cycle(at(1507.0f, 6.0f, 1477.0f, ++seq));
    }
  }
  void right(int n = 1) {
    for (int i = 0; i < n; ++i) {
      cycle(at(2971.0f, 1510.0f, 1477.0f, ++seq));
    }
  }
  /// From power-on: OK, then centred until the permit's latch is set (300 ms neutral).
  void to_driving() {
    centred(3);
    expect_state(MonitorState::OK, monitor.monitor().state());
    centred(10);
    TEST_ASSERT_TRUE(permit.latched());
  }
  [[nodiscard]] bool command_is_zero() const {
    return std::bit_cast<std::uint32_t>(command.x) == 0U &&
           std::bit_cast<std::uint32_t>(command.y) == 0U &&
           std::bit_cast<std::uint32_t>(command.twist) == 0U;
  }
  [[nodiscard]] MonitorState state() const { return monitor.monitor().state(); }
};

} // namespace

TEST_CASE("STK-084 OK, driving forward with the permit open; one implausible sample: that "
          "cycle +0.0 x3; SUSPECT, RECOVERING, OK after 3 good; then 0 until 300 ms neutral, "
          "then the stick's value",
          "[stick][monitor][wired][safety][REQ-STK-23]") {
  Wired w;
  w.to_driving();
  w.forward();
  TEST_ASSERT_EQUAL_FLOAT(1.0f, w.command.y);
  // One implausible sample on a valid cycle: the twist's max above its limit (3060 mV).
  RawSample spike = at(1507.0f, 6.0f, 1477.0f, ++w.seq);
  spike.twist_max_mv = 3200.0f;
  w.cycle(spike);
  TEST_ASSERT_TRUE(w.published && w.command_is_zero());
  expect_state(MonitorState::SUSPECT, w.state());
  TEST_ASSERT_TRUE(w.reason == HoldReason::STICK_CHECK);
  // Forward held: SUSPECT -> RECOVERING (1st good), RECOVERING (2nd), OK (3rd), all held at 0.
  w.forward();
  expect_state(MonitorState::RECOVERING, w.state());
  TEST_ASSERT_TRUE(w.command_is_zero());
  w.forward();
  expect_state(MonitorState::RECOVERING, w.state());
  TEST_ASSERT_TRUE(w.command_is_zero());
  w.forward();
  expect_state(MonitorState::OK, w.state());
  TEST_ASSERT_TRUE(w.command_is_zero()); // OK, but C1's latch was cleared: centre first
  TEST_ASSERT_TRUE(w.reason == HoldReason::CENTRE_FIRST);
  w.forward(20); // still forward: never resumes by itself
  TEST_ASSERT_TRUE(w.command_is_zero());
  // Centred: held for the cycles under 300 ms, allowed from the first at 300 ms or later.
  const std::uint32_t t0 = w.now_ms;
  while (w.now_ms - t0 < 300) {
    w.centred();
    TEST_ASSERT_FALSE(w.permit.latched());
  }
  w.centred();
  TEST_ASSERT_TRUE(w.permit.latched());
  w.forward();
  TEST_ASSERT_EQUAL_FLOAT(1.0f, w.command.y);
}

TEST_CASE("STK-092 keys: full right held in INIT, SUSPECT, FAULT and CALIBRATING gives joy_key 0, "
          "no flick; a remote key still sets joy_key",
          "[stick][monitor][wired][REQ-STK-20]") {
  Wired w; // INIT: before any window (the sequences still 0)
  w.cycle(at(2971.0f, 1510.0f, 1477.0f, 0));
  expect_state(MonitorState::INIT, w.state());
  TEST_ASSERT_EQUAL_UINT32(0, w.key);
  TEST_ASSERT_EQUAL_UINT32(0, w.flick);
  w.remote = kKeys.up;
  w.cycle(at(2971.0f, 1510.0f, 1477.0f, 0));
  TEST_ASSERT_EQUAL_UINT32(kKeys.up, w.key);
  w.remote = 0;
  // SUSPECT: OK first, then a NaN twist with the stick full right.
  w.right(3);
  expect_state(MonitorState::OK, w.state());
  TEST_ASSERT_EQUAL_UINT32(kKeys.right, w.key);
  RawSample nan = at(2971.0f, 1510.0f, 1477.0f, ++w.seq);
  nan.twist_mean_mv = std::numeric_limits<float>::quiet_NaN();
  w.cycle(nan);
  expect_state(MonitorState::SUSPECT, w.state());
  TEST_ASSERT_EQUAL_UINT32(0, w.key);
  TEST_ASSERT_EQUAL_UINT32(0, w.flick);
  // FAULT: two more.
  w.cycle(nan);
  w.cycle(nan);
  expect_state(MonitorState::FAULT, w.state());
  w.right(5);
  TEST_ASSERT_EQUAL_UINT32(0, w.key);
  TEST_ASSERT_EQUAL_UINT32(0, w.flick);
  w.remote = kKeys.left;
  w.right();
  TEST_ASSERT_EQUAL_UINT32(kKeys.left, w.key);
  w.remote = 0;
  // CALIBRATING.
  w.calibrating = true;
  w.right(3);
  expect_state(MonitorState::CALIBRATING, w.state());
  TEST_ASSERT_EQUAL_UINT32(0, w.key);
  TEST_ASSERT_EQUAL_UINT32(0, w.flick);
}

TEST_CASE("STK-093 keys: full right held through SUSPECT -> RECOVERING: 0 in SUSPECT; in "
          "RECOVERING the key re-engages with one flick",
          "[stick][monitor][wired][REQ-STK-20]") {
  Wired w;
  w.right(3);
  expect_state(MonitorState::OK, w.state());
  w.right();
  TEST_ASSERT_EQUAL_UINT32(kKeys.right, w.key);
  TEST_ASSERT_EQUAL_UINT32(0, w.flick); // held: no new flick
  RawSample high = at(2971.0f, 1510.0f, 1477.0f, ++w.seq);
  high.horizontal.max_mv = 3100.0f; // above board 2's horizontal limit, 3071
  w.cycle(high);
  expect_state(MonitorState::SUSPECT, w.state());
  TEST_ASSERT_EQUAL_UINT32(0, w.key);
  w.right();
  expect_state(MonitorState::RECOVERING, w.state());
  TEST_ASSERT_EQUAL_UINT32(kKeys.right, w.key);
  TEST_ASSERT_EQUAL_UINT32(kKeys.right, w.flick); // one flick
  w.right();
  TEST_ASSERT_EQUAL_UINT32(kKeys.right, w.key);
  TEST_ASSERT_EQUAL_UINT32(0, w.flick); // and only one
}

TEST_CASE("STK-094 the button pressed in each monitor state and on an invalid cycle: the XYTwist "
          "button bit is set every time",
          "[stick][monitor][wired][REQ-STK-20]") {
  Wired w;
  w.button = true;
  w.cycle(at(1507.0f, 1510.0f, 1477.0f, 0)); // INIT
  expect_state(MonitorState::INIT, w.state());
  TEST_ASSERT_TRUE(w.button_bit);
  w.centred(3); // RECOVERING, RECOVERING, OK
  expect_state(MonitorState::OK, w.state());
  TEST_ASSERT_TRUE(w.button_bit);
  RawSample failed = at(1507.0f, 1510.0f, 1477.0f, ++w.seq);
  failed.vertical.mean_mv = std::nullopt; // an invalid cycle: SUSPECT
  w.cycle(failed);
  expect_state(MonitorState::SUSPECT, w.state());
  TEST_ASSERT_TRUE(w.published && w.button_bit);
  w.centred();
  expect_state(MonitorState::RECOVERING, w.state());
  TEST_ASSERT_TRUE(w.button_bit);
  w.cycle(failed);
  w.cycle(failed); // the 3rd bad of the window: FAULT
  expect_state(MonitorState::FAULT, w.state());
  TEST_ASSERT_TRUE(w.button_bit);
  w.centred();
  TEST_ASSERT_TRUE(w.button_bit);
  w.calibrating = true;
  w.centred();
  expect_state(MonitorState::CALIBRATING, w.state());
  TEST_ASSERT_TRUE(w.button_bit);
  w.post = PostGate::PENDING; // before POST pass the bit is released (C3's REQ-STK-15)
  w.centred();
  TEST_ASSERT_FALSE(w.button_bit);
}

TEST_CASE("STK-096 6 mV drives full: OK, vertical 6 mV, the permit open after 300 ms neutral: y = "
          "+1.0 x the speed scale; state OK",
          "[stick][monitor][wired][safety]") {
  Wired w;
  w.to_driving();
  for (int i = 0; i < 30; ++i) {
    w.cycle(at(1507.0f, 6.0f, 1477.0f, ++w.seq));
    expect_state(MonitorState::OK, w.state());
    TEST_ASSERT_EQUAL_HEX32(std::bit_cast<std::uint32_t>(1.0f),
                            std::bit_cast<std::uint32_t>(w.command.y)); // drive speed 10: x 1.0
  }
}

TEST_CASE("STK-097 RESIDUAL (D2): each axis at 0 mV with the permit open: state OK; full left, "
          "full forward, full counter-clockwise",
          "[stick][monitor][wired][safety][residual]") {
  struct Axis {
    float h, v, tw;
    float Command::*field;
    float want;
  };
  const std::array<Axis, 3> axes{{{0.0f, 1510.0f, 1477.0f, &Command::x, -1.0f},
                                  {1507.0f, 0.0f, 1477.0f, &Command::y, 1.0f},
                                  {1507.0f, 1510.0f, 0.0f, &Command::twist, -1.0f}}};
  for (const Axis &a : axes) {
    Wired w;
    w.to_driving();
    for (int i = 0; i < 10; ++i) {
      w.cycle(at(a.h, a.v, a.tw, ++w.seq));
      expect_state(MonitorState::OK, w.state());
    }
    TEST_ASSERT_EQUAL_FLOAT(a.want, w.command.*a.field);
  }
}

TEST_CASE("STK-098 rail while driving: forward held, then horizontal 3300 mV for 1 s, then "
          "centred: deflected before; +0.0 from the first rail cycle; FAULT on the 3rd; 0 after "
          "centring (latched)",
          "[stick][monitor][wired][safety]") {
  Wired w;
  w.to_driving();
  w.forward(5);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, w.command.y);
  for (int i = 1; i * 35 <= 1000; ++i) {
    w.cycle(at(3300.0f, 6.0f, 1477.0f, ++w.seq));
    TEST_ASSERT_TRUE(w.published && w.command_is_zero());
    expect_state(i < 3 ? MonitorState::SUSPECT : MonitorState::FAULT, w.state());
  }
  expect_reason({SampleFault::HIGH, StickAxis::HORIZONTAL}, w.monitor.monitor().fault_reason());
  w.centred(100);
  expect_state(MonitorState::FAULT, w.state());
  TEST_ASSERT_TRUE(w.command_is_zero());
  w.forward(5);
  TEST_ASSERT_TRUE(w.command_is_zero());
  TEST_ASSERT_TRUE(w.reason == HoldReason::STICK_FAULT);
}

TEST_CASE("STK-101 the hold reason with the monitor writing stick health: each monitor state "
          "with each other permit condition, first in REQ-STK-13's order",
          "[stick][monitor][wired][REQ-STK-13][REQ-STK-27]") {
  for (std::size_t s = 0; s < kMonitorStateCount; ++s) {
    const auto state = static_cast<MonitorState>(s);
    StickMonitor m;
    Peer::set_state(m, static_cast<std::uint8_t>(s));
    const StickHealth health = stick_health(m.health());
    const bool calibrating = state == MonitorState::CALIBRATING; // §3.2: they go together
    for (int failing = 0; failing <= 5; ++failing) {             // 0: none of conditions 1-5 fails
      PermitInputs in{.gate_open = failing != 1,
                      .motion_guard_ok = failing != 2,
                      .calibrating = calibrating || failing == 3,
                      .calibration_measured = failing != 4,
                      .post = failing == 5 ? PostGate::PENDING : PostGate::PASS,
                      .health = health};
      HoldReason want = HoldReason::CENTRE_FIRST; // a fresh permit, stick forward
      if (failing == 1) {
        want = HoldReason::GATE_SHUT;
      } else if (failing == 2) {
        want = HoldReason::MOTION_GUARD;
      } else if (in.calibrating) {
        want = HoldReason::CALIBRATING; // CALIBRATING's hold is C1's, before the stick reasons
      } else if (failing == 4) {
        want = HoldReason::NOT_CALIBRATED;
      } else if (failing == 5) {
        want = HoldReason::POST_NOT_PASSED;
      } else if (state == MonitorState::FAULT) {
        want = HoldReason::STICK_FAULT;
      } else if (state != MonitorState::OK) { // INIT, SUSPECT, RECOVERING
        want = HoldReason::STICK_CHECK;
      }
      OutputPermit p;
      const Permit got = p.cycle(in, Position{0.0f, 1.0f, 0.0f}, 1000);
      TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(want),
                              static_cast<std::uint8_t>(got.reason));
      TEST_ASSERT_FALSE(got.output);
    }
  }
}
