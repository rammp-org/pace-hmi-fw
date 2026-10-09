// L1 host app for drive_session: the session's sequences, the safe state, the stick gate and
// the hazard fix C1's cases (DRV-013..021, DRV-023..034, DRV-116). The full-product oracle
// (DRV-001..012) is ../oracle_full, run on demand
// (`make full`, owner decision G2); CI runs the by-input oracle (../oracle_by_input).

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
constexpr std::size_t ENV_COUNT = 2 * MIBS.size() * SCREENS.size() * 2 * 8 * 4 * RESENDS.size() * 2;

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
             .resend = RESENDS[i % RESENDS.size()],
             .post_ok = (i / RESENDS.size()) % 2 != 0};
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
  return Env{.link_connected = link,
             .mib = mib,
             .screen = screen,
             .menu_open = false,
             .exit_elapsed = false,
             .warn_elapsed = false,
             .giveup_elapsed = false,
             .calibrating = false,
             .stop_fault_elapsed = false,
             .resend = ds::Resend::NOT_DUE,
             .post_ok = true};
}

// The actions of each sub-step of one tick, in TICK_SEQUENCE order.
using TickOut = std::array<Actions, ds::TICK_SEQUENCE.size()>;

// The index of a sub-step in TICK_SEQUENCE.
std::size_t step_of(Input in) {
  for (std::size_t i = 0; i < ds::TICK_SEQUENCE.size(); ++i) {
    if (ds::TICK_SEQUENCE[i] == in) {
      return i;
    }
  }
  return ds::TICK_SEQUENCE.size();
}

// One rtps_poll_cb tick as the adapter runs it: TICK_FOLLOW on the Env sampled when the tick
// starts, then the other sub-steps on one Env sampled after follow-state's actions ran.
TickOut tick(DriveSession &s, const Env &at_start, const Env &after_follow) {
  TickOut out{};
  for (std::size_t i = 0; i < ds::TICK_SEQUENCE.size(); ++i) {
    TEST_ASSERT_TRUE(s.step(ds::TICK_SEQUENCE[i], i == 0 ? at_start : after_follow, out[i]));
  }
  return out;
}

// A tick in which nothing changed between the two samples.
TickOut tick(DriveSession &s, const Env &env) { return tick(s, env, env); }

bool contains(const Actions &a, Action x) { return std::ranges::find(a, x) != a.end(); }
bool any_contains(const TickOut &steps, Action x) {
  return std::ranges::any_of(steps, [x](const Actions &a) { return contains(a, x); });
}

std::uint64_t splitmix64(std::uint64_t &state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

// A session after the boot DISABLE (C3, row 50): LOCKED, BOOT_STOP_DONE (the "booted"
// preamble of hazard-c3-spec.md F1).
DriveSession booted() {
  DriveSession s;
  (void)tick(s, make_env(true, MibState::IDLE, Screen::LOCKED));
  TEST_ASSERT_EQUAL_HEX32(ds::bit(ds::Guard::BOOT_STOP_DONE), s.hidden());
  return s;
}

// A session DRIVING with the Drive screen up (rows 1, 35).
DriveSession driving() {
  DriveSession s = booted();
  Actions out{};
  (void)tick(s, make_env(true, MibState::ENABLED, Screen::LOCKED));
  TEST_ASSERT_TRUE(
      s.step(Input::UNLOCK_TIMER, make_env(true, MibState::ENABLED, Screen::LOCKED), out));
  TEST_ASSERT_EQUAL(Phase::DRIVING, s.phase());
  return s;
}

} // namespace

// ---- Sequences ------------------------------------------------------------------------------

