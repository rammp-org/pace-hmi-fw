// SPDX-License-Identifier: MIT
//
// drive_session_types.hpp: the vocabulary of the drive session table.
//
// The enums (Phase, Input, Guard, Action), the guard and action helpers, the Env and
// env_guards(), and the row structs that drive_session_table.hpp fills in. Moved here
// unchanged from drive_session_table.hpp (owner-approved, 2026-10-06) so that each file stays
// within CS-FIL-01; the table's data, the spec, stays in drive_session_table.hpp, and
// drive_session_fingerprint.hpp proves the move changed no row. MODEL, line references and
// the oracle contract: the comment at the top of drive_session_table.hpp.
//
// This is part of the declaration: it is never edited to make a check pass (CORE
// never-list). A change to an enum or a struct is a table change (CS-SAF-05).
//
// Pure C++23: no LVGL, no ESP-IDF, no FreeRTOS.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <string_view>

namespace hmi::drive_session {

// ---------------------------------------------------------------------------
// Phase
// ---------------------------------------------------------------------------
// The six phases of the plan's draft are kept: the code supports exactly these
// as projections of (locked_subject, lock_waiting, unlock_advance_timer,
// drive_exit_until_us, drive_exit_requested); see phase_of.
enum class Phase : std::uint8_t {
  LOCKED,       // locked, not waiting (giveup may still be armed: HIDDEN)
  ASKING,       // locked, lock_waiting: ENABLE sent, ring going round
  UNLOCKING,    // unlocked, unlock_advance_timer armed, no exit asked
  DRIVING,      // unlocked, timer gone, no exit asked
  EXITING,      // unlocked, exit asked, exit deadline armed
  EXIT_REFUSED, // unlocked, exit asked (latched), exit deadline expired
};
inline constexpr std::size_t kPhaseCount = 6;

// The code variables a phase is made of. The implementation's state must map
// onto these one to one.
struct CodeVars {
  bool locked;              // lv_subject_get_int(&locked_subject) != 0
  bool lock_waiting;        // lock_waiting, frag_lock.inc:8 (orig 1073)
  bool unlock_timer_armed;  // unlock_advance_timer != nullptr, frag_lock.inc:5 (orig 1070)
  bool exit_deadline_armed; // drive_exit_until_us != 0, frag_drive.inc:7 (orig 1530)
  bool exit_requested;      // drive_exit_requested, frag_drive.inc:11 (orig 1534)
};

// Locked phases ignore the exit variables: the code never sets them while
// locked (except in the excluded case U3 of TABLE.md).
constexpr Phase phase_of(const CodeVars &v) noexcept {
  if (v.locked) {
    return v.lock_waiting ? Phase::ASKING : Phase::LOCKED;
  }
  if (v.exit_requested) {
    return v.exit_deadline_armed ? Phase::EXITING : Phase::EXIT_REFUSED;
  }
  return v.unlock_timer_armed ? Phase::UNLOCKING : Phase::DRIVING;
}

constexpr bool is_locked_phase(Phase p) noexcept {
  return p == Phase::LOCKED || p == Phase::ASKING;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
enum class Input : std::uint8_t {
  // drive_wait_poll(), from rtps_poll_cb every kTickPeriod; see TICK_SEQUENCE.
  TICK_FOLLOW,     // drive_screen_follow_state()      frag_drive.inc:44-97 (orig 1567-1620)
  TICK_EXIT_DUE,   // exit deadline check              frag_drive.inc:102-109 (orig 1625-1632)
  TICK_WARN_DUE,   // answer-window (warn) check       frag_drive.inc:110-115 (orig 1633-1638)
  TICK_GIVEUP_DUE, // give-up check                    frag_drive.inc:116-121 (orig 1639-1644)
  // Hold completions (see HOLD_TRANSITIONS): the fill finished.
  UNLOCK_HOLD_DONE, // unlock_gesture.completed -> activate_drive()   frag_lock.inc:99 (orig 1164)
  EXIT_HOLD_DONE, // drive_exit_gesture.completed -> drive_exit_ask(false) frag_drive.inc:198 (orig
                  // 1721)
  // nav_key_cb with its menu shut, on the Drive screen, unlocked. Other menu
  // key presses are navigation only (they move the gate, GATE_TRIGGERS).
  MENU_KEY_DRIVE, // frag_nav.inc:491-496 (orig 3719-3724)
  // A drive-profile button clicked (touch only, Drive screen).
  PROFILE_CLICK, // drive_profile_click_cb frag_drive_band.inc:17-24 (orig 582-589)
  // unlock_advance_cb: the one-shot kUnlockAdvance timer fired.
  UNLOCK_TIMER, // frag_lock.inc:14-18 (orig 1079-1083)
  // entry_refusal_poll's "pushed": a hold_poll_cb pass (overlay not up) where
  // the stick button has been held >= kBarGrace and no refusal was raised yet
  // during this press. Level-like: repeats every poll until a refusal latches
  // refused_this_press or the button is let go.
  ENTRY_PUSH, // frag_refusal.inc:281-304 (orig 1472-1495)
  // nav_row_cb for the DRIVE row (no row press pending).
  MENU_ROW_DRIVE, // frag_nav.inc:463-467 (orig 3691-3695)
};
inline constexpr std::size_t kInputCount = 11;

// ---------------------------------------------------------------------------
// Guard
// ---------------------------------------------------------------------------
enum class Guard : std::uint8_t {
  // --- env: sampled subjects and clock -------------------------------------
  LINK_CONNECTED, // rtps_link_subject == CONNECTED
  DRIVING_OK,     // CONNECTED && mib_state == ENABLED   (frag_drive.inc:48, orig 1571)
  MCB_READY, // CONNECTED && mib_state in {IDLE, ENABLED} (mcb_ready, frag_refusal.inc:6-11, orig
             // 1197-1202)
  ON_LOCKED_SCREEN, // lv_screen_active() == ui_LockedScreen
  ON_DRIVE_SCREEN,  // lv_screen_active() == ui_DriveScreen
  ON_SEAT_SCREEN,   // lv_screen_active() == ui_SeatScreen
  MENU_OPEN,        // nav_menu_open != nullptr
  EXIT_ELAPSED,     // now >= drive_exit_until_us
  WARN_ELAPSED,     // now >= drive_wait_warn_us
  GIVEUP_ELAPSED,   // now >= drive_wait_until_us
  // --- hidden: session variables that are not in Phase ---------------------
  WARN_ARMED,         // drive_wait_warn_us != 0   frag_drive.inc:4 (orig 1527)
  GIVEUP_ARMED,       // drive_wait_until_us != 0  frag_drive.inc:3 (orig 1526)
  THEN_MENU,          // drive_exit_then_menu      frag_drive.inc:14 (orig 1537)
  REQUEST_ENABLE,     // drive_request == ENABLE   frag_drive.inc:2 (orig 1525)
  UNLOCK_TIMER_ARMED, // unlock_advance_timer != nullptr frag_lock.inc:5 (orig 1070)
};
inline constexpr std::size_t kGuardCount = 15;

using GuardMask = std::uint32_t;
constexpr GuardMask bit(Guard g) noexcept { return GuardMask{1} << static_cast<unsigned>(g); }
constexpr GuardMask mask(std::initializer_list<Guard> gs) noexcept {
  return std::accumulate(gs.begin(), gs.end(), GuardMask{0},
                         [](GuardMask m, Guard g) { return m | bit(g); });
}

inline constexpr GuardMask kEnvGuards =
    mask({Guard::LINK_CONNECTED, Guard::DRIVING_OK, Guard::MCB_READY, Guard::ON_LOCKED_SCREEN,
          Guard::ON_DRIVE_SCREEN, Guard::ON_SEAT_SCREEN, Guard::MENU_OPEN, Guard::EXIT_ELAPSED,
          Guard::WARN_ELAPSED, Guard::GIVEUP_ELAPSED});
inline constexpr GuardMask kHiddenGuards =
    mask({Guard::WARN_ARMED, Guard::GIVEUP_ARMED, Guard::THEN_MENU, Guard::REQUEST_ENABLE,
          Guard::UNLOCK_TIMER_ARMED});
static_assert((kEnvGuards & kHiddenGuards) == 0);
static_assert((kEnvGuards | kHiddenGuards) == (GuardMask{1} << kGuardCount) - 1);

struct GuardExpr {
  GuardMask need_true;
  GuardMask need_false;
};
constexpr GuardExpr when(std::initializer_list<Guard> t,
                         std::initializer_list<Guard> f = {}) noexcept {
  return GuardExpr{mask(t), mask(f)};
}
inline constexpr GuardExpr kAlways{0, 0};

constexpr bool matches(const GuardExpr &g, GuardMask m) noexcept {
  return (m & g.need_true) == g.need_true && (m & g.need_false) == 0;
}
// Syntactically exclusive: some guard is required true by one and false by the other.
constexpr bool exclusive(const GuardExpr &a, const GuardExpr &b) noexcept {
  return ((a.need_true & b.need_false) | (a.need_false & b.need_true)) != 0;
}

// The environment the oracle enumerates. env_guards derives the env bits, so
// impossible combinations (DRIVING_OK without CONNECTED) never arise.
enum class MibState : std::uint8_t { INITIALIZING, IDLE, ENABLED, OTHER };
enum class Screen : std::uint8_t { BOOT, LOCKED, DRIVE, SEAT, OTHER };
struct Env {
  bool link_connected;
  MibState mib;
  Screen screen;
  bool menu_open;
  bool exit_elapsed;
  bool warn_elapsed;
  bool giveup_elapsed;
};
constexpr GuardMask env_guards(const Env &e) noexcept {
  GuardMask m = 0;
  if (e.link_connected) {
    m |= bit(Guard::LINK_CONNECTED);
    if (e.mib == MibState::ENABLED) {
      m |= bit(Guard::DRIVING_OK);
    }
    if (e.mib == MibState::ENABLED || e.mib == MibState::IDLE) {
      m |= bit(Guard::MCB_READY);
    }
  }
  if (e.screen == Screen::LOCKED) {
    m |= bit(Guard::ON_LOCKED_SCREEN);
  }
  if (e.screen == Screen::DRIVE) {
    m |= bit(Guard::ON_DRIVE_SCREEN);
  }
  if (e.screen == Screen::SEAT) {
    m |= bit(Guard::ON_SEAT_SCREEN);
  }
  if (e.menu_open) {
    m |= bit(Guard::MENU_OPEN);
  }
  if (e.exit_elapsed) {
    m |= bit(Guard::EXIT_ELAPSED);
  }
  if (e.warn_elapsed) {
    m |= bit(Guard::WARN_ELAPSED);
  }
  if (e.giveup_elapsed) {
    m |= bit(Guard::GIVEUP_ELAPSED);
  }
  return m;
}

// ---------------------------------------------------------------------------
// Action
// ---------------------------------------------------------------------------
// Performed in row order. NONE pads the fixed-size list and never appears
// before a real action.
enum class Action : std::uint8_t {
  NONE,
  // DriveCommand (rtps_comms_publish_drive, one-shot BEST_EFFORT, result ignored: H6)
  SEND_ENABLE,   // drive_request := ENABLE; publish (drive_ask)
  SEND_DISABLE,  // drive_request := DISABLE; publish (drive_ask)
  PUBLISH_DRIVE, // drive_profile_published := clicked; publish(drive_request as it is, profile)
                 // (H5)
  // deadlines and latches
  ARM_WARN,             // drive_wait_warn_us := now + kDriveAnswer
  ARM_GIVEUP,           // drive_wait_until_us := now + kDriveWait
  CLEAR_WARN,           // drive_wait_warn_us := 0
  CLEAR_GIVEUP,         // drive_wait_until_us := 0
  ARM_EXIT_DEADLINE,    // drive_exit_until_us := now + kDriveAnswer
  CLEAR_EXIT_DEADLINE,  // drive_exit_until_us := 0
  SET_EXIT_REQUESTED,   // drive_exit_requested := true
  CLEAR_EXIT_REQUESTED, // drive_exit_requested := false
  SET_THEN_MENU,        // drive_exit_then_menu := true
  CLEAR_THEN_MENU,      // drive_exit_then_menu := false
  // padlock visual
  RING_WAIT,        // lock_visual_wait: lock_waiting := true, quarter ring spins
  RING_REST,        // lock_visual_rest: lock_waiting := false, ring empty, shackle down
  LOCK_OPEN_VISUAL, // lock_open's visual half: lock_waiting := false, ring full, shackle up,
                    // STRONG_CLICK haptic
  // lock state (set_locked, frag_lock.inc:64-79, orig 1129-1144)
  CANCEL_UNLOCK_TIMER,   // delete unlock_advance_timer if armed
  START_UNLOCK_TIMER,    // one-shot kUnlockAdvance -> UNLOCK_TIMER
  SET_UNLOCKED,          // locked_subject := 0
  SET_LOCKED,            // locked_subject := 1
  GO_LOCKED_SCREEN,      // instant load of LockedScreen (nav_arrive runs inside it)
  GATE_UPDATE,           // nav_update_stick_gate()
  OPEN_MENU_ON_ARRIVAL,  // nav_menu_on_arrival := true before the load: the menu opens over Locked
  CLEAR_MENU_ON_ARRIVAL, // nav_menu_on_arrival := false before the load: no menu over Locked
  // unlock advance (unlock_advance_cb)
  UNLOCK_TIMER_DONE, // unlock_advance_timer := nullptr (LVGL deletes the one-shot)
  GO_DRIVE_SCREEN,   // fade to DriveScreen over kUnlockDissolve; gate re-evaluated at SCREEN_LOADED
                     // (nav_arrive)
  // navigation
  NAV_HOME, // nav_home(): close menu, fade to Locked (locked) or Drive (unlocked)
  // banners (entry_refused_show(kRefused*, dwell))
  SHOW_REFUSED_DRIVE,      // kRefusedDrive (1), 3000 ms
  SHOW_REFUSED_SEAT,       // kRefusedSeat (2), 3000 ms
  SHOW_NOT_GRANTED,        // kRefusedDriveNotGranted (3), 3000 ms
  SHOW_DRIVE_STOPPED,      // kRefusedDriveStopped (4), 3000 ms
  SHOW_EXIT_REFUSED,       // kRefusedExit (5), 2000 ms
  SHOW_DRIVE_LOST,         // kRefusedDriveLost (6), 3000 ms
  SHOW_REFUSED_DRIVE_MENU, // kRefusedDriveMenu (7), 3000 ms
  REFUSAL_FEEDBACK,        // refusal_feedback(): refusal haptic and sound
};
inline constexpr std::size_t kActionCount = 36;
// 11: the relock rows use 10, and DriveSession::SAFE_STATE_ACTIONS 11 (it also clears the
// menu-on-arrival flag before it loads the Locked screen).
inline constexpr std::size_t kMaxActions = 11;
using Actions = std::array<Action, kMaxActions>;

constexpr Actions acts(std::initializer_list<Action> l) noexcept {
  Actions a{};
  std::size_t i = 0;
  for (Action x : l) {
    a[i] = x; // more than kMaxActions fails constant evaluation
    ++i;
  }
  return a;
}

// One entry of ACTION_EFFECTS (drive_session_table.hpp): what an action does to the
// hidden variables.
struct ActionEffect {
  Action action;
  GuardMask sets;
  GuardMask clears;
};

// One entry of PHASE_INVARIANTS (drive_session_table.hpp).
struct PhaseInvariant {
  Phase phase;
  GuardMask must_true;
  GuardMask must_false;
};

// ---------------------------------------------------------------------------
// Sets of phases (for INPUT_PRECONDITIONS and the hold applies())
// ---------------------------------------------------------------------------
using PhaseSet = std::uint8_t;
constexpr PhaseSet pset(std::initializer_list<Phase> ps) noexcept {
  const unsigned m = std::accumulate(ps.begin(), ps.end(), 0U, [](unsigned acc, Phase p) {
    return acc | (1u << static_cast<unsigned>(p));
  });
  return static_cast<PhaseSet>(m);
}
inline constexpr PhaseSet kAllPhases = pset({Phase::LOCKED, Phase::ASKING, Phase::UNLOCKING,
                                             Phase::DRIVING, Phase::EXITING, Phase::EXIT_REFUSED});
inline constexpr PhaseSet kUnlockedPhases =
    pset({Phase::UNLOCKING, Phase::DRIVING, Phase::EXITING, Phase::EXIT_REFUSED});
constexpr bool in(PhaseSet s, Phase p) noexcept {
  return ((static_cast<unsigned>(s) >> static_cast<unsigned>(p)) & 1u) != 0;
}

// One entry of INPUT_PRECONDITIONS (drive_session_table.hpp).
struct InputPrecondition {
  Input input;
  PhaseSet phases;
  GuardExpr guard;
};

// ---------------------------------------------------------------------------
// One row of TRANSITIONS (drive_session_table.hpp)
// ---------------------------------------------------------------------------
struct Transition {
  Phase from;
  Input input;
  GuardExpr guard;
  Phase to;
  Actions actions;
  std::string_view code; // frag_*.inc:line (orig main.cpp line at e2047a4)
};

} // namespace hmi::drive_session
