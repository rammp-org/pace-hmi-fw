/// @file drive_session.cpp
/// @brief The drive session's hand-written transition function (CS-SAF-02).
///
/// One `switch` on Phase (decide), one `switch` on Input per phase (on_*), guards read as the
/// table's `Guard` bits, actions written as the table's `Action` values: no function pointers
/// (CS-SAF-08). Each branch names the table row it implements ("row N" = TRANSITIONS[N-1]). The
/// action lists below are written out here on purpose, not taken from the table, so that the
/// oracle (test/oracle) compares two independent statements of the same behaviour (TS-UNIT-08).
///
/// Every switch names every enumerator and adds a `default` that goes to the safe state
/// (CS-TYP-06, -Wswitch-enum): a value outside the enum can only come from corrupted memory.

#include "drive_session.hpp"

// The table's fingerprint is checked wherever the session is built (the firmware and the
// oracle): a table that differs from the reviewed one does not compile.
#include "drive_session_fingerprint.hpp"

namespace hmi::drive_session {

namespace {

[[nodiscard]] constexpr bool on(GuardMask g, Guard x) noexcept { return (g & bit(x)) != 0; }

// F1, drive_screen_follow_state branch 1: the chair drives and the HMI is locked (rows 1-2).
constexpr Actions UNLOCK_ON_DRIVING{Action::CLEAR_WARN,          Action::CLEAR_GIVEUP,
                                    Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_EXIT_REQUESTED,
                                    Action::CLEAR_THEN_MENU,     Action::LOCK_OPEN_VISUAL,
                                    Action::CANCEL_UNLOCK_TIMER, Action::START_UNLOCK_TIMER,
                                    Action::SET_UNLOCKED,        Action::GATE_UPDATE};
// F2, branch 2: stopped while unlocked. Never sends DISABLE (H5). Rows 3-9. The menu over
// the Locked screen only when the burger key asked (row 7); every other relock clears it.
constexpr Actions RELOCK_STOPPED{Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_EXIT_REQUESTED,
                                 Action::CLEAR_THEN_MENU,     Action::CLEAR_MENU_ON_ARRIVAL,
                                 Action::CANCEL_UNLOCK_TIMER, Action::RING_REST,
                                 Action::GO_LOCKED_SCREEN,    Action::SET_LOCKED,
                                 Action::GATE_UPDATE,         Action::SHOW_DRIVE_STOPPED};
constexpr Actions RELOCK_LOST{Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_EXIT_REQUESTED,
                              Action::CLEAR_THEN_MENU,     Action::CLEAR_MENU_ON_ARRIVAL,
                              Action::CANCEL_UNLOCK_TIMER, Action::RING_REST,
                              Action::GO_LOCKED_SCREEN,    Action::SET_LOCKED,
                              Action::GATE_UPDATE,         Action::SHOW_DRIVE_LOST};
constexpr Actions RELOCK_ASKED{
    Action::CLEAR_EXIT_DEADLINE,   Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU,
    Action::CLEAR_MENU_ON_ARRIVAL, Action::CANCEL_UNLOCK_TIMER,  Action::RING_REST,
    Action::GO_LOCKED_SCREEN,      Action::SET_LOCKED,           Action::GATE_UPDATE};
constexpr Actions RELOCK_ASKED_THEN_MENU{
    Action::CLEAR_EXIT_DEADLINE,  Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU,
    Action::OPEN_MENU_ON_ARRIVAL, Action::CANCEL_UNLOCK_TIMER,  Action::RING_REST,
    Action::GO_LOCKED_SCREEN,     Action::SET_LOCKED,           Action::GATE_UPDATE};
// F3 and F4 (rows 10-13).
constexpr Actions SEAT_REFUSED{Action::NAV_HOME, Action::SHOW_REFUSED_SEAT};
constexpr Actions RING_STOPS{Action::RING_REST};
// The three deadline checks of one tick (rows 14-17).
constexpr Actions EXIT_REFUSED{Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_THEN_MENU,
                               Action::SHOW_EXIT_REFUSED};
constexpr Actions NOT_GRANTED{Action::CLEAR_WARN, Action::SHOW_NOT_GRANTED};
constexpr Actions GIVE_UP{Action::CLEAR_GIVEUP, Action::SEND_DISABLE};
// activate_drive (rows 18-19).
constexpr Actions ASK_ENABLE{Action::RING_WAIT, Action::SEND_ENABLE, Action::ARM_WARN,
                             Action::ARM_GIVEUP};
constexpr Actions UNLOCK_REFUSED{Action::RING_REST, Action::SHOW_REFUSED_DRIVE,
                                 Action::REFUSAL_FEEDBACK};
// drive_exit_ask(false) and drive_exit_ask(true) (rows 21-28).
constexpr Actions ASK_EXIT{Action::SEND_DISABLE, Action::SET_EXIT_REQUESTED,
                           Action::CLEAR_THEN_MENU, Action::ARM_EXIT_DEADLINE};
constexpr Actions ASK_EXIT_THEN_MENU{Action::SEND_DISABLE, Action::SET_EXIT_REQUESTED,
                                     Action::SET_THEN_MENU, Action::ARM_EXIT_DEADLINE};
// The rest (rows 29-41).
constexpr Actions PUBLISH{Action::PUBLISH_DRIVE};
constexpr Actions ADVANCE{Action::UNLOCK_TIMER_DONE, Action::GO_DRIVE_SCREEN};
constexpr Actions ENTRY_REFUSED{Action::SHOW_REFUSED_DRIVE, Action::REFUSAL_FEEDBACK};
constexpr Actions MENU_ROW_REFUSED{Action::REFUSAL_FEEDBACK, Action::SHOW_REFUSED_DRIVE_MENU};
constexpr Actions NOTHING{};

// What an action does to the hidden variables. Only these actions touch them; every other
// action is the caller's to perform (LVGL, RTPS) and changes nothing here.
struct Effect {
  GuardMask sets;
  GuardMask clears;
};
constexpr Effect NO_EFFECT{.sets = 0, .clears = 0};
[[nodiscard]] constexpr Effect effect_of(Action a) noexcept {
  switch (a) {
  case Action::SEND_ENABLE:
    return Effect{.sets = bit(Guard::REQUEST_ENABLE), .clears = 0};
  case Action::SEND_DISABLE:
    return Effect{.sets = 0, .clears = bit(Guard::REQUEST_ENABLE)};
  case Action::ARM_WARN:
    return Effect{.sets = bit(Guard::WARN_ARMED), .clears = 0};
  case Action::CLEAR_WARN:
    return Effect{.sets = 0, .clears = bit(Guard::WARN_ARMED)};
  case Action::ARM_GIVEUP:
    return Effect{.sets = bit(Guard::GIVEUP_ARMED), .clears = 0};
  case Action::CLEAR_GIVEUP:
    return Effect{.sets = 0, .clears = bit(Guard::GIVEUP_ARMED)};
  case Action::SET_THEN_MENU:
    return Effect{.sets = bit(Guard::THEN_MENU), .clears = 0};
  case Action::CLEAR_THEN_MENU:
    return Effect{.sets = 0, .clears = bit(Guard::THEN_MENU)};
  case Action::START_UNLOCK_TIMER:
    return Effect{.sets = bit(Guard::UNLOCK_TIMER_ARMED), .clears = 0};
  case Action::CANCEL_UNLOCK_TIMER:
  case Action::UNLOCK_TIMER_DONE:
    return Effect{.sets = 0, .clears = bit(Guard::UNLOCK_TIMER_ARMED)};
  case Action::NONE:
  case Action::PUBLISH_DRIVE:
  case Action::ARM_EXIT_DEADLINE:
  case Action::CLEAR_EXIT_DEADLINE:
  case Action::SET_EXIT_REQUESTED:
  case Action::CLEAR_EXIT_REQUESTED:
  case Action::RING_WAIT:
  case Action::RING_REST:
  case Action::LOCK_OPEN_VISUAL:
  case Action::SET_UNLOCKED:
  case Action::SET_LOCKED:
  case Action::GO_LOCKED_SCREEN:
  case Action::GATE_UPDATE:
  case Action::OPEN_MENU_ON_ARRIVAL:
  case Action::CLEAR_MENU_ON_ARRIVAL:
  case Action::GO_DRIVE_SCREEN:
  case Action::NAV_HOME:
  case Action::SHOW_REFUSED_DRIVE:
  case Action::SHOW_REFUSED_SEAT:
  case Action::SHOW_NOT_GRANTED:
  case Action::SHOW_DRIVE_STOPPED:
  case Action::SHOW_EXIT_REFUSED:
  case Action::SHOW_DRIVE_LOST:
  case Action::SHOW_REFUSED_DRIVE_MENU:
  case Action::REFUSAL_FEEDBACK:
    return NO_EFFECT; // the caller's to perform
  default:
    break; // not an Action: no effect (only this file's constants reach perform)
  }
  return NO_EFFECT;
}

} // namespace

bool DriveSession::step(Input in, const Env &env, Actions &out) noexcept {
  const Outcome o = decide(in, env_guards(env) | hidden());
  if (!o.ok) {
    enter_safe_state(out);
    return false;
  }
  perform(o, out);
  return true;
}

void DriveSession::enter_safe_state(Actions &out) noexcept {
  perform(go(Phase::LOCKED, SAFE_STATE_ACTIONS), out);
}

DriveSession::Outcome DriveSession::decide(Input in, GuardMask g) const noexcept {
  switch (phase_) {
  case Phase::LOCKED:
    return on_locked(in, g);
  case Phase::ASKING:
    return on_asking(in, g);
  case Phase::UNLOCKING:
    return on_unlocking(in, g);
  case Phase::DRIVING:
    return on_driving(in, g);
  case Phase::EXITING:
    return on_exiting(in, g);
  case Phase::EXIT_REFUSED:
    return on_exit_refused(in, g);
  default:
    break; // corrupted phase: safe state
  }
  return fault();
}

DriveSession::Outcome DriveSession::stay() const noexcept { return go(phase_, NOTHING); }

DriveSession::Outcome DriveSession::on_locked(Input in, GuardMask g) const noexcept {
  switch (in) {
  case Input::TICK_FOLLOW:
    if (on(g, Guard::DRIVING_OK)) {
      return go(Phase::UNLOCKING, UNLOCK_ON_DRIVING); // row 1 (H1)
    }
    if (on(g, Guard::ON_SEAT_SCREEN) && !on(g, Guard::MCB_READY)) {
      return go(Phase::LOCKED, SEAT_REFUSED); // row 10
    }
    return stay();
  case Input::TICK_GIVEUP_DUE:
    if (on(g, Guard::GIVEUP_ARMED) && on(g, Guard::GIVEUP_ELAPSED)) {
      return go(Phase::LOCKED, GIVE_UP); // row 16
    }
    return stay();
  case Input::UNLOCK_HOLD_DONE:
    if (on(g, Guard::MCB_READY)) {
      return go(Phase::ASKING, ASK_ENABLE); // row 18
    }
    return go(Phase::LOCKED, UNLOCK_REFUSED); // row 19
  case Input::PROFILE_CLICK:
    return go(Phase::LOCKED, PUBLISH); // row 29 (H5)
  case Input::ENTRY_PUSH:
    if (on(g, Guard::ON_LOCKED_SCREEN) && !on(g, Guard::MENU_OPEN) && !on(g, Guard::MCB_READY)) {
      return go(Phase::LOCKED, ENTRY_REFUSED); // row 38
    }
    return stay();
  case Input::MENU_ROW_DRIVE:
    if (!on(g, Guard::MCB_READY)) {
      return go(Phase::LOCKED, MENU_ROW_REFUSED); // row 40
    }
    return stay();
  case Input::TICK_EXIT_DUE:
  case Input::TICK_WARN_DUE:
  case Input::EXIT_HOLD_DONE: // excluded by the table (U3): nothing here
  case Input::MENU_KEY_DRIVE:
  case Input::UNLOCK_TIMER:
    return stay();
  default:
    break; // corrupted input: safe state
  }
  return fault();
}

DriveSession::Outcome DriveSession::on_asking(Input in, GuardMask g) const noexcept {
  switch (in) {
  case Input::TICK_FOLLOW:
    if (on(g, Guard::DRIVING_OK)) {
      return go(Phase::UNLOCKING, UNLOCK_ON_DRIVING); // row 2
    }
    if (on(g, Guard::ON_SEAT_SCREEN) && !on(g, Guard::MCB_READY)) {
      return go(Phase::ASKING, SEAT_REFUSED); // row 11: before F4
    }
    if (!on(g, Guard::WARN_ARMED)) {
      return go(Phase::LOCKED, RING_STOPS); // rows 12-13
    }
    return stay();
  case Input::TICK_WARN_DUE:
    if (on(g, Guard::WARN_ARMED) && on(g, Guard::WARN_ELAPSED)) {
      return go(Phase::ASKING, NOT_GRANTED); // row 15
    }
    return stay();
  case Input::TICK_GIVEUP_DUE:
    if (on(g, Guard::GIVEUP_ARMED) && on(g, Guard::GIVEUP_ELAPSED)) {
      return go(Phase::ASKING, GIVE_UP); // row 17
    }
    return stay();
  case Input::PROFILE_CLICK:
    return go(Phase::ASKING, PUBLISH); // row 30
  case Input::ENTRY_PUSH:
    if (on(g, Guard::ON_LOCKED_SCREEN) && !on(g, Guard::MENU_OPEN) && !on(g, Guard::MCB_READY)) {
      return go(Phase::ASKING, ENTRY_REFUSED); // row 39
    }
    return stay();
  case Input::MENU_ROW_DRIVE:
    if (!on(g, Guard::MCB_READY)) {
      return go(Phase::ASKING, MENU_ROW_REFUSED); // row 41
    }
    return stay();
  case Input::UNLOCK_HOLD_DONE: // row 20: "already asked", nothing
  case Input::TICK_EXIT_DUE:
  case Input::EXIT_HOLD_DONE: // excluded by the table (U3): nothing here
  case Input::MENU_KEY_DRIVE:
  case Input::UNLOCK_TIMER:
    return stay();
  default:
    break; // corrupted input: safe state
  }
  return fault();
}

DriveSession::Outcome DriveSession::on_unlocking(Input in, GuardMask g) const noexcept {
  switch (in) {
  case Input::TICK_FOLLOW:
    if (!on(g, Guard::DRIVING_OK)) {
      // rows 3-4 (H5): which banner depends on the link
      return go(Phase::LOCKED, on(g, Guard::LINK_CONNECTED) ? RELOCK_STOPPED : RELOCK_LOST);
    }
    return stay();
  case Input::EXIT_HOLD_DONE:
    return go(Phase::EXITING, ASK_EXIT); // row 21
  case Input::MENU_KEY_DRIVE:
    return go(Phase::EXITING, ASK_EXIT_THEN_MENU); // row 25
  case Input::PROFILE_CLICK:
    return go(Phase::UNLOCKING, PUBLISH); // row 31
  case Input::UNLOCK_TIMER:
    if (on(g, Guard::UNLOCK_TIMER_ARMED)) {
      return go(Phase::DRIVING, ADVANCE); // row 35
    }
    return stay();
  case Input::TICK_EXIT_DUE:
  case Input::TICK_WARN_DUE:
  case Input::TICK_GIVEUP_DUE:
  case Input::UNLOCK_HOLD_DONE:
  case Input::ENTRY_PUSH:
  case Input::MENU_ROW_DRIVE:
    return stay();
  default:
    break; // corrupted input: safe state
  }
  return fault();
}

DriveSession::Outcome DriveSession::on_driving(Input in, GuardMask g) const noexcept {
  switch (in) {
  case Input::TICK_FOLLOW:
    if (!on(g, Guard::DRIVING_OK)) {
      // rows 5-6 (H5): which banner depends on the link
      return go(Phase::LOCKED, on(g, Guard::LINK_CONNECTED) ? RELOCK_STOPPED : RELOCK_LOST);
    }
    return stay();
  case Input::EXIT_HOLD_DONE:
    return go(Phase::EXITING, ASK_EXIT); // row 22
  case Input::MENU_KEY_DRIVE:
    return go(Phase::EXITING, ASK_EXIT_THEN_MENU); // row 26
  case Input::PROFILE_CLICK:
    return go(Phase::DRIVING, PUBLISH); // row 32
  case Input::TICK_EXIT_DUE:
  case Input::TICK_WARN_DUE:
  case Input::TICK_GIVEUP_DUE:
  case Input::UNLOCK_HOLD_DONE:
  case Input::UNLOCK_TIMER:
  case Input::ENTRY_PUSH:
  case Input::MENU_ROW_DRIVE:
    return stay();
  default:
    break; // corrupted input: safe state
  }
  return fault();
}

DriveSession::Outcome DriveSession::on_exiting(Input in, GuardMask g) const noexcept {
  switch (in) {
  case Input::TICK_FOLLOW:
    if (!on(g, Guard::DRIVING_OK)) {
      // rows 7-8: the user's own exit, no banner; the menu if the key asked for it
      return go(Phase::LOCKED, on(g, Guard::THEN_MENU) ? RELOCK_ASKED_THEN_MENU : RELOCK_ASKED);
    }
    return stay();
  case Input::TICK_EXIT_DUE:
    if (on(g, Guard::EXIT_ELAPSED)) {
      return go(Phase::EXIT_REFUSED, EXIT_REFUSED); // row 14 (H6)
    }
    return stay();
  case Input::EXIT_HOLD_DONE:
    return go(Phase::EXITING, ASK_EXIT); // row 23
  case Input::PROFILE_CLICK:
    return go(Phase::EXITING, PUBLISH); // row 33
  case Input::UNLOCK_TIMER:
    if (on(g, Guard::UNLOCK_TIMER_ARMED)) {
      return go(Phase::EXITING, ADVANCE); // row 36
    }
    return stay();
  case Input::MENU_KEY_DRIVE: // row 27: deadline armed, the key does nothing
  case Input::TICK_WARN_DUE:
  case Input::TICK_GIVEUP_DUE:
  case Input::UNLOCK_HOLD_DONE:
  case Input::ENTRY_PUSH:
  case Input::MENU_ROW_DRIVE:
    return stay();
  default:
    break; // corrupted input: safe state
  }
  return fault();
}

DriveSession::Outcome DriveSession::on_exit_refused(Input in, GuardMask g) const noexcept {
  switch (in) {
  case Input::TICK_FOLLOW:
    if (!on(g, Guard::DRIVING_OK)) {
      return go(Phase::LOCKED, RELOCK_ASKED); // row 9
    }
    return stay();
  case Input::EXIT_HOLD_DONE:
    return go(Phase::EXITING, ASK_EXIT); // row 24
  case Input::MENU_KEY_DRIVE:
    return go(Phase::EXITING, ASK_EXIT_THEN_MENU); // row 28
  case Input::PROFILE_CLICK:
    return go(Phase::EXIT_REFUSED, PUBLISH); // row 34
  case Input::UNLOCK_TIMER:
    if (on(g, Guard::UNLOCK_TIMER_ARMED)) {
      return go(Phase::EXIT_REFUSED, ADVANCE); // row 37
    }
    return stay();
  case Input::TICK_EXIT_DUE: // no timeout here (H6)
  case Input::TICK_WARN_DUE:
  case Input::TICK_GIVEUP_DUE:
  case Input::UNLOCK_HOLD_DONE:
  case Input::ENTRY_PUSH:
  case Input::MENU_ROW_DRIVE:
    return stay();
  default:
    break; // corrupted input: safe state
  }
  return fault();
}

void DriveSession::perform(const Outcome &o, Actions &out) noexcept {
  phase_ = o.to;
  for (const Action a : o.actions) {
    const Effect e = effect_of(a);
    hidden_ = (hidden_ | e.sets) & ~e.clears;
  }
  out = o.actions;
}

} // namespace hmi::drive_session
