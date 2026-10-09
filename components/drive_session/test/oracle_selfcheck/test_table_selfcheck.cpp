// L1 host self-check of drive_session_table.hpp: the TABLE's own invariants,
// re-checked at run time by brute force over every valid (phase, hidden, env).
// It does not test any state machine implementation; that is the oracle test,
// written by another agent against this table (TS-UNIT-08).

#include "drive_session_table.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "drive_session.hpp" // SAFE_STATE_ACTIONS (DSO-014)
#include "drive_session_fingerprint.hpp"
#include "test_case.hpp"

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

constexpr std::array kResend{ds::Resend::NOT_DUE, ds::Resend::FAST, ds::Resend::SLOW};

// Calls f(env_mask) for every distinct Env.
template <typename F> void for_each_env(F &&f) {
  for (bool link : {false, true}) {
    for (MibState mib : kMib) {
      for (Screen s : kScreens) {
        for (bool menu : {false, true}) {
          for (unsigned t = 0; t < 64; ++t) {
            for (ds::Resend r : kResend) {
              const Env e{.link_connected = link,
                          .mib = mib,
                          .screen = s,
                          .menu_open = menu,
                          .exit_elapsed = (t & 1u) != 0,
                          .warn_elapsed = (t & 2u) != 0,
                          .giveup_elapsed = (t & 4u) != 0,
                          .calibrating = (t & 8u) != 0,
                          .stop_fault_elapsed = (t & 16u) != 0,
                          .resend = r,
                          .post_ok = (t & 32u) != 0};
              f(ds::env_guards(e));
            }
          }
        }
      }
    }
  }
}

constexpr unsigned kHiddenCombos = 1u << static_cast<unsigned>(std::popcount(ds::kHiddenGuards));

// Spread an index over the hidden guard bits, in bit order.
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

// The cases register through tests/host/test_case.hpp and run from tests/host/test_main.cpp
// (host L1 contract, TS-UNIT-02), which also defines setUp/tearDown.
TEST_CASE("DSO-001 the tables have their declared sizes", "[drive_session_table]") {
  TEST_ASSERT_EQUAL_size_t(54, ds::TRANSITIONS.size());
  TEST_ASSERT_EQUAL_size_t(14, ds::kInputCount);
  TEST_ASSERT_EQUAL_size_t(23, ds::kGuardCount);
  TEST_ASSERT_EQUAL_INT(16, std::popcount(ds::kEnvGuards));
  TEST_ASSERT_EQUAL_INT(7, std::popcount(ds::kHiddenGuards));
  TEST_ASSERT_EQUAL_size_t(41, ds::kActionCount);
  TEST_ASSERT_EQUAL_size_t(12, ds::kMaxActions);
  TEST_ASSERT_EQUAL_size_t(ds::kTransitionCount, ds::TRANSITIONS.size());
  TEST_ASSERT_EQUAL_size_t(11, ds::HOLD_TRANSITIONS.size());
  TEST_ASSERT_EQUAL_size_t(ds::kPhaseCount, ds::PHASE_INVARIANTS.size());
  TEST_ASSERT_EQUAL_size_t(ds::kInputCount, ds::INPUT_PRECONDITIONS.size());
  TEST_ASSERT_EQUAL_size_t(7, ds::TICK_SEQUENCE.size());
  TEST_ASSERT_EQUAL_size_t(0, ds::KNOWN_GAPS.size());
  TEST_ASSERT_EQUAL_size_t(5, ds::GATE_TRIGGERS.size());
}

TEST_CASE("DSO-002 no valid combination matches two rows", "[drive_session_table]") {
  unsigned long combos = 0;
  unsigned long over = 0;
  for_each_valid([&](Phase p, GuardMask, Input in, GuardMask guards) {
    ++combos;
    const auto hits = std::ranges::count_if(ds::TRANSITIONS, [&](const auto &t) {
      return t.from == p && t.input == in && ds::matches(t.guard, guards);
    });
    if (hits > 1) {
      ++over;
    }
  });
  TEST_ASSERT_TRUE(combos > 0);
  TEST_ASSERT_EQUAL_UINT32(0, over);
}

