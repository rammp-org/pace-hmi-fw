// SPDX-License-Identifier: MIT
//
// drive_session_table.hpp: the AS-IS drive session of pace-hmi-fw, as data.
//
// Rows 1-41 are the characterisation of the CODE of e2047a4 (main.cpp split into
// main/frag_*.inc, one translation unit), not of its comments; where a comment disagrees with
// the code, the code wins and the disagreement is listed in TABLE.md. The hazard fix C1
// (docs/plans/hazard-c1-spec.md, approved by the owner 2026-10-08) changes rows 1-9, 21, 22,
// 25, 26, 31 and 32 and appends rows 42-49; the hazard fix C3 (hazard-c3-spec.md, approved the
// same day) changes rows 1, 2, 18, 31 and 32 and appends rows 50-54; each of those rows cites
// its spec. Any further change to a row is a behaviour change and needs the owner's approval
// (CS-SAF-05).
//
// This file is the spec, the test oracle (TS-UNIT-08) and the source of the D4
// diagram in TABLE.md (CS-SAF-02, CS-TYP-03). The agent that writes the
// extraction never edits it (CORE never-list: declarations).
//
// Pure C++23: no LVGL, no ESP-IDF, no FreeRTOS.
//
// ---------------------------------------------------------------------------
// MODEL
// ---------------------------------------------------------------------------
// State = Phase + a mask of HIDDEN variables (the Guard values in the
// "hidden" group below). Phase is a function of code variables (phase_of);
// the hidden variables are everything else the session remembers.
//
// Input  = one of the events below. The 250 ms rtps_poll_cb tick is NOT one
//          input: drive_wait_poll() runs drive_screen_follow_state() first and
//          then the other sub-steps in sequence, each seeing the state the
//          previous one left. So a TICK is the sub-inputs of TICK_SEQUENCE,
//          applied in that order, with TWO Envs: follow-state reads the
//          subjects, screen and menu as they are when the tick starts; the
//          other sub-steps share one Env sampled after follow-state's actions
//          ran (`now` is read once, then). A deadline or a re-send that falls
//          due between the two samples is acted on in the same tick (DRV-116).
//          There is no MIB_UPDATE event: a MibStatus only writes subjects; the
//          session looks at them on the next tick or the next user input.
//
// Guard  = a conjunction over guard bits: need_true all set, need_false all
//          clear. Env guards come from env_guards(Env); hidden guards come
//          from the state's hidden mask.
//
// Priority rule: within one (from, input), rows are pairwise EXCLUSIVE
// (static_assert below), so their order does not matter. Order matters only
// BETWEEN sub-inputs of one tick (TICK_SEQUENCE) and between the steps of one
// hold poll (HOLD_POLL_SEQUENCE). The rows are listed in the order the code
// evaluates them.
//
// Oracle contract (TS-UNIT-08): for every (phase, hidden, input, env) that
// satisfies the phase's invariant (PHASE_INVARIANTS) and the input's
// precondition (INPUT_PRECONDITIONS):
//   - exactly zero or one row matches (find_row);
//   - if one matches: new phase = row.to, new hidden = apply(row.actions),
//     and the actions are performed in row order;
//   - if none matches: nothing changes and nothing is sent or shown.
// Action -> hidden-variable effects are ACTION_EFFECTS: the oracle is fully
// determined by this file.
//
// Line references: "frag_X.inc:L (orig N)" where N is the line in main.cpp at
// e2047a4: N = (fragment header's first original line) + L - 2, because line 1
// of each fragment is the split_main.py header. main.cpp's own lines after the
// fragment includes are N = L + 4622.

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <string_view>

// The enums, helpers and row structs (moved there unchanged, owner-approved 2026-10-06).
#include "drive_session_types.hpp"
// The hold gestures' tables (moved there unchanged, 2026-10-08: CS-FIL-01).
#include "drive_session_hold.hpp"

