// L1 host app for drive_session: the session's sequences, the safe state and the stick gate
// (DRV-013..). The full-product oracle (DRV-001..012) is ../oracle_full, run on demand
// (`make full`, owner decision G2); CI runs the by-input oracle (../oracle_by_input).

#include "drive_session.hpp"

#include <algorithm>
#include <array>
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
constexpr unsigned HIDDEN_COMBOS = 1U << 5U;
constexpr std::size_t ENV_COUNT = 2 * MIBS.size() * SCREENS.size() * 2 * 8;

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
  return Env{link, mib, screen, menu, (i & 1U) != 0, (i & 2U) != 0, (i & 4U) != 0};
}

// The hidden guards are bits 10..14: spread a 5-bit index over them.
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

Env make_env(bool link, MibState mib, Screen screen) {
  return Env{link, mib, screen, false, false, false, false};
}

// One rtps_poll_cb tick as drive_wait_poll runs it: TICK_FOLLOW on the Env sampled when the tick
// starts, then the three deadline checks on one Env sampled after follow-state's actions ran.
// Returns the actions of each step.
std::array<Actions, 4> tick(DriveSession &s, const Env &at_start, const Env &after_follow) {
  std::array<Actions, 4> out{};
  for (std::size_t i = 0; i < ds::TICK_SEQUENCE.size(); ++i) {
    TEST_ASSERT_TRUE(s.step(ds::TICK_SEQUENCE[i], i == 0 ? at_start : after_follow, out[i]));
  }
  return out;
}

// A tick in which nothing changed between the two samples.
std::array<Actions, 4> tick(DriveSession &s, const Env &env) { return tick(s, env, env); }

bool contains(const Actions &a, Action x) { return std::ranges::find(a, x) != a.end(); }

std::uint64_t splitmix64(std::uint64_t &state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

} // namespace

// ---- Sequences ------------------------------------------------------------------------------

TEST_CASE("DRV-013 an unlock asked, granted, driven and exited follows the table's path",
          "[drive][safety]") {
  DriveSession s;
  Actions out{};
  const Env idle_locked = make_env(true, MibState::IDLE, Screen::LOCKED);
  TEST_ASSERT_TRUE(s.step(Input::UNLOCK_HOLD_DONE, idle_locked, out));
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase());
  TEST_ASSERT_TRUE(s.request_enable());
  (void)tick(s, make_env(true, MibState::ENABLED, Screen::LOCKED));
  TEST_ASSERT_EQUAL(Phase::UNLOCKING, s.phase());
  TEST_ASSERT_FALSE(s.locked());
  TEST_ASSERT_TRUE(s.step(Input::UNLOCK_TIMER, idle_locked, out));
  TEST_ASSERT_EQUAL(Phase::DRIVING, s.phase());
  TEST_ASSERT_TRUE(contains(out, Action::GO_DRIVE_SCREEN));
  const Env enabled_drive = make_env(true, MibState::ENABLED, Screen::DRIVE);
  TEST_ASSERT_TRUE(s.step(Input::MENU_KEY_DRIVE, enabled_drive, out));
  TEST_ASSERT_EQUAL(Phase::EXITING, s.phase());
  TEST_ASSERT_TRUE(contains(out, Action::SEND_DISABLE));
  TEST_ASSERT_FALSE(s.request_enable());
  const auto steps = tick(s, make_env(true, MibState::IDLE, Screen::DRIVE));
  TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
  TEST_ASSERT_TRUE(contains(steps[0], Action::OPEN_MENU_ON_ARRIVAL));
  TEST_ASSERT_FALSE(contains(steps[0], Action::SHOW_DRIVE_STOPPED));
  TEST_ASSERT_TRUE(ds::hidden_valid(s.phase(), s.hidden()));
}

TEST_CASE("DRV-014 an unanswered ask warns on one tick, rests the ring on the next, then gives up",
          "[drive][safety]") {
  DriveSession s;
  Actions out{};
  TEST_ASSERT_TRUE(
      s.step(Input::UNLOCK_HOLD_DONE, make_env(true, MibState::IDLE, Screen::LOCKED), out));
  Env late = make_env(true, MibState::IDLE, Screen::LOCKED);
  late.warn_elapsed = true;
  auto steps = tick(s, late);
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase()); // follow-state ran before the warn
  TEST_ASSERT_TRUE(contains(steps[2], Action::SHOW_NOT_GRANTED));
  steps = tick(s, late);
  TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
  TEST_ASSERT_TRUE(contains(steps[0], Action::RING_REST));
  late.giveup_elapsed = true;
  steps = tick(s, late);
  TEST_ASSERT_TRUE(contains(steps[3], Action::SEND_DISABLE));
  TEST_ASSERT_FALSE(s.request_enable());
  TEST_ASSERT_EQUAL_HEX32(0, s.hidden());
}