TEST_CASE("DSO-003 every row matches some valid combination", "[drive_session_table]") {
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

TEST_CASE("DSO-004 every row lands in a valid target state", "[drive_session_table]") {
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

TEST_CASE("DSO-005 every Action and Input is used", "[drive_session_table]") {
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

TEST_CASE("DSO-006 every non-safe phase has a way out or a known gap", "[drive_session_table]") {
  for (std::size_t pi = 0; pi < ds::kPhaseCount; ++pi) {
    const auto p = static_cast<Phase>(pi);
    if (p == ds::kSafePhase) {
      continue;
    }
    TEST_ASSERT_TRUE(ds::has_way_out(p) || ds::is_known_gap(p));
  }
}

namespace {
std::size_t tick_index(Input in) {
  return static_cast<std::size_t>(std::ranges::find(ds::TICK_SEQUENCE, in) -
                                  ds::TICK_SEQUENCE.begin());
}
} // namespace

TEST_CASE("DSO-007 a tick runs follow-state before the deadlines", "[drive_session_table]") {
  // Follow-state first; the three deadline checks in their order after it (DSO-018 pins the
  // whole sequence).
  TEST_ASSERT_TRUE(ds::TICK_SEQUENCE[0] == Input::TICK_FOLLOW);
  TEST_ASSERT_TRUE(tick_index(Input::TICK_EXIT_DUE) < tick_index(Input::TICK_WARN_DUE));
  TEST_ASSERT_TRUE(tick_index(Input::TICK_WARN_DUE) < tick_index(Input::TICK_GIVEUP_DUE));
  TEST_ASSERT_TRUE(tick_index(Input::TICK_GIVEUP_DUE) < ds::TICK_SEQUENCE.size());
}

TEST_CASE("DSO-008 every hold poll combination has one row", "[drive_session_table]") {
  using ds::HoldInput;
  using ds::HoldState;
  for (HoldState s : {HoldState::IDLE, HoldState::FILLING}) {
    for (unsigned m = 0; m < 16; ++m) {
      const auto hits = std::ranges::count_if(ds::HOLD_TRANSITIONS, [&](const auto &t) {
        return t.from == s && t.input == HoldInput::POLL &&
               (m & t.guard.need_true) == t.guard.need_true && (m & t.guard.need_false) == 0;
      });
      TEST_ASSERT_EQUAL_INT(1, static_cast<int>(hits));
    }
  }
}

TEST_CASE("DSO-009 the stick gate opens in exactly one case", "[drive_session_table]") {
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

TEST_CASE("DSO-010 every row cites its code, or the hazard spec that added it",
          "[drive_session_table]") {
  // Rows 1-41 come from the code of e2047a4; a row a hazard fix changed also cites its spec;
  // a row a hazard fix added (42 on) has no code of e2047a4 to cite and cites its spec.
  for (std::size_t i = 0; i < ds::TRANSITIONS.size(); ++i) {
    const auto &t = ds::TRANSITIONS[i];
    const bool spec = t.code.find("hazard-c1-spec.md") != std::string_view::npos ||
                      t.code.find("hazard-c3-spec.md") != std::string_view::npos;
    if (i < 41) {
      TEST_ASSERT_TRUE(t.code.find("frag_") != std::string_view::npos);
      TEST_ASSERT_TRUE(t.code.find("orig ") != std::string_view::npos);
    } else {
      TEST_ASSERT_TRUE_MESSAGE(spec, t.code.data());
    }
  }
  for (const auto &t : ds::HOLD_TRANSITIONS) {
    TEST_ASSERT_TRUE(t.code.find("orig ") != std::string_view::npos);
  }
  for (const auto &g : ds::GATE_TRIGGERS) {
    TEST_ASSERT_TRUE(g.code.find("orig ") != std::string_view::npos);
  }
}

TEST_CASE("DSO-011 phase_of maps the code variables", "[drive_session_table]") {
  TEST_ASSERT_TRUE(ds::phase_of({true, false, false, false, false}) == Phase::LOCKED);
  TEST_ASSERT_TRUE(ds::phase_of({true, true, false, false, false}) == Phase::ASKING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, true, false, false}) == Phase::UNLOCKING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, false, false, false}) == Phase::DRIVING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, true, true, true}) == Phase::EXITING);
  TEST_ASSERT_TRUE(ds::phase_of({false, false, false, false, true}) == Phase::EXIT_REFUSED);
}

