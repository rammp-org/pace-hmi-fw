// L1 tests of components/joystick_cal's run model (cal_run.hpp): the transition-table oracle
// (TS-UNIT-08), the table's own invariants, and the model driven directly. The same run seen
// through main's LVGL view is pinned by the characterisation cases in test_joystick_cal.cpp.

#include <array>
#include <cstddef>
#include <limits>
#include <string>

#include "cal_run.hpp"
#include "test_case.hpp"

namespace {

using hmi::cal::Action;
using hmi::cal::CalibrationRun;
using hmi::cal::Decision;
using hmi::cal::Guard;
using hmi::cal::Input;
using hmi::cal::kTransitions;
using hmi::cal::Phase;
using hmi::cal::Sample;

constexpr std::array<Phase, hmi::cal::kPhaseCount> kPhases{
    Phase::IDLE, Phase::REST, Phase::DIRECTION, Phase::RELEASE, Phase::RESULT};
constexpr std::array<Input, hmi::cal::kInputCount> kInputs{Input::START, Input::CANCEL,
                                                           Input::TICK};
constexpr std::array<Guard, hmi::cal::kGuardCount> kGuards{
    Guard::NONE,     Guard::WAITING,      Guard::TAKEN,     Guard::LAST_TAKEN,
    Guard::RELEASED, Guard::RELEASED_BAD, Guard::TIMED_OUT, Guard::RESULT_OVER};

constexpr Sample kRest{1507.0f, 1510.0f, 1477.0f};
constexpr std::array<Sample, 6> kEnds{{{11.0f, 1510.0f, 1477.0f},
                                       {2971.0f, 1510.0f, 1477.0f},
                                       {1507.0f, 6.0f, 1477.0f},
                                       {1507.0f, 2962.0f, 1477.0f},
                                       {1507.0f, 1510.0f, 2960.0f},
                                       {1507.0f, 1510.0f, 10.0f}}};

int count_rows(Phase from, Input input, Guard guard) {
  int n = 0;
  for (const auto &t : kTransitions) {
    n += (t.from == from && t.input == input && t.guard == guard) ? 1 : 0;
  }
  return n;
}

// `n` ticks at `mv`; returns the last action and counts the non-NONE ones in `acted`.
Action ticks(CalibrationRun &run, const Sample &mv, int n, int *acted = nullptr) {
  Action last = Action::NONE;
  for (int i = 0; i < n; ++i) {
    last = run.handle(Input::TICK, mv);
    if (acted != nullptr && last != Action::NONE) {
      ++*acted;
    }
  }
  return last;
}

} // namespace

TEST_CASE("CAL-301 decide() matches the transition table for every phase, input and guard",
          "[cal][run][oracle]") {
  int listed = 0;
  for (const Phase from : kPhases) {
    for (const Input input : kInputs) {
      for (const Guard guard : kGuards) {
        const Decision d = hmi::cal::decide(from, input, guard);
        Phase to = from;
        Action action = Action::NONE;
        for (const auto &t : kTransitions) {
          if (t.from == from && t.input == input && t.guard == guard) {
            to = t.to;
            action = t.action;
            ++listed;
          }
        }
        const std::string where = std::to_string(static_cast<int>(from)) + "/" +
                                  std::to_string(static_cast<int>(input)) + "/" +
                                  std::to_string(static_cast<int>(guard));
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(to), static_cast<int>(d.to), where.c_str());
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(action), static_cast<int>(d.action),
                                      where.c_str());
      }
    }
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kTransitions.size()), listed);
}

TEST_CASE("CAL-302 the table has one row per combination and a way out of every running phase",
          "[cal][run][oracle]") {
  for (const auto &t : kTransitions) {
    TEST_ASSERT_EQUAL_INT(1, count_rows(t.from, t.input, t.guard));
    if (t.input != Input::TICK) {
      TEST_ASSERT_TRUE(t.guard == Guard::NONE); // START and CANCEL carry no guard
    }
  }
  for (const Phase p : {Phase::REST, Phase::DIRECTION, Phase::RELEASE}) {
    TEST_ASSERT_EQUAL_INT(1, count_rows(p, Input::CANCEL, Guard::NONE));
    TEST_ASSERT_EQUAL_INT(1, count_rows(p, Input::TICK, Guard::TIMED_OUT));
    TEST_ASSERT_EQUAL_INT(0, count_rows(p, Input::START, Guard::NONE)); // never restarted
  }
  TEST_ASSERT_EQUAL_INT(1, count_rows(Phase::RESULT, Input::TICK, Guard::RESULT_OVER));
  // a run only ends in RESULT, and only BEGIN starts one
  for (const auto &t : kTransitions) {
    if (t.action == Action::CANCEL || t.action == Action::TIME_OUT ||
        t.action == Action::COMPLETE || t.action == Action::REJECT) {
      TEST_ASSERT_TRUE(t.to == Phase::RESULT);
    }
    TEST_ASSERT_EQUAL(t.action == Action::BEGIN, t.to == Phase::REST && t.from != Phase::REST);
  }
}

