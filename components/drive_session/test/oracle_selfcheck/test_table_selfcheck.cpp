// L1 host self-check of drive_session_table.hpp: the TABLE's own invariants,
// re-checked at run time by brute force over every valid (phase, hidden, env).
// It does not test any state machine implementation; that is the oracle test,
// written by another agent against this table (TS-UNIT-08).

#include "drive_session_table.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" {
#include "unity.h"
}

namespace ds = hmi::drive_session;
using ds::Env;
using ds::GuardMask;
using ds::Input;
using ds::MibState;
using ds::Phase;
using ds::Screen;

namespace {

constexpr std::array kMib{MibState::INITIALIZING, MibState::IDLE, MibState::ENABLED,
                          MibState::OTHER};
constexpr std::array kScreens{Screen::BOOT, Screen::LOCKED, Screen::DRIVE, Screen::SEAT,
                              Screen::OTHER};

// Calls f(env_mask) for every distinct Env.
template <typename F> void for_each_env(F &&f) {
  for (bool link : {false, true}) {
    for (MibState mib : kMib) {
      for (Screen s : kScreens) {
        for (bool menu : {false, true}) {
          for (unsigned t = 0; t < 8; ++t) {
            const Env e{link, mib, s, menu, (t & 1u) != 0, (t & 2u) != 0, (t & 4u) != 0};
            f(ds::env_guards(e));
          }
        }
      }
    }
  }
}

constexpr unsigned kHiddenCombos = 1u << 5;

// The hidden guards are bits 10..14; spread a 5-bit index over them.
GuardMask hidden_from_index(unsigned i) {
  GuardMask m = 0;
  unsigned k = 0;
  for (unsigned b = 0; b < ds::kGuardCount; ++b) {
    const GuardMask v = GuardMask{1} << b;
    if ((v & ds::kHiddenGuards) != 0) {
      if (((i >> k) & 1u) != 0) {
        m |= v;
      }
      ++k;
    }
  }
  return m;
}

bool precondition_holds(Input in, Phase p, GuardMask guards) {
  const auto &pre = ds::INPUT_PRECONDITIONS[static_cast<std::size_t>(in)];
  return ds::in(pre.phases, p) && ds::matches(pre.guard, guards);
}

// Calls f(phase, hidden, input, guards) for every combination the oracle drives.
template <typename F> void for_each_valid(F &&f) {
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    for (unsigned hi = 0; hi < kHiddenCombos; ++hi) {
      const GuardMask hidden = hidden_from_index(hi);
      if (!ds::hidden_valid(p, hidden)) {
        continue;
      }
      for_each_env([&](GuardMask env) {
        const GuardMask guards = env | hidden;
        for (std::size_t ii = 0; ii < ds::kInputCount; ++ii) {
          const auto in = static_cast<Input>(ii);
          if (precondition_holds(in, p, guards)) {
            f(p, hidden, in, guards);
          }
        }
      });
    }
  }
}

} // namespace

void setUp() {}
void tearDown() {}

static void test_counts() {
  TEST_ASSERT_EQUAL_size_t(41, ds::TRANSITIONS.size());
  TEST_ASSERT_EQUAL_size_t(ds::kTransitionCount, ds::TRANSITIONS.size());
  TEST_ASSERT_EQUAL_size_t(11, ds::HOLD_TRANSITIONS.size());
  TEST_ASSERT_EQUAL_size_t(ds::kPhaseCount, ds::PHASE_INVARIANTS.size());
  TEST_ASSERT_EQUAL_size_t(ds::kInputCount, ds::INPUT_PRECONDITIONS.size());
  TEST_ASSERT_EQUAL_size_t(4, ds::TICK_SEQUENCE.size());
  TEST_ASSERT_EQUAL_size_t(0, ds::KNOWN_GAPS.size());
  TEST_ASSERT_EQUAL_size_t(5, ds::GATE_TRIGGERS.size());
}

static void test_at_most_one_row() {
  unsigned long combos = 0;
  unsigned long over = 0;
  for_each_valid([&](Phase p, GuardMask, Input in, GuardMask guards) {
    ++combos;
    int hits = 0;
    for (const auto &t : ds::TRANSITIONS) {
      if (t.from == p && t.input == in && ds::matches(t.guard, guards)) {
        ++hits;
      }
    }
    if (hits > 1) {
      ++over;
    }
  });
  TEST_ASSERT_TRUE(combos > 0);
  TEST_ASSERT_EQUAL_UINT32(0, over);
}