TEST_CASE("DSO-012 the table's data has the reviewed fingerprint", "[drive_session_table]") {
  // Also a static_assert in drive_session_fingerprint.hpp; printed here for the report.
  std::printf("FINGERPRINT 0x%016llX\n", static_cast<unsigned long long>(ds::table_fingerprint()));
  TEST_ASSERT_TRUE(ds::table_fingerprint() == ds::TABLE_FINGERPRINT);
}

namespace {
// Index of `x` in a row's actions, or kMaxActions when it is not there.
std::size_t index_of(const ds::Actions &a, ds::Action x) {
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] == x) {
      return i;
    }
  }
  return ds::kMaxActions;
}
} // namespace

TEST_CASE("DSO-013 every relock sets the menu-on-arrival flag to then_menu before it loads the "
          "Locked screen",
          "[drive_session_table]") {
  using ds::Action;
  // The rule, row by row: a row that loads the Locked screen has exactly one of
  // OPEN_MENU_ON_ARRIVAL / CLEAR_MENU_ON_ARRIVAL, before GO_LOCKED_SCREEN; no other row has
  // either. OPEN only where the row's guard requires THEN_MENU.
  std::size_t relocks = 0;
  for (const auto &t : ds::TRANSITIONS) {
    const std::size_t load = index_of(t.actions, Action::GO_LOCKED_SCREEN);
    const std::size_t open = index_of(t.actions, Action::OPEN_MENU_ON_ARRIVAL);
    const std::size_t clear = index_of(t.actions, Action::CLEAR_MENU_ON_ARRIVAL);
    if (load == ds::kMaxActions) {
      TEST_ASSERT_TRUE_MESSAGE(open == ds::kMaxActions && clear == ds::kMaxActions, t.code.data());
      continue;
    }
    ++relocks;
    TEST_ASSERT_TRUE_MESSAGE((open < load) != (clear < load), t.code.data());
    TEST_ASSERT_TRUE_MESSAGE(open == ds::kMaxActions || clear == ds::kMaxActions, t.code.data());
    const bool wants_menu = (t.guard.need_true & ds::bit(ds::Guard::THEN_MENU)) != 0;
    TEST_ASSERT_TRUE_MESSAGE(wants_menu == (open < load), t.code.data());
  }
  TEST_ASSERT_EQUAL_size_t(7, relocks); // rows 3-9
  // The same rule as a function of the state: in every valid combination a relock row
  // matches, the flag it writes is the hidden THEN_MENU (nav_menu_on_arrival := then_menu).
  unsigned long checked = 0;
  unsigned long bad = 0;
  for_each_valid([&](Phase p, GuardMask hidden, Input in, GuardMask guards) {
    const std::size_t r = ds::find_row(p, in, guards);
    if (r >= ds::TRANSITIONS.size()) {
      return;
    }
    const ds::Actions &a = ds::TRANSITIONS[r].actions;
    if (index_of(a, Action::GO_LOCKED_SCREEN) == ds::kMaxActions) {
      return;
    }
    ++checked;
    const bool then_menu = (hidden & ds::bit(ds::Guard::THEN_MENU)) != 0;
    const bool opens = index_of(a, Action::OPEN_MENU_ON_ARRIVAL) != ds::kMaxActions;
    bad += then_menu == opens ? 0UL : 1UL;
  });
  TEST_ASSERT_TRUE(checked > 0);
  TEST_ASSERT_EQUAL_UINT32(0, bad);
}

// ---- The hazard fix C1 (docs/plans/hazard-c1-spec.md §5.1) ----------------------------------

namespace {
bool row_has(const ds::Transition &t, ds::Action a) {
  return std::ranges::find(t.actions, a) != t.actions.end();
}
std::size_t row_number(const ds::Transition &t) {
  return static_cast<std::size_t>(&t - ds::TRANSITIONS.data()) + 1;
}
bool needs_true(const ds::Transition &t, ds::Guard g) {
  return (t.guard.need_true & ds::bit(g)) != 0;
}
bool needs_false(const ds::Transition &t, ds::Guard g) {
  return (t.guard.need_false & ds::bit(g)) != 0;
}
bool exit_phase(Phase p) { return p == Phase::EXITING || p == Phase::EXIT_REFUSED; }
} // namespace