TEST_CASE("DRV-013 an unlock asked, granted, driven and exited follows the table's path",
          "[drive][safety]") {
  DriveSession s = booted();
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
  TEST_ASSERT_TRUE(contains(out, Action::ARM_STOP_TIMER));
  TEST_ASSERT_FALSE(s.request_enable());
  const auto steps = tick(s, make_env(true, MibState::IDLE, Screen::DRIVE));
  TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
  TEST_ASSERT_TRUE(contains(steps[0], Action::OPEN_MENU_ON_ARRIVAL));
  TEST_ASSERT_FALSE(contains(steps[0], Action::SHOW_DRIVE_STOPPED));
  // C1: the relock sends one DISABLE and ends the stop.
  TEST_ASSERT_TRUE(contains(steps[0], Action::SEND_DISABLE));
  TEST_ASSERT_TRUE(contains(steps[0], Action::CLEAR_STOP_FAULT));
  TEST_ASSERT_FALSE(s.request_enable());
  TEST_ASSERT_TRUE(ds::hidden_valid(s.phase(), s.hidden()));
}

TEST_CASE("DRV-014 an unanswered ask warns on one tick, rests the ring on the next, then gives up",
          "[drive][safety]") {
  DriveSession s = booted();
  Actions out{};
  TEST_ASSERT_TRUE(
      s.step(Input::UNLOCK_HOLD_DONE, make_env(true, MibState::IDLE, Screen::LOCKED), out));
  Env late = make_env(true, MibState::IDLE, Screen::LOCKED);
  late.warn_elapsed = true;
  auto steps = tick(s, late);
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase()); // follow-state ran before the warn
  TEST_ASSERT_TRUE(contains(steps[step_of(Input::TICK_WARN_DUE)], Action::SHOW_NOT_GRANTED));
  steps = tick(s, late);
  TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
  TEST_ASSERT_TRUE(contains(steps[0], Action::RING_REST));
  late.giveup_elapsed = true;
  steps = tick(s, late);
  TEST_ASSERT_TRUE(contains(steps[step_of(Input::TICK_GIVEUP_DUE)], Action::SEND_DISABLE));
  TEST_ASSERT_FALSE(s.request_enable());
  TEST_ASSERT_EQUAL_HEX32(ds::bit(ds::Guard::BOOT_STOP_DONE), s.hidden()); // only the boot stop
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
          "gate updated, stop fault cleared",
          "[drive][safety][REQ-DRV-31]") {
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
      // C1: the stop ends with it.
      TEST_ASSERT_TRUE(contains(out, Action::CLEAR_STOP_FAULT));
      TEST_ASSERT_EQUAL_HEX32(0, s.hidden() & ds::bit(ds::Guard::STOP_FAULT));
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

// ---- The hazard fix C1 (docs/plans/hazard-c1-spec.md §5.1) ----------------------------------

TEST_CASE("DRV-023 the MCB ENABLED while calibrating or on the Boot screen: no row, nothing; the "
          "next tick after either clears unlocks",
          "[drive][safety][REQ-DRV-35]") {
  for (const bool on_boot : {false, true}) {
    for (const Phase from : {Phase::LOCKED, Phase::ASKING}) {
      DriveSession s = booted();
      Actions out{};
      if (from == Phase::ASKING) {
        TEST_ASSERT_TRUE(
            s.step(Input::UNLOCK_HOLD_DONE, make_env(true, MibState::IDLE, Screen::LOCKED), out));
      }
      Env blocked = make_env(true, MibState::ENABLED, on_boot ? Screen::BOOT : Screen::OTHER);
      blocked.calibrating = !on_boot;
      const GuardMask hidden = s.hidden();
      const auto steps = tick(s, blocked);
      TEST_ASSERT_EQUAL(from, s.phase());
      TEST_ASSERT_EQUAL_HEX32(hidden, s.hidden());
      for (const Actions &a : steps) {
        TEST_ASSERT_TRUE(a == Actions{});
      }
      Env clear = blocked;
      clear.calibrating = false;
      clear.screen = on_boot ? Screen::LOCKED : Screen::OTHER;
      const auto next = tick(s, clear);
      TEST_ASSERT_EQUAL(Phase::UNLOCKING, s.phase());
      TEST_ASSERT_TRUE(contains(next[0], Action::SET_UNLOCKED));
    }
  }
}

TEST_CASE("DRV-024 the stop timeline: DISABLE on each FAST tick, the fault once when the window "
          "passes, then DISABLE only on SLOW; the relock clears the fault",
          "[drive][safety][REQ-DRV-26][REQ-DRV-28]") {
  using ds::Resend;
  DriveSession s = driving();
  Actions out{};
  Env env = make_env(true, MibState::ENABLED, Screen::DRIVE);
  TEST_ASSERT_TRUE(s.step(Input::EXIT_HOLD_DONE, env, out));
  TEST_ASSERT_TRUE(contains(out, Action::ARM_STOP_TIMER));
  TEST_ASSERT_TRUE(s.notice() == ds::StopNotice::STOPPING);
  const std::size_t fault = step_of(Input::TICK_STOP_FAULT_DUE);
  const std::size_t resend = step_of(Input::TICK_STOP_RESEND);
  // Before the fault: FAST due -> DISABLE; not due -> nothing.
  env.resend = Resend::FAST;
  auto steps = tick(s, env);
  TEST_ASSERT_TRUE(contains(steps[resend], Action::SEND_DISABLE));
  env.resend = Resend::NOT_DUE;
  steps = tick(s, env);
  TEST_ASSERT_TRUE(steps[resend] == Actions{});
  // The exit refused: the re-send goes on.
  env.exit_elapsed = true;
  env.resend = Resend::FAST;
  steps = tick(s, env);
  TEST_ASSERT_EQUAL(Phase::EXIT_REFUSED, s.phase());
  TEST_ASSERT_TRUE(contains(steps[resend], Action::SEND_DISABLE));
  // The window passes: the fault once, and on that tick FAST is no longer enough.
  env.stop_fault_elapsed = true;
  steps = tick(s, env);
  TEST_ASSERT_TRUE(contains(steps[fault], Action::RAISE_STOP_FAULT));
  TEST_ASSERT_TRUE(steps[resend] == Actions{});
  TEST_ASSERT_TRUE(s.notice() == ds::StopNotice::MCB_DID_NOT_STOP);
  steps = tick(s, env);
  TEST_ASSERT_TRUE(steps[fault] == Actions{}); // raised once
  env.resend = Resend::SLOW;
  steps = tick(s, env);
  TEST_ASSERT_TRUE(contains(steps[resend], Action::SEND_DISABLE));
  // The MCB stops: the relock clears the fault and the notice.
  env.mib = MibState::IDLE;
  steps = tick(s, env);
  TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
  TEST_ASSERT_TRUE(contains(steps[0], Action::CLEAR_STOP_FAULT));
  TEST_ASSERT_EQUAL_HEX32(0, s.hidden() & ds::bit(ds::Guard::STOP_FAULT));
  TEST_ASSERT_TRUE(s.notice() == ds::StopNotice::NONE);
}

TEST_CASE("DRV-025 stop_notice over every phase with and without the stop fault",
          "[drive][safety][REQ-DRV-29]") {
  using ds::StopNotice;
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    const bool exit_phase = p == Phase::EXITING || p == Phase::EXIT_REFUSED;
    TEST_ASSERT_TRUE(ds::stop_notice(p, 0) ==
                     (exit_phase ? StopNotice::STOPPING : StopNotice::NONE));
    TEST_ASSERT_TRUE(ds::stop_notice(p, ds::bit(ds::Guard::STOP_FAULT)) ==
                     (exit_phase ? StopNotice::MCB_DID_NOT_STOP : StopNotice::NONE));
    // Other hidden bits do not matter.
    TEST_ASSERT_TRUE(ds::stop_notice(p, ds::kHiddenGuards & ~ds::bit(ds::Guard::STOP_FAULT)) ==
                     ds::stop_notice(p, 0));
  }
}

