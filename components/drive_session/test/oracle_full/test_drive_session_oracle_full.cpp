// L1 host app for drive_session, on demand: the full-product transition-table oracle
// (TS-UNIT-08), DRV-001..012. Moved out of CI by the owner's decision G2
// (docs/plans/hazard-decisions.md; hazard-c1-spec.md E5): with the hazard fixes' guard bits it
// grows past the 30 s budget of an L1 app (TS-UNIT-09). CI runs the by-input oracle
// (../oracle_by_input, DRV-101..) instead; `make equivalence` and `make mutants` there are the
// evidence that the by-input oracle is not weaker. Run this one with `make full` here.
//
// The oracle drives DriveSession::step() through every (phase, hidden mask, env, input) and
// compares it with the table (drive_session_table.hpp): where find_row() finds a row, the phase
// must become the row's `to`, the hidden mask apply(row.actions, hidden), and the actions must
// be the row's, in order; where it finds none, nothing may change and nothing may be returned.
// It drives all hidden masks, not only the ones the phase invariants allow, and every input
// in every phase, not only the ones INPUT_PRECONDITIONS allows: the implementation follows the
// row guards literally, so it must agree with find_row everywhere. The contract subset (valid
// hidden mask and precondition) is counted separately and printed.

#include "drive_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "test_case.hpp"

namespace hmi::drive_session {
// Test-only access to the session's state (declared a friend in drive_session.hpp).
struct DriveSessionTestPeer {
  static void set(DriveSession &s, Phase p, GuardMask hidden) {
    s.phase_ = p;
    s.hidden_ = hidden;
  }
  static void perform_one(DriveSession &s, Action a, Actions &out) {
    Actions acts{};
    acts[0] = a;
    s.perform(DriveSession::go(s.phase_, acts), out);
  }
};
} // namespace hmi::drive_session

namespace {

namespace ds = hmi::drive_session;
using ds::Action;
using ds::Actions;
using ds::DriveSession;
using ds::DriveSessionTestPeer;
using ds::Env;
using ds::GuardMask;
using ds::Input;
using ds::MibState;
using ds::Phase;
using ds::Screen;

constexpr std::array MIBS{MibState::INITIALIZING, MibState::IDLE, MibState::ENABLED,
                          MibState::OTHER};
constexpr std::array SCREENS{Screen::BOOT, Screen::LOCKED, Screen::DRIVE, Screen::SEAT,
                             Screen::OTHER};
constexpr std::array RESENDS{ds::Resend::NOT_DUE, ds::Resend::FAST, ds::Resend::SLOW};
constexpr unsigned HIDDEN_COMBOS = 1U << static_cast<unsigned>(std::popcount(ds::kHiddenGuards));
constexpr std::size_t ENV_COUNT = 2 * MIBS.size() * SCREENS.size() * 2 * 8 * 4 * RESENDS.size();

// Every distinct Env, by index (0 .. ENV_COUNT-1).
Env env_at(std::size_t i) {
  const bool link = (i % 2) != 0;
  i /= 2;
  const MibState mib = MIBS[i % MIBS.size()];
  i /= MIBS.size();
  const Screen screen = SCREENS[i % SCREENS.size()];
  i /= SCREENS.size();
  const bool menu = (i % 2) != 0;
  i /= 2;
  const std::size_t deadlines = i % 8;
  i /= 8;
  const std::size_t stop = i % 4;
  i /= 4;
  return Env{.link_connected = link,
             .mib = mib,
             .screen = screen,
             .menu_open = menu,
             .exit_elapsed = (deadlines & 1U) != 0,
             .warn_elapsed = (deadlines & 2U) != 0,
             .giveup_elapsed = (deadlines & 4U) != 0,
             .calibrating = (stop & 1U) != 0,
             .stop_fault_elapsed = (stop & 2U) != 0,
             .resend = RESENDS[i % RESENDS.size()]};
}

// Spread an index over the hidden guard bits, in bit order.
GuardMask hidden_at(unsigned i) {
  GuardMask m = 0;
  unsigned k = 0;
  for (unsigned b = 0; b < ds::kGuardCount; ++b) {
    const GuardMask v = GuardMask{1} << b;
    if ((v & ds::kHiddenGuards) != 0) {
      if (((i >> k) & 1U) != 0) {
        m |= v;
      }
      ++k;
    }
  }
  return m;
}

bool in_contract(Phase p, GuardMask hidden, Input in, GuardMask guards) {
  const auto &pre = ds::INPUT_PRECONDITIONS[static_cast<std::size_t>(in)];
  return ds::hidden_valid(p, hidden) && ds::in(pre.phases, p) && ds::matches(pre.guard, guards);
}

struct Tally {
  unsigned long all = 0;
  unsigned long contract = 0;
  unsigned long matched = 0;
  unsigned long mismatches = 0;
};
constinit unsigned long g_contract_total = 0;
constinit unsigned long g_all_total = 0;

// One combination: the session against the table. Returns true when they agree.
bool agrees(Phase p, GuardMask hidden, Input in, const Env &env, bool &matched) {
  DriveSession s;
  DriveSessionTestPeer::set(s, p, hidden);
  Actions got{};
  const bool ok = s.step(in, env, got);
  const std::size_t row = ds::find_row(p, in, ds::env_guards(env) | hidden);
  matched = row < ds::TRANSITIONS.size();
  if (!matched) {
    return ok && s.phase() == p && s.hidden() == hidden && got == Actions{};
  }
  const ds::Transition &t = ds::TRANSITIONS[row];
  return ok && s.phase() == t.to && s.hidden() == ds::apply(t.actions, hidden) && got == t.actions;
}

// Every (phase, hidden, env) for one input.
Tally run_oracle(Input in) {
  Tally t;
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    for (unsigned hi = 0; hi < HIDDEN_COMBOS; ++hi) {
      const GuardMask hidden = hidden_at(hi);
      for (std::size_t ei = 0; ei < ENV_COUNT; ++ei) {
        const Env env = env_at(ei);
        bool matched = false;
        const bool ok = agrees(p, hidden, in, env, matched);
        ++t.all;
        t.matched += matched ? 1UL : 0UL;
        if (in_contract(p, hidden, in, ds::env_guards(env) | hidden)) {
          ++t.contract;
        }
        if (!ok && t.mismatches++ < 5) {
          std::printf("MISMATCH input %u phase %zu hidden 0x%x env %zu\n",
                      static_cast<unsigned>(in), pi, static_cast<unsigned>(hidden), ei);
        }
      }
    }
  }
  std::printf("ORACLE input %u: %lu combinations (%lu in the contract), %lu matched a row\n",
              static_cast<unsigned>(in), t.all, t.contract, t.matched);
  g_contract_total += t.contract;
  g_all_total += t.all;
  return t;
}