TEST_CASE("DSO-014 STOP_FAULT only in the exit phases; every row from an exit phase to LOCKED "
          "clears it",
          "[drive_session_table][REQ-DRV-28]") {
  for (const auto &inv : ds::PHASE_INVARIANTS) {
    const bool must_be_clear = (inv.must_false & ds::bit(ds::Guard::STOP_FAULT)) != 0;
    TEST_ASSERT_EQUAL(!exit_phase(inv.phase), must_be_clear);
  }
  std::size_t ends = 0;
  for (const auto &t : ds::TRANSITIONS) {
    if (exit_phase(t.from) && t.to == Phase::LOCKED) {
      ++ends;
      TEST_ASSERT_TRUE_MESSAGE(row_has(t, ds::Action::CLEAR_STOP_FAULT), t.code.data());
    }
    if (row_has(t, ds::Action::RAISE_STOP_FAULT)) {
      TEST_ASSERT_TRUE(exit_phase(t.from) && t.to == t.from);
    }
  }
  TEST_ASSERT_EQUAL_size_t(3, ends); // rows 7-9
  TEST_ASSERT_TRUE(
      std::ranges::find(ds::DriveSession::SAFE_STATE_ACTIONS, ds::Action::CLEAR_STOP_FAULT) !=
      ds::DriveSession::SAFE_STATE_ACTIONS.end());
}

TEST_CASE("DSO-015 every row from an unlocked phase to LOCKED has SEND_DISABLE after GATE_UPDATE",
          "[drive_session_table][REQ-DRV-34]") {
  std::size_t relocks = 0;
  for (const auto &t : ds::TRANSITIONS) {
    if (ds::is_locked_phase(t.from) || t.to != Phase::LOCKED) {
      continue;
    }
    ++relocks;
    const auto gate = std::ranges::find(t.actions, ds::Action::GATE_UPDATE);
    const auto disable = std::ranges::find(t.actions, ds::Action::SEND_DISABLE);
    TEST_ASSERT_TRUE_MESSAGE(
        gate != t.actions.end() && disable != t.actions.end() && gate < disable, t.code.data());
  }
  TEST_ASSERT_EQUAL_size_t(7, relocks); // rows 3-9
}

TEST_CASE("DSO-016 SEND_ENABLE only in rows 18, 31 and 32; rows 31-32 need DRIVING_OK",
          "[drive_session_table][REQ-DRV-37]") {
  std::size_t enables = 0;
  for (const auto &t : ds::TRANSITIONS) {
    if (!row_has(t, ds::Action::SEND_ENABLE)) {
      continue;
    }
    ++enables;
    const std::size_t row = row_number(t);
    TEST_ASSERT_TRUE(row == 18 || row == 31 || row == 32);
    if (row != 18) {
      TEST_ASSERT_TRUE(needs_true(t, ds::Guard::DRIVING_OK));
    }
  }
  TEST_ASSERT_EQUAL_size_t(3, enables);
}

TEST_CASE("DSO-017 rows 1-2 read !CALIBRATING !ON_BOOT_SCREEN; ARM_STOP_TIMER only on "
          "UNLOCKING/DRIVING -> EXITING",
          "[drive_session_table][REQ-DRV-35]") {
  for (std::size_t i : {std::size_t{0}, std::size_t{1}}) {
    const auto &t = ds::TRANSITIONS[i];
    TEST_ASSERT_TRUE(t.input == Input::TICK_FOLLOW && t.to == Phase::UNLOCKING);
    TEST_ASSERT_TRUE(needs_true(t, ds::Guard::DRIVING_OK));
    TEST_ASSERT_TRUE(needs_false(t, ds::Guard::CALIBRATING));
    TEST_ASSERT_TRUE(needs_false(t, ds::Guard::ON_BOOT_SCREEN));
  }
  std::size_t arms = 0;
  for (const auto &t : ds::TRANSITIONS) {
    if (row_has(t, ds::Action::ARM_STOP_TIMER)) {
      ++arms;
      TEST_ASSERT_TRUE((t.from == Phase::UNLOCKING || t.from == Phase::DRIVING) &&
                       t.to == Phase::EXITING);
    }
  }
  TEST_ASSERT_EQUAL_size_t(4, arms); // rows 21, 22, 25, 26
}