TEST_CASE("DRV-026 every relock from an unlocked phase sends DISABLE and leaves the request "
          "DISABLE",
          "[drive][safety][REQ-DRV-23][REQ-DRV-34]") {
  unsigned long relocks = 0;
  unsigned long bad = 0;
  for (const Phase p : {Phase::UNLOCKING, Phase::DRIVING, Phase::EXITING, Phase::EXIT_REFUSED}) {
    for (unsigned hi = 0; hi < HIDDEN_COMBOS; ++hi) {
      const GuardMask hidden = hidden_at(hi);
      if (!ds::hidden_valid(p, hidden)) {
        continue;
      }
      for (std::size_t ei = 0; ei < ENV_COUNT; ++ei) {
        DriveSession s;
        DriveSessionTestPeer::set(s, p, hidden);
        Actions out{};
        TEST_ASSERT_TRUE(s.step(Input::TICK_FOLLOW, env_at(ei), out));
        if (s.phase() != Phase::LOCKED) {
          continue;
        }
        ++relocks;
        bad += contains(out, Action::SEND_DISABLE) && !s.request_enable() ? 0UL : 1UL;
      }
    }
  }
  std::printf("DRV-026 %lu relocks checked\n", relocks);
  TEST_ASSERT_TRUE(relocks > 0);
  TEST_ASSERT_EQUAL_UINT64(0, bad);
}