TEST_CASE("DRV-015 a seeded random walk keeps the phase invariants and matches the table at "
          "every step",
          "[drive][safety]") {
  DriveSession s;
  std::uint64_t seed = 0x5EED0D51;
  unsigned long steps = 0;
  unsigned long bad = 0;
  constexpr unsigned long STEPS = 200000;
  for (unsigned long n = 0; n < STEPS; ++n) {
    const std::uint64_t r = splitmix64(seed);
    const auto in = static_cast<Input>(r % ds::kInputCount);
    const Env env = env_at(static_cast<std::size_t>((r >> 8U) % ENV_COUNT));
    const auto &pre = ds::INPUT_PRECONDITIONS[static_cast<std::size_t>(in)];
    const GuardMask g = ds::env_guards(env) | s.hidden();
    if (!ds::in(pre.phases, s.phase()) || !ds::matches(pre.guard, g)) {
      continue; // an input that cannot arrive here
    }
    const Phase before = s.phase();
    const GuardMask hidden_before = s.hidden();
    bool matched = false;
    bad += agrees(before, hidden_before, in, env, matched) ? 0UL : 1UL;
    Actions out{};
    TEST_ASSERT_TRUE(s.step(in, env, out));
    bad += ds::hidden_valid(s.phase(), s.hidden()) ? 0UL : 1UL;
    ++steps;
  }
  std::printf("WALK %lu steps inside the contract\n", steps);
  TEST_ASSERT_TRUE(steps > STEPS / 4);
  TEST_ASSERT_EQUAL_UINT64(0, bad);
}

// ---- Safe state -----------------------------------------------------------------------------

TEST_CASE("DRV-016 a corrupted phase or input sends the session to the safe state and says so",
          "[drive][safety]") {
  const Env env = make_env(true, MibState::ENABLED, Screen::DRIVE);
  for (std::size_t pi = 0; pi <= ds::kPhaseCount; ++pi) {
    for (unsigned bad_in : {static_cast<unsigned>(ds::kInputCount), 0xFFU}) {
      DriveSession s;
      // pi == kPhaseCount is a corrupted phase; with a valid phase, the input is corrupted.
      DriveSessionTestPeer::set(s, static_cast<Phase>(pi), ds::kHiddenGuards);
      const Input in = pi == ds::kPhaseCount
                           ? Input::TICK_FOLLOW
                           : static_cast<Input>(static_cast<std::uint8_t>(bad_in));
      Actions out{};
      TEST_ASSERT_FALSE(s.step(in, env, out));
      TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
      TEST_ASSERT_TRUE(out == DriveSession::SAFE_STATE_ACTIONS);
      TEST_ASSERT_FALSE(s.request_enable());
      TEST_ASSERT_TRUE(ds::hidden_valid(s.phase(), s.hidden()));
    }
  }
}

TEST_CASE("DRV-017 the safe state from any state is LOCKED, DISABLE sent, no menu on arrival, "
          "gate updated",
          "[drive][safety]") {
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    for (unsigned hi = 0; hi < HIDDEN_COMBOS; ++hi) {
      DriveSession s;
      DriveSessionTestPeer::set(s, static_cast<Phase>(pi), hidden_at(hi));
      Actions out{};
      s.enter_safe_state(out);
      TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
      TEST_ASSERT_TRUE(s.locked());
      TEST_ASSERT_FALSE(s.request_enable());
      TEST_ASSERT_TRUE(ds::hidden_valid(s.phase(), s.hidden()));
      TEST_ASSERT_TRUE(contains(out, Action::SEND_DISABLE));
      TEST_ASSERT_TRUE(contains(out, Action::SET_LOCKED));
      TEST_ASSERT_TRUE(contains(out, Action::GATE_UPDATE));
      // No menu over the Locked screen it loads (the relocks' rule, TABLE.md rows 3-9).
      TEST_ASSERT_TRUE(contains(out, Action::CLEAR_MENU_ON_ARRIVAL));
    }
  }
}