void expect_oracle(Input in) {
  const Tally t = run_oracle(in);
  TEST_ASSERT_EQUAL_UINT64(ds::kPhaseCount * HIDDEN_COMBOS * ENV_COUNT, t.all);
  TEST_ASSERT_TRUE(t.contract > 0);
  TEST_ASSERT_TRUE(t.matched > 0);
  TEST_ASSERT_EQUAL_UINT64(0, t.mismatches);
}

} // namespace

// ---- The oracle, one case per input (TS-UNIT-08) ------------------------------------------

TEST_CASE("DRV-001 TICK_FOLLOW gives the table's row in every phase, hidden mask and env, "
          "or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::TICK_FOLLOW);
}

TEST_CASE("DRV-002 TICK_EXIT_DUE gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::TICK_EXIT_DUE);
}

TEST_CASE("DRV-003 TICK_WARN_DUE gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::TICK_WARN_DUE);
}

TEST_CASE("DRV-004 TICK_GIVEUP_DUE gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::TICK_GIVEUP_DUE);
}

TEST_CASE("DRV-005 UNLOCK_HOLD_DONE gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::UNLOCK_HOLD_DONE);
}

TEST_CASE("DRV-006 EXIT_HOLD_DONE gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::EXIT_HOLD_DONE);
}

TEST_CASE("DRV-007 MENU_KEY_DRIVE gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::MENU_KEY_DRIVE);
}

TEST_CASE("DRV-008 PROFILE_CLICK gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::PROFILE_CLICK);
}

TEST_CASE("DRV-009 UNLOCK_TIMER gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::UNLOCK_TIMER);
}

TEST_CASE("DRV-010 ENTRY_PUSH gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::ENTRY_PUSH);
}

TEST_CASE("DRV-011 MENU_ROW_DRIVE gives the table's row or changes nothing",
          "[drive][safety][oracle]") {
  expect_oracle(Input::MENU_ROW_DRIVE);
}

TEST_CASE("DRV-012 the oracle drove every input and every row of the table",
          "[drive][safety][oracle]") {
  // Runs after DRV-001..011 in declared order; with a shuffled order it recomputes.
  if (g_all_total != ds::kInputCount * ds::kPhaseCount * HIDDEN_COMBOS * ENV_COUNT) {
    g_all_total = 0;
    g_contract_total = 0;
    for (std::size_t i = 0; i < ds::kInputCount; ++i) {
      (void)run_oracle(static_cast<Input>(i)); // totals only; verdicts are DRV-001..011
    }
  }
  std::printf("ORACLE total: %lu combinations driven, %lu of them in the table's contract\n",
              g_all_total, g_contract_total);
  TEST_ASSERT_EQUAL_UINT64(ds::kInputCount * ds::kPhaseCount * HIDDEN_COMBOS * ENV_COUNT,
                           g_all_total);
  // Every row is reachable from a state inside the contract.
  for (std::size_t r = 0; r < ds::TRANSITIONS.size(); ++r) {
    const ds::Transition &t = ds::TRANSITIONS[r];
    bool reached = false;
    for (unsigned hi = 0; hi < HIDDEN_COMBOS && !reached; ++hi) {
      for (std::size_t ei = 0; ei < ENV_COUNT && !reached; ++ei) {
        const GuardMask g = ds::env_guards(env_at(ei)) | hidden_at(hi);
        reached =
            in_contract(t.from, hidden_at(hi), t.input, g) && ds::find_row(t.from, t.input, g) == r;
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(reached, "a row no contract state reaches");
  }
}