TEST_CASE("DRV-027 over every state, ENABLE is sent only by rows 18, 31 and 32, and by 31-32 "
          "only with the MCB ENABLED on a CONNECTED link and POST passed",
          "[drive][safety][REQ-DRV-37]") {
  unsigned long enables = 0;
  unsigned long bad = 0;
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    for (unsigned hi = 0; hi < HIDDEN_COMBOS; ++hi) {
      const GuardMask hidden = hidden_at(hi);
      if (!ds::hidden_valid(p, hidden)) {
        continue;
      }
      for (std::size_t ii = 0; ii < ds::kInputCount; ++ii) {
        const auto in = static_cast<Input>(ii);
        for (std::size_t ei = 0; ei < ENV_COUNT; ++ei) {
          const Env env = env_at(ei);
          DriveSession s;
          DriveSessionTestPeer::set(s, p, hidden);
          Actions out{};
          (void)s.step(in, env, out);
          if (!contains(out, Action::SEND_ENABLE)) {
            continue;
          }
          ++enables;
          const bool hold = p == Phase::LOCKED && in == Input::UNLOCK_HOLD_DONE && // row 18
                            env.post_ok;
          const bool tap = (p == Phase::UNLOCKING || p == Phase::DRIVING) && // rows 31-32
                           in == Input::PROFILE_CLICK && env.link_connected &&
                           env.mib == MibState::ENABLED && env.post_ok;
          bad += hold || tap ? 0UL : 1UL;
        }
      }
    }
  }
  std::printf("DRV-027 %lu ENABLEs checked\n", enables);
  TEST_ASSERT_TRUE(enables > 0);
  TEST_ASSERT_EQUAL_UINT64(0, bad);
}