TEST_CASE("DRV-018 a value outside the Action enum changes no hidden variable", "[drive][safety]") {
  DriveSession s;
  DriveSessionTestPeer::set(s, Phase::ASKING, ds::kHiddenGuards);
  Actions out{};
  DriveSessionTestPeer::perform_one(s, static_cast<Action>(0xEEU), out);
  TEST_ASSERT_EQUAL_HEX32(ds::kHiddenGuards, s.hidden());
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase());
}

// ---- Stick gate and scale (pure functions, CS-SAF-02) ----------------------------------------

TEST_CASE("DRV-019 the stick drives only unlocked, on the Drive screen, with no menu",
          "[drive][safety][gate]") {
  for (bool locked : {false, true}) {
    for (Screen screen : SCREENS) {
      for (bool menu : {false, true}) {
        const bool want = !locked && screen == Screen::DRIVE && !menu;
        TEST_ASSERT_EQUAL(want, ds::stick_drives(locked, screen, menu));
      }
    }
  }
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    DriveSession s;
    DriveSessionTestPeer::set(s, static_cast<Phase>(pi), 0);
    TEST_ASSERT_EQUAL(ds::is_locked_phase(s.phase()), s.locked());
  }
}

TEST_CASE("DRV-020 the stick scale is zero when calibrating or gated, else the speed",
          "[drive][safety][gate]") {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ds::stick_scale(true, true, 0.7f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ds::stick_scale(false, false, 0.7f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ds::stick_scale(true, false, 0.7f));
  TEST_ASSERT_EQUAL_FLOAT(0.7f, ds::stick_scale(false, true, 0.7f));
  // Pinned as-is (TABLE.md section 4): the gate is a multiply, so a NaN speed passes through.
  TEST_ASSERT_TRUE(
      std::isnan(ds::stick_scale(false, true, std::numeric_limits<float>::quiet_NaN())));
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
                          ds::stick_scale(false, false, std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("DRV-021 a new session is LOCKED, asks nothing and has nothing armed",
          "[drive][safety]") {
  const DriveSession s;
  TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
  TEST_ASSERT_TRUE(s.locked());
  TEST_ASSERT_FALSE(s.request_enable());
  TEST_ASSERT_EQUAL_HEX32(0, s.hidden());
}

// ---- The tick's two Envs (README "Model", TABLE.md section 1) --------------------------------

TEST_CASE("DRV-022 a tick decides follow-state on the Env at its start and the three deadline "
          "checks on one Env sampled after follow-state",
          "[drive][safety]") {
  const Env at_start = make_env(true, MibState::IDLE, Screen::LOCKED);
  Actions out{};
  // A warn deadline that passes between the two samples: NOT_GRANTED in the same tick.
  DriveSession s;
  TEST_ASSERT_TRUE(s.step(Input::UNLOCK_HOLD_DONE, at_start, out));
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase());
  Env warn_passed = at_start;
  warn_passed.warn_elapsed = true;
  auto steps = tick(s, at_start, warn_passed);
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase()); // follow-state saw the warn still running
  TEST_ASSERT_TRUE(contains(steps[2], Action::SHOW_NOT_GRANTED));
  // With one Env (the start sample) for the whole tick, the warn would wait a tick.
  DriveSession one;
  TEST_ASSERT_TRUE(one.step(Input::UNLOCK_HOLD_DONE, at_start, out));
  steps = tick(one, at_start);
  TEST_ASSERT_FALSE(contains(steps[2], Action::SHOW_NOT_GRANTED));
  // An ENABLED that arrives between the samples is not seen by follow-state until the next
  // tick: the deadline rows do not read the link or the MIB.
  DriveSession late;
  TEST_ASSERT_TRUE(late.step(Input::UNLOCK_HOLD_DONE, at_start, out));
  const Env enabled = make_env(true, MibState::ENABLED, Screen::LOCKED);
  steps = tick(late, at_start, enabled);
  TEST_ASSERT_EQUAL(Phase::ASKING, late.phase());
  for (const Actions &a : steps) {
    TEST_ASSERT_FALSE(contains(a, Action::SET_UNLOCKED));
  }
  (void)tick(late, enabled);
  TEST_ASSERT_EQUAL(Phase::UNLOCKING, late.phase());
}