namespace hmi::drive_session {

// ---------------------------------------------------------------------------
// Timing, as the code has it (CS-TYP-01: units in types)
// ---------------------------------------------------------------------------
using std::chrono::milliseconds;
inline constexpr milliseconds kTickPeriod{250}; // kRtpsPollMs, frag_stick_config.inc:39 (orig 517)
inline constexpr milliseconds kHoldPollPeriod{
    33};                                       // kHoldPollMs, frag_stick_button.inc:101 (orig 952)
inline constexpr milliseconds kBarGrace{500};  // kBarGraceMs, frag_stick_button.inc:93 (orig 944)
inline constexpr milliseconds kHoldFill{1000}; // kHoldMs, frag_stick_button.inc:89 (orig 940)
inline constexpr milliseconds kUnlockAdvance{
    1000}; // kUnlockAdvanceMs, frag_stick_button.inc:97 (orig 948)
inline constexpr milliseconds kUnlockDissolve{
    280}; // kUnlockDissolveMs, frag_hold.inc:113 (orig 1065)
inline constexpr milliseconds kMibStatusPeriod{500};   // hmi_rtps_spec.hpp:103
inline constexpr milliseconds kMibStatusTimeout{2000}; // hmi_rtps_spec.hpp:104
// kDriveAnswerUs = period + 250 ms: the warn deadline, and the exit deadline.
inline constexpr milliseconds kDriveAnswer = kMibStatusPeriod + milliseconds{250};
// kDriveWaitUs = MibStatus timeout: the give-up deadline.
inline constexpr milliseconds kDriveWait = kMibStatusTimeout;
inline constexpr milliseconds kDriveRefusedShow{3000}; // frag_refusal.inc:38 (orig 1229)
inline constexpr milliseconds kExitRefusedShow{2000};  // frag_refusal.inc:39 (orig 1230)
static_assert(kDriveAnswer == milliseconds{750});
static_assert(kDriveAnswer < kDriveWait, "warn comes before give-up");

// The user's stop (hazard fix C1, G4; values approved, D4): DISABLE re-sent every kStopResend,
// "MCB did not stop" kStopFaultAfter after the first stop of the exit, then re-sent every
// kStopResendSlow. The re-send rides the tick; kStopResendSlack absorbs its jitter, so FAST is
// due on every tick and SLOW on every 4th.
inline constexpr milliseconds kStopResend{250};
inline constexpr milliseconds kStopFaultAfter{5000};
inline constexpr milliseconds kStopResendSlow{1000};
inline constexpr milliseconds kStopResendSlack = kTickPeriod / 2;
static_assert(kStopResend == kTickPeriod, "the re-send rides the tick");
static_assert(kStopResendSlack == milliseconds{125});
static_assert(kStopResendSlow > kStopResend && kStopFaultAfter > kStopResendSlow);

// One rtps_poll_cb tick, in order (drive_wait_poll, frag_drive.inc:99-122,
// orig 1622-1645; hazard-c1-spec.md §2.5). follow_state runs BEFORE the other
// sub-steps, so a deadline that expires on tick n is acted on by follow_state only on
// tick n+1 (e.g. ASKING + warn: NOT_GRANTED on tick n, ring rest on tick n+1). FOLLOW
// first: a relock ends the stop before any re-send. BOOT_STOP right after FOLLOW (C3 §2.5): the
// first CONNECTED tick sends the boot DISABLE and does not enter Drive. EXIT_DUE before the
// stop steps: the phase they read is final. STOP_FAULT_DUE before STOP_RESEND: on the fault
// tick the rate switches to 1 Hz at once.
inline constexpr std::array TICK_SEQUENCE{
    Input::TICK_FOLLOW,         Input::TICK_BOOT_STOP,   Input::TICK_EXIT_DUE,
    Input::TICK_STOP_FAULT_DUE, Input::TICK_STOP_RESEND, Input::TICK_WARN_DUE,
    Input::TICK_GIVEUP_DUE,
};

// What each action does to the HIDDEN variables. Phase changes are the row's
// `to`; these are the rest. An action not listed changes no hidden variable.
inline constexpr std::array ACTION_EFFECTS{
    ActionEffect{Action::SEND_ENABLE, bit(Guard::REQUEST_ENABLE), 0},
    ActionEffect{Action::SEND_DISABLE, 0, bit(Guard::REQUEST_ENABLE)},
    ActionEffect{Action::ARM_WARN, bit(Guard::WARN_ARMED), 0},
    ActionEffect{Action::ARM_GIVEUP, bit(Guard::GIVEUP_ARMED), 0},
    ActionEffect{Action::CLEAR_WARN, 0, bit(Guard::WARN_ARMED)},
    ActionEffect{Action::CLEAR_GIVEUP, 0, bit(Guard::GIVEUP_ARMED)},
    ActionEffect{Action::SET_THEN_MENU, bit(Guard::THEN_MENU), 0},
    ActionEffect{Action::CLEAR_THEN_MENU, 0, bit(Guard::THEN_MENU)},
    ActionEffect{Action::CANCEL_UNLOCK_TIMER, 0, bit(Guard::UNLOCK_TIMER_ARMED)},
    ActionEffect{Action::START_UNLOCK_TIMER, bit(Guard::UNLOCK_TIMER_ARMED), 0},
    ActionEffect{Action::UNLOCK_TIMER_DONE, 0, bit(Guard::UNLOCK_TIMER_ARMED)},
    ActionEffect{Action::RAISE_STOP_FAULT, bit(Guard::STOP_FAULT), 0},
    ActionEffect{Action::CLEAR_STOP_FAULT, 0, bit(Guard::STOP_FAULT)},
    ActionEffect{Action::MARK_BOOT_STOP, bit(Guard::BOOT_STOP_DONE), 0},
};

constexpr GuardMask apply(const Actions &a, GuardMask hidden) noexcept {
  for (Action x : a) {
    for (const ActionEffect &e : ACTION_EFFECTS) {
      if (e.action == x) {
        hidden = (hidden | e.sets) & ~e.clears;
      }
    }
  }
  return hidden;
}

// ---------------------------------------------------------------------------
// Phase invariants on the hidden variables (derived from every write site)
// ---------------------------------------------------------------------------
// must_true / must_false: bits that are always set / clear in that phase.
// WARN_ARMED => ASKING: armed only by drive_request_enter right after
//   lock_visual_wait; ASKING leaves only by F1 (clears it) or F4 (needs it 0).
// GIVEUP_ARMED => locked: F1 clears it before any unlock.
// THEN_MENU => EXITING: set only with an armed exit deadline, cleared with
//   it at the deadline (frag_drive.inc:107) and on every lock/unlock.
// REQUEST_ENABLE is false in EXITING/EXIT_REFUSED: drive_exit_ask sends
//   DISABLE and nothing sends ENABLE while unlocked.
// UNLOCK_TIMER_ARMED: armed only by set_locked(false) (enter UNLOCKING),
//   cleared by set_locked(*) and by firing; survives UNLOCKING -> EXITING.
// STOP_FAULT => EXITING or EXIT_REFUSED (C1): raised only there (rows 44-45), cleared by
//   every row from them to LOCKED (rows 7-9) and by the safe state.
// BOOT_STOP_DONE in every unlocked phase (C3): only rows 1-2 leave the locked phases, and both
//   need it; nothing clears it (the safe state does not mark it either).
inline constexpr std::array PHASE_INVARIANTS{
    PhaseInvariant{
        Phase::LOCKED, 0,
        mask({Guard::WARN_ARMED, Guard::THEN_MENU, Guard::UNLOCK_TIMER_ARMED, Guard::STOP_FAULT})},
    PhaseInvariant{Phase::ASKING, 0,
                   mask({Guard::THEN_MENU, Guard::UNLOCK_TIMER_ARMED, Guard::STOP_FAULT})},
    PhaseInvariant{
        Phase::UNLOCKING, mask({Guard::UNLOCK_TIMER_ARMED, Guard::BOOT_STOP_DONE}),
        mask({Guard::WARN_ARMED, Guard::GIVEUP_ARMED, Guard::THEN_MENU, Guard::STOP_FAULT})},
    PhaseInvariant{Phase::DRIVING, bit(Guard::BOOT_STOP_DONE),
                   mask({Guard::WARN_ARMED, Guard::GIVEUP_ARMED, Guard::THEN_MENU,
                         Guard::UNLOCK_TIMER_ARMED, Guard::STOP_FAULT})},
    PhaseInvariant{Phase::EXITING, bit(Guard::BOOT_STOP_DONE),
                   mask({Guard::WARN_ARMED, Guard::GIVEUP_ARMED, Guard::REQUEST_ENABLE})},
    PhaseInvariant{
        Phase::EXIT_REFUSED, bit(Guard::BOOT_STOP_DONE),
        mask({Guard::WARN_ARMED, Guard::GIVEUP_ARMED, Guard::THEN_MENU, Guard::REQUEST_ENABLE})},
};

constexpr const PhaseInvariant &invariant_of(Phase p) noexcept {
  return PHASE_INVARIANTS[static_cast<std::size_t>(p)];
}
constexpr bool hidden_valid(Phase p, GuardMask hidden) noexcept {
  const PhaseInvariant &inv = invariant_of(p);
  return (hidden & ~kHiddenGuards) == 0 && (hidden & inv.must_true) == inv.must_true &&
         (hidden & inv.must_false) == 0;
}

// ---------------------------------------------------------------------------
// Input preconditions: which (phase, guards) an input can arrive in at all.
// The oracle drives only these. Everything excluded is listed with its reason
// in TABLE.md ("Excluded combinations").
// ---------------------------------------------------------------------------
inline constexpr std::array INPUT_PRECONDITIONS{
    InputPrecondition{Input::TICK_FOLLOW, kAllPhases, kAlways},
    InputPrecondition{Input::TICK_EXIT_DUE, kAllPhases, kAlways},
    InputPrecondition{Input::TICK_WARN_DUE, kAllPhases, kAlways},
    InputPrecondition{Input::TICK_GIVEUP_DUE, kAllPhases, kAlways},
    // Completion lands up to one hold poll after applies() was last true, so
    // the phase may have moved on: activate_drive re-checks (frag_drive.inc:145).
    InputPrecondition{Input::UNLOCK_HOLD_DONE, kAllPhases, kAlways},
    // Every phase (C1): an exit hold completing while locked (TABLE.md U3) is rows 42-43.
    InputPrecondition{Input::EXIT_HOLD_DONE, kAllPhases, kAlways},
    // By definition of the input (frag_nav.inc:491).
    InputPrecondition{Input::MENU_KEY_DRIVE, kUnlockedPhases,
                      when({Guard::ON_DRIVE_SCREEN}, {Guard::MENU_OPEN})},
    InputPrecondition{Input::PROFILE_CLICK, kAllPhases, kAlways},
    InputPrecondition{Input::UNLOCK_TIMER, kAllPhases, when({Guard::UNLOCK_TIMER_ARMED})},
    InputPrecondition{Input::ENTRY_PUSH, kAllPhases, kAlways},
    InputPrecondition{Input::MENU_ROW_DRIVE, kAllPhases, kAlways},
    // The stop's tick sub-steps (C1): every phase, always.
    InputPrecondition{Input::TICK_STOP_FAULT_DUE, kAllPhases, kAlways},
    InputPrecondition{Input::TICK_STOP_RESEND, kAllPhases, kAlways},
    // The boot DISABLE's tick sub-step (C3): every phase, always.
    InputPrecondition{Input::TICK_BOOT_STOP, kAllPhases, kAlways},
};

// ---------------------------------------------------------------------------
// The transition table
// ---------------------------------------------------------------------------

// drive_screen_follow_state, branch 1 (frag_drive.inc:51-62): the chair drives
// and the HMI is locked -> unlock, asked or not (H1, accepted by M1). set_locked(false) deletes
// any armed timer first (none can be armed while locked), arms the advance,
// writes the subject, then updates the gate (shut: the screen is not Drive yet).
inline constexpr Actions kF1Unlock =
    acts({Action::CLEAR_WARN, Action::CLEAR_GIVEUP, Action::CLEAR_EXIT_DEADLINE,
          Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU, Action::LOCK_OPEN_VISUAL,
          Action::CANCEL_UNLOCK_TIMER, Action::START_UNLOCK_TIMER, Action::SET_UNLOCKED,
          Action::GATE_UPDATE});

// drive_screen_follow_state, branch 2 (frag_drive.inc:64-83): the clears, then
// nav_menu_on_arrival := then_menu (frag_drive.inc:75, orig 1598: OPEN_MENU_ON_ARRIVAL
// when the burger key asked, CLEAR_MENU_ON_ARRIVAL otherwise), then
// set_locked(true) = cancel timer, lock_visual_rest, instant Locked screen,
// subject, gate. Then one DISABLE (C1, H5): the gate shuts first, the request is DISABLE
// after. The banner only when the stop was not asked (exit_requested false). From the exit
// phases the stop also ends: CLEAR_STOP_FAULT, last.
inline constexpr Actions kF2LockStopped =
    acts({Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU,
          Action::CLEAR_MENU_ON_ARRIVAL, Action::CANCEL_UNLOCK_TIMER, Action::RING_REST,
          Action::GO_LOCKED_SCREEN, Action::SET_LOCKED, Action::GATE_UPDATE, Action::SEND_DISABLE,
          Action::SHOW_DRIVE_STOPPED});
inline constexpr Actions kF2LockLost =
    acts({Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU,
          Action::CLEAR_MENU_ON_ARRIVAL, Action::CANCEL_UNLOCK_TIMER, Action::RING_REST,
          Action::GO_LOCKED_SCREEN, Action::SET_LOCKED, Action::GATE_UPDATE, Action::SEND_DISABLE,
          Action::SHOW_DRIVE_LOST});
inline constexpr Actions kF2LockAsked =
    acts({Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU,
          Action::CLEAR_MENU_ON_ARRIVAL, Action::CANCEL_UNLOCK_TIMER, Action::RING_REST,
          Action::GO_LOCKED_SCREEN, Action::SET_LOCKED, Action::GATE_UPDATE, Action::SEND_DISABLE,
          Action::CLEAR_STOP_FAULT});
inline constexpr Actions kF2LockAskedMenu =
    acts({Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU,
          Action::OPEN_MENU_ON_ARRIVAL, Action::CANCEL_UNLOCK_TIMER, Action::RING_REST,
          Action::GO_LOCKED_SCREEN, Action::SET_LOCKED, Action::GATE_UPDATE, Action::SEND_DISABLE,
          Action::CLEAR_STOP_FAULT});

// drive_exit_ask(then_menu) (frag_drive.inc:177-182): DISABLE, latch, menu flag,
// a fresh deadline. Unconditional: re-sends and re-arms from EXITING too.
inline constexpr Actions kExitAskHold = acts({Action::SEND_DISABLE, Action::SET_EXIT_REQUESTED,
                                              Action::CLEAR_THEN_MENU, Action::ARM_EXIT_DEADLINE});
inline constexpr Actions kExitAskMenu = acts({Action::SEND_DISABLE, Action::SET_EXIT_REQUESTED,
                                              Action::SET_THEN_MENU, Action::ARM_EXIT_DEADLINE});
// The first stop of an exit, from UNLOCKING or DRIVING, also arms the stop timer (C1, G4: the
// 5 s count from the first stop); a repeated stop keeps it (rows 23, 24, 28).
inline constexpr Actions kFirstExitAskHold =
    acts({Action::SEND_DISABLE, Action::SET_EXIT_REQUESTED, Action::CLEAR_THEN_MENU,
          Action::ARM_EXIT_DEADLINE, Action::ARM_STOP_TIMER});
inline constexpr Actions kFirstExitAskMenu =
    acts({Action::SEND_DISABLE, Action::SET_EXIT_REQUESTED, Action::SET_THEN_MENU,
          Action::ARM_EXIT_DEADLINE, Action::ARM_STOP_TIMER});

inline constexpr Actions kAdvance = acts({Action::UNLOCK_TIMER_DONE, Action::GO_DRIVE_SCREEN});
inline constexpr Actions kNoActions = acts({});

inline constexpr std::array TRANSITIONS{
    // ===== TICK_FOLLOW: drive_screen_follow_state, branches tried in code order
    // F1: driving && locked (M1: asked or not), unless a calibration runs (G5) or the Boot
    // screen is up (U4), and only once the boot DISABLE is done (C3). POST is not read: the
    // MCB's ENABLED enters Drive before POST pass, the stick held (G3).
    Transition{Phase::LOCKED, Input::TICK_FOLLOW,
               when({Guard::DRIVING_OK, Guard::BOOT_STOP_DONE},
                    {Guard::CALIBRATING, Guard::ON_BOOT_SCREEN}),
               Phase::UNLOCKING, kF1Unlock,
               "frag_drive.inc:51-62 (orig 1574-1585); hazard-c1-spec.md 2.3 row 1; "
               "hazard-c3-spec.md 4.2"},
    Transition{Phase::ASKING, Input::TICK_FOLLOW,
               when({Guard::DRIVING_OK, Guard::BOOT_STOP_DONE},
                    {Guard::CALIBRATING, Guard::ON_BOOT_SCREEN}),
               Phase::UNLOCKING, kF1Unlock,
               "frag_drive.inc:51-62 (orig 1574-1585); hazard-c1-spec.md 2.3 row 2; "
               "hazard-c3-spec.md 4.2"},
    // F2: !driving && !locked. Banner only when not asked; which banner by link. One DISABLE.
    Transition{Phase::UNLOCKING, Input::TICK_FOLLOW,
               when({Guard::LINK_CONNECTED}, {Guard::DRIVING_OK}), Phase::LOCKED, kF2LockStopped,
               "frag_drive.inc:64-83 (orig 1587-1606); hazard-c1-spec.md 2.3 row 3"},
    Transition{Phase::UNLOCKING, Input::TICK_FOLLOW,
               when({}, {Guard::DRIVING_OK, Guard::LINK_CONNECTED}), Phase::LOCKED, kF2LockLost,
               "frag_drive.inc:64-83 (orig 1587-1606); hazard-c1-spec.md 2.3 row 4"},
    Transition{Phase::DRIVING, Input::TICK_FOLLOW,
               when({Guard::LINK_CONNECTED}, {Guard::DRIVING_OK}), Phase::LOCKED, kF2LockStopped,
               "frag_drive.inc:64-83 (orig 1587-1606); hazard-c1-spec.md 2.3 row 5"},
    Transition{Phase::DRIVING, Input::TICK_FOLLOW,
               when({}, {Guard::DRIVING_OK, Guard::LINK_CONNECTED}), Phase::LOCKED, kF2LockLost,
               "frag_drive.inc:64-83 (orig 1587-1606); hazard-c1-spec.md 2.3 row 6"},
    Transition{Phase::EXITING, Input::TICK_FOLLOW, when({Guard::THEN_MENU}, {Guard::DRIVING_OK}),
               Phase::LOCKED, kF2LockAskedMenu,
               "frag_drive.inc:64-83 (orig 1587-1606); hazard-c1-spec.md 2.3 row 7"},
    Transition{Phase::EXITING, Input::TICK_FOLLOW, when({}, {Guard::DRIVING_OK, Guard::THEN_MENU}),
               Phase::LOCKED, kF2LockAsked,
               "frag_drive.inc:64-83 (orig 1587-1606); hazard-c1-spec.md 2.3 row 8"},
    Transition{Phase::EXIT_REFUSED, Input::TICK_FOLLOW, when({}, {Guard::DRIVING_OK}),
               Phase::LOCKED, kF2LockAsked,
               "frag_drive.inc:64-83 (orig 1587-1606); hazard-c1-spec.md 2.3 row 9"},
    // F3: on Seat without the MCB (only reachable locked: unlocked && !driving took F2,
    // and driving implies mcb_ready). Returns before F4: the ring rest waits a tick.
    Transition{Phase::LOCKED, Input::TICK_FOLLOW,
               when({Guard::ON_SEAT_SCREEN}, {Guard::DRIVING_OK, Guard::MCB_READY}), Phase::LOCKED,
               acts({Action::NAV_HOME, Action::SHOW_REFUSED_SEAT}),
               "frag_drive.inc:87-90 (orig 1610-1613)"},
    Transition{Phase::ASKING, Input::TICK_FOLLOW,
               when({Guard::ON_SEAT_SCREEN}, {Guard::DRIVING_OK, Guard::MCB_READY}), Phase::ASKING,
               acts({Action::NAV_HOME, Action::SHOW_REFUSED_SEAT}),
               "frag_drive.inc:87-90 (orig 1610-1613)"},
    // F4: locked && lock_waiting && warn == 0 -> the ring stops.
    Transition{Phase::ASKING, Input::TICK_FOLLOW,
               when({}, {Guard::DRIVING_OK, Guard::WARN_ARMED, Guard::ON_SEAT_SCREEN}),
               Phase::LOCKED, acts({Action::RING_REST}), "frag_drive.inc:94-96 (orig 1617-1619)"},
    Transition{
        Phase::ASKING, Input::TICK_FOLLOW,
        when({Guard::ON_SEAT_SCREEN, Guard::MCB_READY}, {Guard::DRIVING_OK, Guard::WARN_ARMED}),
        Phase::LOCKED, acts({Action::RING_REST}), "frag_drive.inc:94-96 (orig 1617-1619)"},

    // ===== TICK_EXIT_DUE: refused exit; the latch stays. The DISABLE re-send is rows 46-49
    // (C1), which run in EXIT_REFUSED too.
    Transition{
        Phase::EXITING, Input::TICK_EXIT_DUE, when({Guard::EXIT_ELAPSED}), Phase::EXIT_REFUSED,
        acts({Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_THEN_MENU, Action::SHOW_EXIT_REFUSED}),
        "frag_drive.inc:102-109 (orig 1625-1632)"},

    // ===== TICK_WARN_DUE: no ENABLED inside the answer window. Ring keeps
    // spinning until the next tick's F4.
    Transition{Phase::ASKING, Input::TICK_WARN_DUE, when({Guard::WARN_ARMED, Guard::WARN_ELAPSED}),
               Phase::ASKING, acts({Action::CLEAR_WARN, Action::SHOW_NOT_GRANTED}),
               "frag_drive.inc:110-115 (orig 1633-1638)"},

    // ===== TICK_GIVEUP_DUE: stop asking.
    Transition{Phase::LOCKED, Input::TICK_GIVEUP_DUE,
               when({Guard::GIVEUP_ARMED, Guard::GIVEUP_ELAPSED}), Phase::LOCKED,
               acts({Action::CLEAR_GIVEUP, Action::SEND_DISABLE}),
               "frag_drive.inc:116-121 (orig 1639-1644)"},
    Transition{Phase::ASKING, Input::TICK_GIVEUP_DUE,
               when({Guard::GIVEUP_ARMED, Guard::GIVEUP_ELAPSED}), Phase::ASKING,
               acts({Action::CLEAR_GIVEUP, Action::SEND_DISABLE}),
               "frag_drive.inc:116-121 (orig 1639-1644)"},

    // ===== UNLOCK_HOLD_DONE: activate_drive; no ENABLE before POST pass (C3, H2: row 52)
    Transition{Phase::LOCKED, Input::UNLOCK_HOLD_DONE, when({Guard::MCB_READY, Guard::POST_OK}),
               Phase::ASKING,
               acts({Action::RING_WAIT, Action::SEND_ENABLE, Action::ARM_WARN, Action::ARM_GIVEUP}),
               "frag_drive.inc:154-155,134-137 (orig 1677-1678,1657-1660); hazard-c3-spec.md 4.2"},
    Transition{Phase::LOCKED, Input::UNLOCK_HOLD_DONE, when({}, {Guard::MCB_READY}), Phase::LOCKED,
               acts({Action::RING_REST, Action::SHOW_REFUSED_DRIVE, Action::REFUSAL_FEEDBACK}),
               "frag_drive.inc:148-152 (orig 1671-1675)"},
    // "already asked": ignored (applies() also refuses to start a fill here).
    Transition{Phase::ASKING, Input::UNLOCK_HOLD_DONE, kAlways, Phase::ASKING, kNoActions,
               "frag_drive.inc:145-146 (orig 1668-1669); frag_lock.inc:96 (orig 1161)"},

    // ===== EXIT_HOLD_DONE: drive_exit_ask(false), no phase check in the code; the first stop
    // arms the stop timer (C1, G4)
    Transition{Phase::UNLOCKING, Input::EXIT_HOLD_DONE, kAlways, Phase::EXITING, kFirstExitAskHold,
               "frag_drive.inc:198,177-182 (orig 1721,1700-1705); hazard-c1-spec.md 2.3 row 21"},
    Transition{Phase::DRIVING, Input::EXIT_HOLD_DONE, kAlways, Phase::EXITING, kFirstExitAskHold,
               "frag_drive.inc:198,177-182 (orig 1721,1700-1705); hazard-c1-spec.md 2.3 row 22"},
    // Re-sends DISABLE, resets the deadline, and overwrites then_menu := false.
    Transition{Phase::EXITING, Input::EXIT_HOLD_DONE, kAlways, Phase::EXITING, kExitAskHold,
               "frag_drive.inc:198,177-182 (orig 1721,1700-1705)"},
    Transition{Phase::EXIT_REFUSED, Input::EXIT_HOLD_DONE, kAlways, Phase::EXITING, kExitAskHold,
               "frag_drive.inc:198,177-182 (orig 1721,1700-1705)"},

    // ===== MENU_KEY_DRIVE: nav_key_cb on Drive, unlocked: ask to stop, menu after; the first
    // stop arms the stop timer (C1, G4)
    Transition{Phase::UNLOCKING, Input::MENU_KEY_DRIVE, kAlways, Phase::EXITING, kFirstExitAskMenu,
               "frag_nav.inc:491-495 (orig 3719-3723); hazard-c1-spec.md 2.3 row 25"},
    Transition{Phase::DRIVING, Input::MENU_KEY_DRIVE, kAlways, Phase::EXITING, kFirstExitAskMenu,
               "frag_nav.inc:491-495 (orig 3719-3723); hazard-c1-spec.md 2.3 row 26"},
    // drive_exit_until_us != 0: the key does nothing (no menu, no re-send).
    Transition{Phase::EXITING, Input::MENU_KEY_DRIVE, kAlways, Phase::EXITING, kNoActions,
               "frag_nav.inc:492,495 (orig 3720,3723)"},
    // Deadline is 0 after a refusal: the key re-sends DISABLE and re-arms.
    Transition{Phase::EXIT_REFUSED, Input::MENU_KEY_DRIVE, kAlways, Phase::EXITING, kExitAskMenu,
               "frag_nav.inc:491-495 (orig 3719-3723)"},

    // ===== PROFILE_CLICK: re-publishes drive_request as it stands, except unlocked before an
    // exit: there ENABLE with the new profile, only with the MCB ENABLED (C1, H5) and POST
    // passed (C3, H2)
    Transition{Phase::LOCKED, Input::PROFILE_CLICK, kAlways, Phase::LOCKED,
               acts({Action::PUBLISH_DRIVE}), "frag_drive_band.inc:22-23 (orig 587-588)"},
    Transition{Phase::ASKING, Input::PROFILE_CLICK, kAlways, Phase::ASKING,
               acts({Action::PUBLISH_DRIVE}), "frag_drive_band.inc:22-23 (orig 587-588)"},
    Transition{Phase::UNLOCKING, Input::PROFILE_CLICK, when({Guard::DRIVING_OK, Guard::POST_OK}),
               Phase::UNLOCKING, acts({Action::SEND_ENABLE}),
               "frag_drive_band.inc:22-23 (orig 587-588); hazard-c1-spec.md 2.3 row 31; "
               "hazard-c3-spec.md 4.2"},
    Transition{Phase::DRIVING, Input::PROFILE_CLICK, when({Guard::DRIVING_OK, Guard::POST_OK}),
               Phase::DRIVING, acts({Action::SEND_ENABLE}),
               "frag_drive_band.inc:22-23 (orig 587-588); hazard-c1-spec.md 2.3 row 32; "
               "hazard-c3-spec.md 4.2"},
    Transition{Phase::EXITING, Input::PROFILE_CLICK, kAlways, Phase::EXITING,
               acts({Action::PUBLISH_DRIVE}), "frag_drive_band.inc:22-23 (orig 587-588)"},
    Transition{Phase::EXIT_REFUSED, Input::PROFILE_CLICK, kAlways, Phase::EXIT_REFUSED,
               acts({Action::PUBLISH_DRIVE}), "frag_drive_band.inc:22-23 (orig 587-588)"},

    // ===== UNLOCK_TIMER: unlock_advance_cb (cancelled by any set_locked)
    Transition{Phase::UNLOCKING, Input::UNLOCK_TIMER, when({Guard::UNLOCK_TIMER_ARMED}),
               Phase::DRIVING, kAdvance, "frag_lock.inc:14-18 (orig 1079-1083)"},
    Transition{Phase::EXITING, Input::UNLOCK_TIMER, when({Guard::UNLOCK_TIMER_ARMED}),
               Phase::EXITING, kAdvance, "frag_lock.inc:14-18 (orig 1079-1083)"},
    Transition{Phase::EXIT_REFUSED, Input::UNLOCK_TIMER, when({Guard::UNLOCK_TIMER_ARMED}),
               Phase::EXIT_REFUSED, kAdvance, "frag_lock.inc:14-18 (orig 1079-1083)"},

    // ===== ENTRY_PUSH: entry_refusal_poll; lock_waiting is not checked
    Transition{Phase::LOCKED, Input::ENTRY_PUSH,
               when({Guard::ON_LOCKED_SCREEN}, {Guard::MENU_OPEN, Guard::MCB_READY}), Phase::LOCKED,
               acts({Action::SHOW_REFUSED_DRIVE, Action::REFUSAL_FEEDBACK}),
               "frag_refusal.inc:293-304 (orig 1484-1495)"},
    Transition{Phase::ASKING, Input::ENTRY_PUSH,
               when({Guard::ON_LOCKED_SCREEN}, {Guard::MENU_OPEN, Guard::MCB_READY}), Phase::ASKING,
               acts({Action::SHOW_REFUSED_DRIVE, Action::REFUSAL_FEEDBACK}),
               "frag_refusal.inc:293-304 (orig 1484-1495)"},

    // ===== MENU_ROW_DRIVE: refused on the spot while locked and not ready
    Transition{Phase::LOCKED, Input::MENU_ROW_DRIVE, when({}, {Guard::MCB_READY}), Phase::LOCKED,
               acts({Action::REFUSAL_FEEDBACK, Action::SHOW_REFUSED_DRIVE_MENU}),
               "frag_nav.inc:463-467 (orig 3691-3695)"},
    Transition{Phase::ASKING, Input::MENU_ROW_DRIVE, when({}, {Guard::MCB_READY}), Phase::ASKING,
               acts({Action::REFUSAL_FEEDBACK, Action::SHOW_REFUSED_DRIVE_MENU}),
               "frag_nav.inc:463-467 (orig 3691-3695)"},

    // ===== Hazard fix C1, appended (docs/plans/hazard-c1-spec.md §2.3)
    // EXIT_HOLD_DONE while locked (was TABLE.md U3, outside the table): one DISABLE, no latch,
    // no deadline, no banner. From ASKING the user's stop wins over their own ask.
    Transition{Phase::LOCKED, Input::EXIT_HOLD_DONE, kAlways, Phase::LOCKED,
               acts({Action::SEND_DISABLE, Action::CLEAR_GIVEUP}), "hazard-c1-spec.md 2.3 row 42"},
    Transition{
        Phase::ASKING, Input::EXIT_HOLD_DONE, kAlways, Phase::LOCKED,
        acts({Action::SEND_DISABLE, Action::CLEAR_WARN, Action::CLEAR_GIVEUP, Action::RING_REST}),
        "hazard-c1-spec.md 2.3 row 43"},
    // TICK_STOP_FAULT_DUE: "MCB did not stop", once, kStopFaultAfter after the first stop (G4).
    Transition{Phase::EXITING, Input::TICK_STOP_FAULT_DUE,
               when({Guard::STOP_FAULT_ELAPSED}, {Guard::STOP_FAULT}), Phase::EXITING,
               acts({Action::RAISE_STOP_FAULT}), "hazard-c1-spec.md 2.3 row 44"},
    Transition{Phase::EXIT_REFUSED, Input::TICK_STOP_FAULT_DUE,
               when({Guard::STOP_FAULT_ELAPSED}, {Guard::STOP_FAULT}), Phase::EXIT_REFUSED,
               acts({Action::RAISE_STOP_FAULT}), "hazard-c1-spec.md 2.3 row 45"},
    // TICK_STOP_RESEND: DISABLE every kStopResend, then every kStopResendSlow after the fault,
    // for as long as the exit phase lasts (G4).
    Transition{Phase::EXITING, Input::TICK_STOP_RESEND,
               when({Guard::RESEND_FAST_DUE}, {Guard::STOP_FAULT}), Phase::EXITING,
               acts({Action::SEND_DISABLE}), "hazard-c1-spec.md 2.3 row 46"},
    Transition{Phase::EXITING, Input::TICK_STOP_RESEND,
               when({Guard::RESEND_SLOW_DUE, Guard::STOP_FAULT}), Phase::EXITING,
               acts({Action::SEND_DISABLE}), "hazard-c1-spec.md 2.3 row 47"},
    Transition{Phase::EXIT_REFUSED, Input::TICK_STOP_RESEND,
               when({Guard::RESEND_FAST_DUE}, {Guard::STOP_FAULT}), Phase::EXIT_REFUSED,
               acts({Action::SEND_DISABLE}), "hazard-c1-spec.md 2.3 row 48"},
    Transition{Phase::EXIT_REFUSED, Input::TICK_STOP_RESEND,
               when({Guard::RESEND_SLOW_DUE, Guard::STOP_FAULT}), Phase::EXIT_REFUSED,
               acts({Action::SEND_DISABLE}), "hazard-c1-spec.md 2.3 row 49"},

    // ===== Hazard fix C3, appended (docs/plans/hazard-c3-spec.md §4.3)
    // TICK_BOOT_STOP: one DISABLE on the first tick with the link CONNECTED after boot; when the
    // user's own ask already went (ASKING), it wins: the bit is only marked.
    Transition{Phase::LOCKED, Input::TICK_BOOT_STOP,
               when({Guard::LINK_CONNECTED}, {Guard::BOOT_STOP_DONE}), Phase::LOCKED,
               acts({Action::SEND_DISABLE, Action::MARK_BOOT_STOP}),
               "hazard-c3-spec.md 4.3 row 50"},
    Transition{Phase::ASKING, Input::TICK_BOOT_STOP,
               when({Guard::LINK_CONNECTED}, {Guard::BOOT_STOP_DONE}), Phase::ASKING,
               acts({Action::MARK_BOOT_STOP}), "hazard-c3-spec.md 4.3 row 51"},
    // Refusals before POST pass, the MCB ready (their !RDY twins are rows 19, 38, 40).
    Transition{Phase::LOCKED, Input::UNLOCK_HOLD_DONE, when({Guard::MCB_READY}, {Guard::POST_OK}),
               Phase::LOCKED,
               acts({Action::RING_REST, Action::SHOW_REFUSED_POST, Action::REFUSAL_FEEDBACK}),
               "hazard-c3-spec.md 4.3 row 52"},
    Transition{
        Phase::LOCKED, Input::ENTRY_PUSH,
        when({Guard::ON_LOCKED_SCREEN, Guard::MCB_READY}, {Guard::MENU_OPEN, Guard::POST_OK}),
        Phase::LOCKED, acts({Action::SHOW_REFUSED_POST, Action::REFUSAL_FEEDBACK}),
        "hazard-c3-spec.md 4.3 row 53"},
    Transition{Phase::LOCKED, Input::MENU_ROW_DRIVE, when({Guard::MCB_READY}, {Guard::POST_OK}),
               Phase::LOCKED, acts({Action::REFUSAL_FEEDBACK, Action::SHOW_REFUSED_POST}),
               "hazard-c3-spec.md 4.3 row 54"},
};
inline constexpr std::size_t kTransitionCount = 54;

// What the Drive screen says about the user's stop (hazard-c1-spec.md §2.6): MCB_DID_NOT_STOP
// in an exit phase with the stop fault, STOPPING in an exit phase without it, NONE otherwise.
// It clears at the relock; the fault stays in the log.
constexpr StopNotice stop_notice(Phase p, GuardMask hidden) noexcept {
  if (p != Phase::EXITING && p != Phase::EXIT_REFUSED) {
    return StopNotice::NONE;
  }
  return (hidden & bit(Guard::STOP_FAULT)) != 0 ? StopNotice::MCB_DID_NOT_STOP
                                                : StopNotice::STOPPING;
}

// Lookup for the oracle: index of the one matching row, or TRANSITIONS.size().
constexpr std::size_t find_row(Phase p, Input in, GuardMask guards) noexcept {
  for (std::size_t i = 0; i < TRANSITIONS.size(); ++i) {
    const Transition &t = TRANSITIONS[i];
    if (t.from == p && t.input == in && matches(t.guard, guards)) {
      return i;
    }
  }
  return TRANSITIONS.size();
}

// ---------------------------------------------------------------------------
// CS-SAF-02 "every state has a way out on timeout or on error"
// ---------------------------------------------------------------------------
// LOCKED is the safe state (stick gate shut, see stick_drives) and is exempt.
// Every other phase needs a row to a different phase whose guard requires an
// error (a required-false DRIVING_OK, MCB_READY or LINK_CONNECTED) or a timeout
// (a required-true *_ELAPSED, or WARN_ARMED required false after the warn has
// fired, or the UNLOCK_TIMER input).
inline constexpr Phase kSafePhase = Phase::LOCKED;

struct KnownGap {
  Phase phase;
  std::string_view code;
  std::string_view what;
};
// No phase lacks a way out in the code as it stands; kept (empty) so a future
// re-derivation records a gap here instead of inventing a row.
inline constexpr std::array<KnownGap, 0> KNOWN_GAPS{};

constexpr bool is_error_or_timeout_exit(const Transition &t) noexcept {
  const GuardMask err = mask({Guard::DRIVING_OK, Guard::MCB_READY, Guard::LINK_CONNECTED});
  const GuardMask time = mask({Guard::EXIT_ELAPSED, Guard::WARN_ELAPSED, Guard::GIVEUP_ELAPSED});
  return t.to != t.from &&
         ((t.guard.need_false & err) != 0 || (t.guard.need_true & time) != 0 ||
          (t.guard.need_false & bit(Guard::WARN_ARMED)) != 0 || t.input == Input::UNLOCK_TIMER);
}

constexpr bool has_way_out(Phase p) noexcept {
  return std::ranges::any_of(
      TRANSITIONS, [p](const Transition &t) { return t.from == p && is_error_or_timeout_exit(t); });
}

// While KNOWN_GAPS is empty no phase is a gap; the lookup is compiled only once a gap is
// recorded (then the array's size is no longer 0).
constexpr bool is_known_gap(Phase p) noexcept {
  if constexpr (KNOWN_GAPS.empty()) {
    static_cast<void>(p);
    return false;
  } else {
    return std::ranges::any_of(KNOWN_GAPS, [p](const KnownGap &g) { return g.phase == p; });
  }
}

// ---------------------------------------------------------------------------
// Static invariants (CS-TYP-03, TS-UNIT-04)
// ---------------------------------------------------------------------------
namespace detail {

constexpr bool rows_exclusive() noexcept {
  for (std::size_t i = 0; i < TRANSITIONS.size(); ++i) {
    for (std::size_t j = i + 1; j < TRANSITIONS.size(); ++j) {
      const Transition &a = TRANSITIONS[i];
      const Transition &b = TRANSITIONS[j];
      if (a.from == b.from && a.input == b.input && !exclusive(a.guard, b.guard)) {
        return false;
      }
    }
  }
  return true;
}

constexpr bool guards_not_contradictory() noexcept {
  return std::ranges::none_of(TRANSITIONS, [](const Transition &t) {
    return (t.guard.need_true & t.guard.need_false) != 0;
  });
}

// One row: NONE only as padding (nothing but NONE after the first NONE), no action twice.
constexpr bool row_actions_packed(const Transition &t) noexcept {
  const auto first_none = std::ranges::find(t.actions, Action::NONE);
  if (!std::all_of(first_none, t.actions.end(), [](Action a) { return a == Action::NONE; })) {
    return false;
  }
  for (std::size_t i = 0; i < kMaxActions; ++i) {
    for (std::size_t j = i + 1; j < kMaxActions; ++j) {
      if (t.actions[i] != Action::NONE && t.actions[i] == t.actions[j]) {
        return false;
      }
    }
  }
  return true;
}

constexpr bool actions_packed() noexcept {
  return std::ranges::all_of(TRANSITIONS, row_actions_packed);
}

constexpr bool row_within_precondition(const Transition &t) noexcept {
  const InputPrecondition &pre = INPUT_PRECONDITIONS[static_cast<std::size_t>(t.input)];
  if (pre.input != t.input || !in(pre.phases, t.from)) {
    return false;
  }
  // A row's guard must not contradict the input's own precondition.
  return ((t.guard.need_true & pre.guard.need_false) |
          (t.guard.need_false & pre.guard.need_true)) == 0;
}

constexpr bool rows_within_preconditions() noexcept {
  return std::ranges::all_of(TRANSITIONS, row_within_precondition);
}

// One hidden bit `v`: run the actions from each value of it the source phase and the guard
// allow (both, when neither fixes it); the bit must land where `dst` allows.
constexpr bool bit_lands_valid(const Actions &actions, GuardMask v, GuardMask fixed_true,
                               GuardMask fixed_false, const PhaseInvariant &dst) noexcept {
  const std::array<GuardMask, 2> starts{GuardMask{0}, v};
  return std::ranges::all_of(starts, [&](GuardMask start) {
    if (((v & fixed_true) != 0 && start == 0) || ((v & fixed_false) != 0 && start != 0)) {
      return true; // not a value this row can start from
    }
    const GuardMask after = apply(actions, start) & v;
    return !((dst.must_true & v) != 0 && after == 0) && !((dst.must_false & v) != 0 && after != 0);
  });
}

// From any hidden mask valid in `from` that the guard admits, the actions must
// land in a mask valid in `to`.
constexpr bool row_keeps_invariants(const Transition &t) noexcept {
  const PhaseInvariant &src = invariant_of(t.from);
  const PhaseInvariant &dst = invariant_of(t.to);
  const GuardMask fixed_true = src.must_true | (t.guard.need_true & kHiddenGuards);
  const GuardMask fixed_false = src.must_false | (t.guard.need_false & kHiddenGuards);
  if ((fixed_true & fixed_false) != 0) {
    return false; // the row can never match a valid state
  }
  for (unsigned b = 0; b < kGuardCount; ++b) {
    const GuardMask v = GuardMask{1} << b;
    if ((v & kHiddenGuards) == 0) {
      continue;
    }
    if (!bit_lands_valid(t.actions, v, fixed_true, fixed_false, dst)) {
      return false;
    }
  }
  return true;
}

constexpr bool all_rows_keep_invariants() noexcept {
  return std::ranges::all_of(TRANSITIONS, row_keeps_invariants);
}

constexpr bool every_action_used() noexcept {
  for (std::size_t a = 1; a < kActionCount; ++a) {
    bool used = false;
    for (const Transition &t : TRANSITIONS) {
      for (Action x : t.actions) {
        used = used || static_cast<std::size_t>(x) == a;
      }
    }
    if (!used) {
      return false;
    }
  }
  return true;
}

constexpr bool every_input_used() noexcept {
  for (std::size_t i = 0; i < kInputCount; ++i) {
    bool used = false;
    for (const Transition &t : TRANSITIONS) {
      used = used || static_cast<std::size_t>(t.input) == i;
    }
    if (!used) {
      return false;
    }
  }
  return true;
}

constexpr bool way_out_everywhere_but_gaps() noexcept {
  for (std::size_t p = 0; p < kPhaseCount; ++p) {
    const auto phase = static_cast<Phase>(p);
    if (phase == kSafePhase || is_known_gap(phase)) {
      continue;
    }
    if (!has_way_out(phase)) {
      return false;
    }
  }
  return true;
}

constexpr bool tables_indexed() noexcept {
  for (std::size_t p = 0; p < PHASE_INVARIANTS.size(); ++p) {
    if (static_cast<std::size_t>(PHASE_INVARIANTS[p].phase) != p) {
      return false;
    }
  }
  for (std::size_t i = 0; i < INPUT_PRECONDITIONS.size(); ++i) {
    if (static_cast<std::size_t>(INPUT_PRECONDITIONS[i].input) != i) {
      return false;
    }
  }
  return PHASE_INVARIANTS.size() == kPhaseCount && INPUT_PRECONDITIONS.size() == kInputCount;
}

constexpr bool has(const Actions &a, Action x) noexcept {
  return std::ranges::find(a, x) != a.end();
}

// The phase-changing actions agree with `to`, and lockedness changes only
// through SET_LOCKED / SET_UNLOCKED.
constexpr bool row_actions_agree_with_phase(const Transition &t) noexcept {
  const Actions &a = t.actions;
  if (has(a, Action::SET_LOCKED) && t.to != Phase::LOCKED) {
    return false;
  }
  if (has(a, Action::SET_UNLOCKED) && t.to != Phase::UNLOCKING) {
    return false;
  }
  if (!has(a, Action::SET_LOCKED) && !has(a, Action::SET_UNLOCKED) &&
      is_locked_phase(t.from) != is_locked_phase(t.to)) {
    return false;
  }
  if (has(a, Action::RING_WAIT) && t.to != Phase::ASKING) {
    return false;
  }
  if (has(a, Action::RING_REST) && t.to != Phase::LOCKED) {
    return false;
  }
  if (has(a, Action::ARM_EXIT_DEADLINE) && t.to != Phase::EXITING) {
    return false;
  }
  if (has(a, Action::SET_EXIT_REQUESTED) != has(a, Action::ARM_EXIT_DEADLINE)) {
    return false;
  }
  if (has(a, Action::START_UNLOCK_TIMER) != has(a, Action::SET_UNLOCKED)) {
    return false;
  }
  // Leaving an exit phase for an unlocked one other than via the deadline.
  if ((t.from == Phase::EXITING) && t.to == Phase::EXIT_REFUSED &&
      !has(a, Action::CLEAR_EXIT_DEADLINE)) {
    return false;
  }
  // A phase change with no action that explains it.
  if (t.from != t.to && !has(a, Action::SET_LOCKED) && !has(a, Action::SET_UNLOCKED) &&
      !has(a, Action::RING_WAIT) && !has(a, Action::RING_REST) &&
      !has(a, Action::ARM_EXIT_DEADLINE) && !has(a, Action::CLEAR_EXIT_DEADLINE) &&
      !has(a, Action::UNLOCK_TIMER_DONE)) {
    return false;
  }
  return true;
}

constexpr bool actions_agree_with_phase() noexcept {
  return std::ranges::all_of(TRANSITIONS, row_actions_agree_with_phase);
}

constexpr bool effects_hidden_only() noexcept {
  return std::ranges::none_of(ACTION_EFFECTS, [](const ActionEffect &e) {
    return ((e.sets | e.clears) & ~kHiddenGuards) != 0 || (e.sets & e.clears) != 0;
  });
}

// ---- The hazard fix C1's own rules (hazard-c1-spec.md §2.5, "Static checks") --------------

constexpr bool is_exit_phase(Phase p) noexcept {
  return p == Phase::EXITING || p == Phase::EXIT_REFUSED;
}

// The index (row number - 1) of a row.
constexpr std::size_t index_of_row(const Transition &t) noexcept {
  return static_cast<std::size_t>(&t - TRANSITIONS.data());
}

// ARM_STOP_TIMER only on rows from UNLOCKING or DRIVING to EXITING (the first stop of an exit).
constexpr bool stop_timer_only_on_first_stop() noexcept {
  return std::ranges::all_of(TRANSITIONS, [](const Transition &t) {
    return !has(t.actions, Action::ARM_STOP_TIMER) ||
           ((t.from == Phase::UNLOCKING || t.from == Phase::DRIVING) && t.to == Phase::EXITING);
  });
}

// Every row from an unlocked phase to LOCKED sends DISABLE, after GATE_UPDATE (the gate shuts
// first, H5).
constexpr bool every_relock_sends_disable() noexcept {
  return std::ranges::all_of(TRANSITIONS, [](const Transition &t) {
    if (is_locked_phase(t.from) || t.to != Phase::LOCKED) {
      return true;
    }
    const auto gate = std::ranges::find(t.actions, Action::GATE_UPDATE);
    const auto disable = std::ranges::find(t.actions, Action::SEND_DISABLE);
    return disable != t.actions.end() && gate < disable;
  });
}

// SEND_ENABLE only in rows 18, 31 and 32; rows 31-32 need DRIVING_OK.
constexpr bool enable_only_where_allowed() noexcept {
  return std::ranges::all_of(TRANSITIONS, [](const Transition &t) {
    if (!has(t.actions, Action::SEND_ENABLE)) {
      return true;
    }
    const std::size_t row = index_of_row(t) + 1;
    if (row == 18) {
      return true;
    }
    return (row == 31 || row == 32) && (t.guard.need_true & bit(Guard::DRIVING_OK)) != 0;
  });
}

// Every row from EXITING or EXIT_REFUSED to LOCKED clears the stop fault (the stop ends).
constexpr bool every_stop_end_clears_fault() noexcept {
  return std::ranges::all_of(TRANSITIONS, [](const Transition &t) {
    return !is_exit_phase(t.from) || t.to != Phase::LOCKED ||
           has(t.actions, Action::CLEAR_STOP_FAULT);
  });
}

// ---- The hazard fix C3's own rules (hazard-c3-spec.md §4.4, "Static checks") --------------

// Every row that sends ENABLE needs POST_OK (H2).
constexpr bool every_enable_needs_post() noexcept {
  return std::ranges::all_of(TRANSITIONS, [](const Transition &t) {
    return !has(t.actions, Action::SEND_ENABLE) || (t.guard.need_true & bit(Guard::POST_OK)) != 0;
  });
}

// Rows 1-2 read +BOOT_STOP_DONE and do not read POST_OK (M1 enters before POST pass, G3).
constexpr bool entry_waits_for_boot_stop() noexcept {
  for (const std::size_t i : {std::size_t{0}, std::size_t{1}}) {
    const Transition &t = TRANSITIONS[i];
    if ((t.guard.need_true & bit(Guard::BOOT_STOP_DONE)) == 0 ||
        ((t.guard.need_true | t.guard.need_false) & bit(Guard::POST_OK)) != 0) {
      return false;
    }
  }
  return true;
}

// MARK_BOOT_STOP only in rows 50-51; SHOW_REFUSED_POST only with !POST_OK.
constexpr bool boot_stop_and_post_refusal_where_allowed() noexcept {
  return std::ranges::all_of(TRANSITIONS, [](const Transition &t) {
    const std::size_t row = index_of_row(t) + 1;
    const bool mark_ok = !has(t.actions, Action::MARK_BOOT_STOP) || row == 50 || row == 51;
    const bool refusal_ok = !has(t.actions, Action::SHOW_REFUSED_POST) ||
                            (t.guard.need_false & bit(Guard::POST_OK)) != 0;
    return mark_ok && refusal_ok;
  });
}

} // namespace detail

static_assert(static_cast<std::size_t>(Phase::EXIT_REFUSED) + 1 == kPhaseCount);
static_assert(static_cast<std::size_t>(Input::TICK_BOOT_STOP) + 1 == kInputCount);
static_assert(static_cast<std::size_t>(Guard::BOOT_STOP_DONE) + 1 == kGuardCount);
static_assert(static_cast<std::size_t>(Action::SHOW_REFUSED_POST) + 1 == kActionCount);
static_assert(kGuardCount <= 32, "GuardMask is 32 bits");
static_assert(TRANSITIONS.size() == kTransitionCount, "update kTransitionCount and TABLE.md");
static_assert(detail::tables_indexed(), "PHASE_INVARIANTS / INPUT_PRECONDITIONS indexed by enum");
static_assert(detail::rows_exclusive(), "two rows with the same (from, input) can both match");
static_assert(detail::guards_not_contradictory(), "a guard requires a bit both true and false");
static_assert(detail::actions_packed(), "NONE before an action, or an action twice in a row");
static_assert(detail::rows_within_preconditions(), "a row for an input that cannot arrive");
static_assert(detail::effects_hidden_only(), "an action effect touches an env guard");
static_assert(detail::all_rows_keep_invariants(), "a row breaks a phase invariant");
static_assert(detail::actions_agree_with_phase(), "an action contradicts the row's phase change");
static_assert(detail::every_action_used(), "an Action no row uses");
static_assert(detail::every_input_used(), "an Input no row uses");
static_assert(detail::way_out_everywhere_but_gaps(), "CS-SAF-02: a phase with no way out");
static_assert(has_way_out(Phase::EXIT_REFUSED), "EXIT_REFUSED leaves only on error (no timeout)");
static_assert(detail::stop_timer_only_on_first_stop(),
              "C1: ARM_STOP_TIMER only on UNLOCKING/DRIVING -> EXITING");
static_assert(detail::every_relock_sends_disable(),
              "C1: a row from an unlocked phase to LOCKED without SEND_DISABLE after GATE_UPDATE");
static_assert(detail::enable_only_where_allowed(),
              "C1: SEND_ENABLE outside rows 18, 31, 32, or rows 31-32 without DRIVING_OK");
static_assert(detail::every_stop_end_clears_fault(),
              "C1: a row from an exit phase to LOCKED that keeps the stop fault");
static_assert(detail::every_enable_needs_post(), "C3: a row sends ENABLE without POST_OK");
static_assert(detail::entry_waits_for_boot_stop(),
              "C3: rows 1-2 must read +BOOT_STOP_DONE and must not read POST_OK");
static_assert(detail::boot_stop_and_post_refusal_where_allowed(),
              "C3: MARK_BOOT_STOP outside rows 50-51, or SHOW_REFUSED_POST without !POST_OK");
static_assert(stop_notice(Phase::EXITING, 0) == StopNotice::STOPPING);
static_assert(stop_notice(Phase::EXIT_REFUSED, bit(Guard::STOP_FAULT)) ==
              StopNotice::MCB_DID_NOT_STOP);
static_assert(stop_notice(Phase::DRIVING, bit(Guard::STOP_FAULT)) == StopNotice::NONE);

// ---------------------------------------------------------------------------
// The stick gate (nav_update_stick_gate, frag_nav.inc:190-193, orig 3418-3421)
// ---------------------------------------------------------------------------
// Written to std::atomic<bool> stick_drives (frag_state.inc:143, orig 249,
// initially false) only at the triggers below, on the LVGL task. Read by the
// ADC task: scale = calibrating || !stick_drives ? 0 : speed (main.cpp:1605,
// orig 6227). Between triggers the stored value can lag the screen and menu
// (e.g. during a FADE screen change it changes only at SCREEN_LOADED).
constexpr bool stick_drives(bool locked, Screen screen, bool menu_open) noexcept {
  return !locked && screen == Screen::DRIVE && !menu_open;
}
static_assert(stick_drives(false, Screen::DRIVE, false));
static_assert(!stick_drives(true, Screen::DRIVE, false));
static_assert(!stick_drives(false, Screen::DRIVE, true));
static_assert(!stick_drives(false, Screen::LOCKED, false));

// The stick multiplier in the ADC task, as written (a multiply: NaN passes, R-S F12).
constexpr float stick_scale(bool calibrating, bool gate, float speed) noexcept {
  return calibrating || !gate ? 0.0f : speed;
}

struct GateTrigger {
  std::string_view function;
  std::string_view code;
  std::string_view reached_from;
};
inline constexpr std::array GATE_TRIGGERS{
    GateTrigger{"set_locked", "frag_lock.inc:78 (orig 1143)",
                "every lock and unlock: TICK_FOLLOW F1 (lock_open) and F2 rows"},
    GateTrigger{"nav_close_menu", "frag_nav.inc:267 (orig 3495)",
                "menu key on its open menu (frag_nav.inc:483-485), LEFT/ESC at the top level "
                "(frag_nav.inc:525), nav_home (frag_nav.inc:324: DRIVE band, F3 row)"},
    GateTrigger{"nav_open_menu", "frag_nav.inc:317 (orig 3545)",
                "menu key anywhere but Drive-while-unlocked (frag_nav.inc:497); nav_arrive with "
                "nav_menu_on_arrival (frag_nav.inc:762, OPEN_MENU_ON_ARRIVAL)"},
    GateTrigger{"nav_go", "frag_nav.inc:428 (orig 3656)",
                "a menu row, kRowPressMs after the pick (nav_press_done_cb, frag_nav.inc:433)"},
    GateTrigger{
        "nav_arrive", "frag_nav.inc:765 (orig 3993)",
        "every LV_EVENT_SCREEN_LOADED (screen_loaded_cb, main.cpp:992 orig 5614; "
        "frag_screens_on_demand.inc:60,89,127), and by hand from nav_go (frag_nav.inc:426)"},
};
inline constexpr std::string_view kGateReader = "ADC task lambda, main.cpp:1605 (orig 6227): scale "
                                                "= calibrating || !stick_drives.load() ? 0 : speed";
// NOT triggers (each is a hazard or a lag): the self-test overlay (H7: the
// gate stays open behind it), unlock_advance_cb itself (the gate opens at the
// Drive screen's SCREEN_LOADED, after the kUnlockDissolve fade), the
// MibStatus handler and the link poll (they only move the gate via F2).

} // namespace hmi::drive_session