TEST_CASE("DRV-028 the exit hold while locked sends one DISABLE (row 42); while asking it also "
          "withdraws the ask (row 43)",
          "[drive][safety][REQ-DRV-25]") {
  const Env env = make_env(true, MibState::IDLE, Screen::LOCKED);
  DriveSession locked;
  Actions out{};
  TEST_ASSERT_TRUE(locked.step(Input::EXIT_HOLD_DONE, env, out));
  TEST_ASSERT_EQUAL(Phase::LOCKED, locked.phase());
  TEST_ASSERT_TRUE(out == (Actions{Action::SEND_DISABLE, Action::CLEAR_GIVEUP}));
  DriveSession asking;
  TEST_ASSERT_TRUE(asking.step(Input::UNLOCK_HOLD_DONE, env, out));
  TEST_ASSERT_EQUAL(Phase::ASKING, asking.phase());
  TEST_ASSERT_TRUE(asking.request_enable());
  TEST_ASSERT_TRUE(asking.step(Input::EXIT_HOLD_DONE, env, out));
  TEST_ASSERT_EQUAL(Phase::LOCKED, asking.phase());
  TEST_ASSERT_TRUE(out == (Actions{Action::SEND_DISABLE, Action::CLEAR_WARN, Action::CLEAR_GIVEUP,
                                   Action::RING_REST}));
  TEST_ASSERT_FALSE(asking.request_enable());
  TEST_ASSERT_EQUAL_HEX32(0, asking.hidden());
}

// ---- The tick's two Envs (README "Model", TABLE.md section 1) --------------------------------

TEST_CASE("DRV-116 a tick runs its sub-steps in TICK_SEQUENCE order on two Envs: the first "
          "CONNECTED tick sends the boot DISABLE and does not enter; a re-send due between the "
          "samples is sent the same tick; a relock on the first means no re-send",
          "[drive][safety][REQ-DRV-36]") {
  using ds::Resend;
  // The warn deadline passing between the two samples: NOT_GRANTED in the same tick (as before).
  const Env at_start = make_env(true, MibState::IDLE, Screen::LOCKED);
  Actions out{};
  DriveSession s;
  TEST_ASSERT_TRUE(s.step(Input::UNLOCK_HOLD_DONE, at_start, out));
  Env warn_passed = at_start;
  warn_passed.warn_elapsed = true;
  auto steps = tick(s, at_start, warn_passed);
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase());
  TEST_ASSERT_TRUE(contains(steps[step_of(Input::TICK_WARN_DUE)], Action::SHOW_NOT_GRANTED));
  // An ENABLED that arrives between the samples is seen by follow-state only on the next tick.
  DriveSession late;
  TEST_ASSERT_TRUE(late.step(Input::UNLOCK_HOLD_DONE, at_start, out));
  const Env enabled = make_env(true, MibState::ENABLED, Screen::LOCKED);
  steps = tick(late, at_start, enabled);
  TEST_ASSERT_EQUAL(Phase::ASKING, late.phase());
  TEST_ASSERT_FALSE(any_contains(steps, Action::SET_UNLOCKED));
  (void)tick(late, enabled);
  TEST_ASSERT_EQUAL(Phase::UNLOCKING, late.phase());
  // A re-send that falls due between the two samples is sent in the same tick.
  DriveSession stop = driving();
  Env drive_env = make_env(true, MibState::ENABLED, Screen::DRIVE);
  TEST_ASSERT_TRUE(stop.step(Input::EXIT_HOLD_DONE, drive_env, out));
  Env due = drive_env;
  due.resend = Resend::FAST;
  steps = tick(stop, drive_env, due);
  TEST_ASSERT_TRUE(contains(steps[step_of(Input::TICK_STOP_RESEND)], Action::SEND_DISABLE));
  // A relock on the first Env: the stop has ended, nothing re-sent on the second.
  Env idle = drive_env;
  idle.mib = MibState::IDLE;
  steps = tick(stop, idle, due);
  TEST_ASSERT_EQUAL(Phase::LOCKED, stop.phase());
  TEST_ASSERT_TRUE(steps[step_of(Input::TICK_STOP_RESEND)] == Actions{});
  // C3: the first CONNECTED tick with the MIB ENABLED: the boot DISABLE, no F1; entry on the
  // next tick.
  DriveSession fresh;
  const Env mib_enabled = make_env(true, MibState::ENABLED, Screen::LOCKED);
  steps = tick(fresh, mib_enabled);
  TEST_ASSERT_EQUAL(Phase::LOCKED, fresh.phase());
  TEST_ASSERT_TRUE(contains(steps[step_of(Input::TICK_BOOT_STOP)], Action::SEND_DISABLE));
  TEST_ASSERT_FALSE(any_contains(steps, Action::SET_UNLOCKED));
  (void)tick(fresh, mib_enabled);
  TEST_ASSERT_EQUAL(Phase::UNLOCKING, fresh.phase());
  // The order itself (DSO-018 pins the data).
  TEST_ASSERT_EQUAL(0, step_of(Input::TICK_FOLLOW));
  TEST_ASSERT_EQUAL(1, step_of(Input::TICK_BOOT_STOP));
  TEST_ASSERT_TRUE(step_of(Input::TICK_EXIT_DUE) < step_of(Input::TICK_STOP_FAULT_DUE));
  TEST_ASSERT_TRUE(step_of(Input::TICK_STOP_FAULT_DUE) < step_of(Input::TICK_STOP_RESEND));
}