TEST_CASE("CAL-303 the model runs board 2's calibration step by step to COMPLETE", "[cal][run]") {
  CalibrationRun run({});
  TEST_ASSERT_TRUE(run.phase() == Phase::IDLE);
  TEST_ASSERT_TRUE(run.handle(Input::START) == Action::BEGIN);
  TEST_ASSERT_TRUE(run.running());
  TEST_ASSERT_EQUAL_STRING("Step 1 of 8\nLet go of the joystick and keep still",
                           run.prompt().c_str());
  int acted = 0;
  TEST_ASSERT_TRUE(ticks(run, kRest, 74, &acted) == Action::NONE);
  TEST_ASSERT_EQUAL_INT(0, acted);
  TEST_ASSERT_TRUE(run.handle(Input::TICK, kRest) == Action::NEXT);
  for (std::size_t d = 0; d < kEnds.size(); ++d) {
    TEST_ASSERT_TRUE(run.phase() == Phase::DIRECTION);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(d) + 2, run.step());
    TEST_ASSERT_TRUE(ticks(run, kEnds[d], 29) == Action::NONE);
    TEST_ASSERT_TRUE(run.handle(Input::TICK, kEnds[d]) == Action::NEXT);
  }
  TEST_ASSERT_TRUE(run.phase() == Phase::RELEASE);
  TEST_ASSERT_EQUAL_STRING("Step 8 of 8\nLet go of the joystick", run.prompt().c_str());
  TEST_ASSERT_TRUE(ticks(run, kRest, 29) == Action::NONE);
  TEST_ASSERT_TRUE(run.handle(Input::TICK, kRest) == Action::COMPLETE);
  TEST_ASSERT_TRUE(run.phase() == Phase::RESULT);
  TEST_ASSERT_FALSE(run.running());
  const hmi::cal::Record &r = run.record();
  TEST_ASSERT_EQUAL_STRING("horizontal 11/1507/2971, vertical 6/1510/2962, twist 10/1477/2960 mV",
                           hmi::cal::describe(r).c_str());
  TEST_ASSERT_TRUE(ticks(run, kRest, 89) == Action::NONE);
  TEST_ASSERT_TRUE(run.handle(Input::TICK, kRest) == Action::SHOW_IDLE);
  TEST_ASSERT_TRUE(run.phase() == Phase::IDLE);
  TEST_ASSERT_TRUE(run.handle(Input::TICK, kRest) == Action::PAUSE);
}

TEST_CASE("CAL-304 a step that is never taken times out on its 601st tick, reporting its step",
          "[cal][run]") {
  CalibrationRun run({});
  (void)run.handle(Input::START);
  (void)ticks(run, kRest, 75);
  TEST_ASSERT_EQUAL_INT(2, run.step());
  TEST_ASSERT_TRUE(ticks(run, kRest, 600) == Action::NONE);
  TEST_ASSERT_TRUE(run.handle(Input::TICK, kRest) == Action::TIME_OUT);
  TEST_ASSERT_EQUAL_INT(2, run.from_step());
  TEST_ASSERT_TRUE(run.phase() == Phase::RESULT);
}

TEST_CASE("CAL-305 CANCEL ends a run in any running phase and does nothing otherwise",
          "[cal][run]") {
  CalibrationRun run({});
  TEST_ASSERT_TRUE(run.handle(Input::CANCEL) == Action::NONE);
  TEST_ASSERT_TRUE(run.phase() == Phase::IDLE);
  for (int stage = 0; stage < 3; ++stage) {
    (void)run.handle(Input::START);
    if (stage >= 1) {
      (void)ticks(run, kRest, 75);
    }
    if (stage >= 2) {
      for (const Sample &e : kEnds) {
        (void)ticks(run, e, 30);
      }
    }
    TEST_ASSERT_TRUE(run.running());
    TEST_ASSERT_TRUE(run.handle(Input::CANCEL) == Action::CANCEL);
    TEST_ASSERT_TRUE(run.phase() == Phase::RESULT);
    TEST_ASSERT_TRUE(run.handle(Input::CANCEL) == Action::NONE);
    TEST_ASSERT_TRUE(run.phase() == Phase::RESULT);
  }
}

TEST_CASE("CAL-306 HAZARD a NaN sample counts as held at any end; the run then ends in REJECT",
          "[cal][run][hazard]") {
  constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
  CalibrationRun run({});
  (void)run.handle(Input::START);
  (void)ticks(run, kRest, 75);
  // NaN on the horizontal axis is "pushed fully" left: the comparison with the limit fails.
  TEST_ASSERT_TRUE(ticks(run, {kNan, 1510.0f, 1477.0f}, 30) == Action::NEXT);
  TEST_ASSERT_EQUAL_INT(3, run.step());
  for (std::size_t d = 1; d < kEnds.size(); ++d) {
    (void)ticks(run, kEnds[d], 30);
  }
  TEST_ASSERT_TRUE(ticks(run, kRest, 30) == Action::REJECT);
  TEST_ASSERT_TRUE(run.phase() == Phase::RESULT);
  TEST_ASSERT_FALSE(hmi::cal::plausible(run.record()));
}

TEST_CASE("CAL-307 START while the result is up begins a fresh run with an empty record",
          "[cal][run]") {
  CalibrationRun run({});
  (void)run.handle(Input::START);
  (void)ticks(run, kRest, 75);
  TEST_ASSERT_TRUE(run.handle(Input::CANCEL) == Action::CANCEL);
  TEST_ASSERT_TRUE(run.handle(Input::START) == Action::BEGIN);
  TEST_ASSERT_EQUAL_INT(1, run.step());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, run.record()[0].center_mv);
  TEST_ASSERT_TRUE(ticks(run, kRest, 74) == Action::NONE);
  TEST_ASSERT_TRUE(run.handle(Input::TICK, kRest) == Action::NEXT);
}