static void test_every_row_reachable() {
  std::array<bool, ds::kTransitionCount> hit{};
  for_each_valid([&](Phase p, GuardMask, Input in, GuardMask guards) {
    const std::size_t r = ds::find_row(p, in, guards);
    if (r < hit.size()) {
      hit[r] = true;
    }
  });
  for (std::size_t i = 0; i < hit.size(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(hit[i], ds::TRANSITIONS[i].code.data());
  }
}

static void test_rows_keep_invariants() {
  unsigned long bad = 0;
  for_each_valid([&](Phase p, GuardMask hidden, Input in, GuardMask guards) {
    const std::size_t r = ds::find_row(p, in, guards);
    if (r >= ds::TRANSITIONS.size()) {
      return;
    }
    const auto &t = ds::TRANSITIONS[r];
    if (!ds::hidden_valid(t.to, ds::apply(t.actions, hidden))) {
      ++bad;
    }
  });
  TEST_ASSERT_EQUAL_UINT32(0, bad);
}

static void test_every_action_and_input_used() {
  std::array<bool, ds::kActionCount> act{};
  std::array<bool, ds::kInputCount> inp{};
  for (const auto &t : ds::TRANSITIONS) {
    inp[static_cast<std::size_t>(t.input)] = true;
    for (auto a : t.actions) {
      act[static_cast<std::size_t>(a)] = true;
    }
  }
  for (std::size_t a = 1; a < act.size(); ++a) {
    TEST_ASSERT_TRUE(act[a]);
  }
  for (bool b : inp) {
    TEST_ASSERT_TRUE(b);
  }
  // Each effect-bearing action appears once in ACTION_EFFECTS.
  for (std::size_t i = 0; i < ds::ACTION_EFFECTS.size(); ++i) {
    for (std::size_t j = i + 1; j < ds::ACTION_EFFECTS.size(); ++j) {
      TEST_ASSERT_TRUE(ds::ACTION_EFFECTS[i].action != ds::ACTION_EFFECTS[j].action);
    }
    TEST_ASSERT_TRUE((ds::ACTION_EFFECTS[i].sets | ds::ACTION_EFFECTS[i].clears) != 0);
  }
}

static void test_way_out() {
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    if (p == ds::kSafePhase) {
      continue;
    }
    TEST_ASSERT_TRUE(ds::has_way_out(p) || ds::is_known_gap(p));
  }
}

static void test_tick_sequence() {
  TEST_ASSERT_TRUE(ds::TICK_SEQUENCE[0] == Input::TICK_FOLLOW);
  TEST_ASSERT_TRUE(ds::TICK_SEQUENCE[1] == Input::TICK_EXIT_DUE);
  TEST_ASSERT_TRUE(ds::TICK_SEQUENCE[2] == Input::TICK_WARN_DUE);
  TEST_ASSERT_TRUE(ds::TICK_SEQUENCE[3] == Input::TICK_GIVEUP_DUE);
}

static void test_hold_poll_complete() {
  using ds::HoldInput;
  using ds::HoldState;
  for (HoldState s : {HoldState::IDLE, HoldState::FILLING}) {
    for (unsigned m = 0; m < 16; ++m) {
      int hits = 0;
      for (const auto &t : ds::HOLD_TRANSITIONS) {
        if (t.from == s && t.input == HoldInput::POLL &&
            (m & t.guard.need_true) == t.guard.need_true && (m & t.guard.need_false) == 0) {
          ++hits;
        }
      }
      TEST_ASSERT_EQUAL_INT(1, hits);
    }
  }
}

static void test_stick_gate() {
  int open = 0;
  for (bool locked : {false, true}) {
    for (Screen s : kScreens) {
      for (bool menu : {false, true}) {
        open += ds::stick_drives(locked, s, menu) ? 1 : 0;
      }
    }
  }
  TEST_ASSERT_EQUAL_INT(1, open);
  TEST_ASSERT_TRUE(ds::stick_drives(false, Screen::DRIVE, false));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ds::stick_scale(true, true, 0.5f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ds::stick_scale(false, false, 0.5f));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, ds::stick_scale(false, true, 0.5f));
}

static void test_rows_cite_code() {
  for (const auto &t : ds::TRANSITIONS) {
    TEST_ASSERT_TRUE(t.code.find("frag_") != std::string_view::npos);
    TEST_ASSERT_TRUE(t.code.find("orig ") != std::string_view::npos);
  }
  for (const auto &t : ds::HOLD_TRANSITIONS) {
    TEST_ASSERT_TRUE(t.code.find("orig ") != std::string_view::npos);
  }
  for (const auto &g : ds::GATE_TRIGGERS) {
    TEST_ASSERT_TRUE(g.code.find("orig ") != std::string_view::npos);
  }
}

static void test_phase_of() {
  TEST_ASSERT_TRUE(ds::phase_of({true, false, false, false, false}) == Phase::LOCKED);
  TEST_ASSERT_TRUE(ds::phase_of({true, true, false, false, false}) == Phase::ASKING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, true, false, false}) == Phase::UNLOCKING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, false, false, false}) == Phase::DRIVING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, true, true, true}) == Phase::EXITING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, false, false, true}) == Phase::EXIT_REFUSED);
}

int main() {
  UNITY_BEGIN();
  UnityDefaultTestRun(test_counts, "DSO-001 the tables have their declared sizes", __LINE__);
  UnityDefaultTestRun(test_at_most_one_row, "DSO-002 no valid combination matches two rows",
                      __LINE__);
  UnityDefaultTestRun(test_every_row_reachable, "DSO-003 every row matches some valid combination",
                      __LINE__);
  UnityDefaultTestRun(test_rows_keep_invariants, "DSO-004 every row lands in a valid target state",
                      __LINE__);
  UnityDefaultTestRun(test_every_action_and_input_used, "DSO-005 every Action and Input is used",
                      __LINE__);
  UnityDefaultTestRun(test_way_out, "DSO-006 every non-safe phase has a way out or a known gap",
                      __LINE__);
  UnityDefaultTestRun(test_tick_sequence, "DSO-007 a tick runs follow-state before the deadlines",
                      __LINE__);
  UnityDefaultTestRun(test_hold_poll_complete, "DSO-008 every hold poll combination has one row",
                      __LINE__);
  UnityDefaultTestRun(test_stick_gate, "DSO-009 the stick gate opens in exactly one case",
                      __LINE__);
  UnityDefaultTestRun(test_rows_cite_code, "DSO-010 every row cites its code", __LINE__);
  UnityDefaultTestRun(test_phase_of, "DSO-011 phase_of maps the code variables", __LINE__);
  return UNITY_END();
}