// ---- The hazard fix C3 (docs/plans/hazard-c3-spec.md §6.1) ----------------------------------

TEST_CASE("DRV-029 the link down for three ticks, then up with the MIB IDLE: one DISABLE on the "
          "first CONNECTED tick, nothing after",
          "[drive][safety][REQ-DRV-39]") {
  DriveSession s;
  unsigned long disables = 0;
  unsigned long other = 0;
  const auto acted = [](const TickOut &steps) {
    return static_cast<unsigned long>(
        std::ranges::count_if(steps, [](const Actions &a) { return a != Actions{}; }));
  };
  for (int i = 0; i < 3; ++i) {
    other += acted(tick(s, make_env(false, MibState::IDLE, Screen::LOCKED)));
  }
  const auto first = tick(s, make_env(true, MibState::IDLE, Screen::LOCKED));
  disables += contains(first[1], Action::SEND_DISABLE) ? 1UL : 0UL;
  for (std::size_t i = 0; i < first.size(); ++i) {
    other += i == 1 || first[i] == Actions{} ? 0UL : 1UL;
  }
  for (int i = 0; i < 4; ++i) {
    other += acted(tick(s, make_env(true, MibState::IDLE, Screen::LOCKED)));
  }
  TEST_ASSERT_EQUAL_UINT64(1, disables);
  TEST_ASSERT_EQUAL_UINT64(0, other);
  TEST_ASSERT_EQUAL(Phase::LOCKED, s.phase());
}

TEST_CASE("DRV-030 the unlock hold before any tick (ASKING), then a tick: no DISABLE, the boot "
          "stop marked, the ask stands",
          "[drive][safety][REQ-DRV-39]") {
  DriveSession s;
  Actions out{};
  const Env env = make_env(true, MibState::IDLE, Screen::LOCKED);
  TEST_ASSERT_TRUE(s.step(Input::UNLOCK_HOLD_DONE, env, out));
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase());
  const auto steps = tick(s, env);
  TEST_ASSERT_FALSE(any_contains(steps, Action::SEND_DISABLE));
  TEST_ASSERT_TRUE(contains(steps[step_of(Input::TICK_BOOT_STOP)], Action::MARK_BOOT_STOP));
  TEST_ASSERT_TRUE((s.hidden() & ds::bit(ds::Guard::BOOT_STOP_DONE)) != 0);
  TEST_ASSERT_EQUAL(Phase::ASKING, s.phase());
  TEST_ASSERT_TRUE(s.request_enable());
}