TEST_CASE("DSO-018 TICK_SEQUENCE is exactly follow, boot stop, exit due, stop fault due, stop "
          "re-send, warn due, give-up due",
          "[drive_session_table][REQ-DRV-36]") {
  constexpr std::array kWant{Input::TICK_FOLLOW,      Input::TICK_BOOT_STOP,
                             Input::TICK_EXIT_DUE,    Input::TICK_STOP_FAULT_DUE,
                             Input::TICK_STOP_RESEND, Input::TICK_WARN_DUE,
                             Input::TICK_GIVEUP_DUE};
  TEST_ASSERT_EQUAL_size_t(kWant.size(), ds::TICK_SEQUENCE.size());
  for (std::size_t i = 0; i < kWant.size(); ++i) {
    TEST_ASSERT_TRUE(ds::TICK_SEQUENCE[i] == kWant[i]);
  }
}

TEST_CASE("DSO-019 the stop's D4 constants: 250, 5000, 1000 and 125 ms; the re-send rides the "
          "tick",
          "[drive_session_table][REQ-DRV-26]") {
  TEST_ASSERT_EQUAL_INT64(250, ds::kStopResend.count());
  TEST_ASSERT_EQUAL_INT64(5000, ds::kStopFaultAfter.count());
  TEST_ASSERT_EQUAL_INT64(1000, ds::kStopResendSlow.count());
  TEST_ASSERT_EQUAL_INT64(125, ds::kStopResendSlack.count());
  TEST_ASSERT_TRUE(ds::kStopResend == ds::kTickPeriod);
}

// ---- The hazard fix C3 (docs/plans/hazard-c3-spec.md §6.1) ----------------------------------

TEST_CASE("DSO-020 BOOT_STOP_DONE is true in every unlocked phase; MARK_BOOT_STOP only in rows "
          "50-51",
          "[drive_session_table][REQ-DRV-39]") {
  for (const auto &inv : ds::PHASE_INVARIANTS) {
    const bool must = (inv.must_true & ds::bit(ds::Guard::BOOT_STOP_DONE)) != 0;
    TEST_ASSERT_EQUAL(!ds::is_locked_phase(inv.phase), must);
  }
  std::size_t marks = 0;
  for (const auto &t : ds::TRANSITIONS) {
    if (row_has(t, ds::Action::MARK_BOOT_STOP)) {
      ++marks;
      TEST_ASSERT_TRUE(row_number(t) == 50 || row_number(t) == 51);
    }
  }
  TEST_ASSERT_EQUAL_size_t(2, marks);
  TEST_ASSERT_TRUE(
      std::ranges::find(ds::DriveSession::SAFE_STATE_ACTIONS, ds::Action::MARK_BOOT_STOP) ==
      ds::DriveSession::SAFE_STATE_ACTIONS.end());
}

TEST_CASE("DSO-021 every SEND_ENABLE row needs POST_OK (rows 18, 31, 32)",
          "[drive_session_table][REQ-DRV-37]") {
  std::size_t enables = 0;
  for (const auto &t : ds::TRANSITIONS) {
    if (row_has(t, ds::Action::SEND_ENABLE)) {
      ++enables;
      TEST_ASSERT_TRUE(needs_true(t, ds::Guard::POST_OK));
    }
  }
  TEST_ASSERT_EQUAL_size_t(3, enables);
}

TEST_CASE("DSO-022 rows 1-2 read +BOOT_STOP_DONE and do not read POST_OK",
          "[drive_session_table][REQ-DRV-35]") {
  for (std::size_t i : {std::size_t{0}, std::size_t{1}}) {
    const auto &t = ds::TRANSITIONS[i];
    TEST_ASSERT_TRUE(needs_true(t, ds::Guard::BOOT_STOP_DONE));
    TEST_ASSERT_FALSE(needs_true(t, ds::Guard::POST_OK) || needs_false(t, ds::Guard::POST_OK));
  }
}

TEST_CASE("DSO-023 UNLOCK_APPLIES needs POST_OK; SHOW_REFUSED_POST only with !POST_OK",
          "[drive_session_table][REQ-DRV-40][REQ-DRV-41]") {
  TEST_ASSERT_TRUE((ds::UNLOCK_APPLIES.guard.need_true & ds::bit(ds::Guard::POST_OK)) != 0);
  std::size_t refusals = 0;
  for (const auto &t : ds::TRANSITIONS) {
    if (row_has(t, ds::Action::SHOW_REFUSED_POST)) {
      ++refusals;
      TEST_ASSERT_TRUE(needs_false(t, ds::Guard::POST_OK));
      TEST_ASSERT_TRUE(needs_true(t, ds::Guard::MCB_READY));
    }
  }
  TEST_ASSERT_EQUAL_size_t(3, refusals); // rows 52-54
}