TEST_CASE("DRV-031 over every state inside the phase invariants with POST_OK false, no row "
          "sends ENABLE",
          "[drive][safety][REQ-DRV-37]") {
  unsigned long checked = 0;
  unsigned long bad = 0;
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    for (unsigned hi = 0; hi < HIDDEN_COMBOS; ++hi) {
      const GuardMask hidden = hidden_at(hi);
      if (!ds::hidden_valid(p, hidden)) {
        continue;
      }
      for (std::size_t ii = 0; ii < ds::kInputCount; ++ii) {
        for (std::size_t ei = 0; ei < ENV_COUNT; ++ei) {
          const Env env = env_at(ei);
          if (env.post_ok) {
            continue;
          }
          DriveSession s;
          DriveSessionTestPeer::set(s, p, hidden);
          Actions out{};
          (void)s.step(static_cast<Input>(ii), env, out);
          ++checked;
          bad += contains(out, Action::SEND_ENABLE) ? 1UL : 0UL;
        }
      }
    }
  }
  std::printf("DRV-031 %lu steps without POST checked\n", checked);
  TEST_ASSERT_EQUAL_UINT64(0, bad);
}

TEST_CASE("DRV-032 the POST refusals (rows 52, 53, 54) and their !RDY twins (19, 38, 40) fire "
          "exactly where their guards say, never both",
          "[drive][safety][REQ-DRV-40][REQ-DRV-41][REQ-DRV-42]") {
  unsigned long bad = 0;
  unsigned long fired = 0;
  for (const Input in : {Input::UNLOCK_HOLD_DONE, Input::ENTRY_PUSH, Input::MENU_ROW_DRIVE}) {
    for (std::size_t ei = 0; ei < ENV_COUNT; ++ei) {
      const Env env = env_at(ei);
      const GuardMask g = ds::env_guards(env);
      const bool rdy = (g & ds::bit(ds::Guard::MCB_READY)) != 0;
      const bool here = in != Input::ENTRY_PUSH || (env.screen == Screen::LOCKED && !env.menu_open);
      DriveSession s;
      Actions out{};
      (void)s.step(in, env, out);
      const bool post_refusal = contains(out, Action::SHOW_REFUSED_POST);
      const bool mcb_refusal = contains(out, Action::SHOW_REFUSED_DRIVE) ||
                               contains(out, Action::SHOW_REFUSED_DRIVE_MENU);
      fired += post_refusal ? 1UL : 0UL;
      bad += post_refusal == (here && rdy && !env.post_ok) ? 0UL : 1UL;
      bad += mcb_refusal == (here && !rdy) ? 0UL : 1UL;
      bad += post_refusal && mcb_refusal ? 1UL : 0UL;
    }
  }
  TEST_ASSERT_TRUE(fired > 0);
  TEST_ASSERT_EQUAL_UINT64(0, bad);
}

TEST_CASE("DRV-033 the MIB ENABLED before POST pass, the boot stop done, on the Locked screen: "
          "row 1 enters Drive (M1 before POST)",
          "[drive][safety][REQ-DRV-35]") {
  DriveSession s = booted();
  Env env = make_env(true, MibState::ENABLED, Screen::LOCKED);
  env.post_ok = false;
  const auto steps = tick(s, env);
  TEST_ASSERT_EQUAL(Phase::UNLOCKING, s.phase());
  TEST_ASSERT_TRUE(steps[0] == ds::TRANSITIONS[0].actions);
}

TEST_CASE("DRV-034 a safe state before the first link: the first CONNECTED tick still sends the "
          "boot DISABLE",
          "[drive][safety][REQ-DRV-39]") {
  DriveSession s;
  Actions out{};
  (void)tick(s, make_env(false, MibState::IDLE, Screen::LOCKED));
  s.enter_safe_state(out);
  TEST_ASSERT_EQUAL_HEX32(0, s.hidden() & ds::bit(ds::Guard::BOOT_STOP_DONE));
  const auto steps = tick(s, make_env(true, MibState::IDLE, Screen::LOCKED));
  TEST_ASSERT_TRUE(contains(steps[step_of(Input::TICK_BOOT_STOP)], Action::SEND_DISABLE));
  TEST_ASSERT_TRUE((s.hidden() & ds::bit(ds::Guard::BOOT_STOP_DONE)) != 0);
}
