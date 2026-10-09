#pragma once
// drive_session_hold.hpp: the hold gestures of the drive session, as data (where UNLOCK_HOLD_DONE
// and EXIT_HOLD_DONE come from): HOLD_TRANSITIONS, UNLOCK_APPLIES, EXIT_APPLIES and
// HOLD_POLL_SEQUENCE, with their static checks. Moved unchanged out of drive_session_table.hpp,
// which includes it, so that each file stays within CS-FIL-01; it is part of the same spec and of
// the same fingerprint (drive_session_fingerprint.hpp), which proves the move changed no row.
//
// Pure C++23: no LVGL, no ESP-IDF, no FreeRTOS.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <string_view>

#include "drive_session_types.hpp"

namespace hmi::drive_session {

// ---------------------------------------------------------------------------
// The hold gestures (frag_hold.inc, frag_hold_poll.inc): where UNLOCK_HOLD_DONE
// and EXIT_HOLD_DONE come from, and the shared "let go first" latch
// joy_button_armed (frag_hold.inc:9, orig 961) that both gestures use.
// ---------------------------------------------------------------------------
// One machine per gesture (unlock, exit), same rows. The calibrate gesture has
// its own latch (calibrate_armed) and is out of scope.
enum class HoldState : std::uint8_t { IDLE, FILLING };
enum class HoldInput : std::uint8_t {
  POLL,      // hold_poll(g) from hold_poll_cb every kHoldPollPeriod; or the overlay pass
  FILL_DONE, // hold_anim_completed_cb: kBarGrace + kHoldFill after START_FILL
};
enum class HoldGuard : std::uint8_t {
  OVERLAY, // selftest_ui_visible()
  HELD,    // joy_button_pressed (level)
  ARMED,   // joy_button_armed (shared by unlock and exit)
  APPLIES, // the gesture's applies(), see UNLOCK_APPLIES / EXIT_APPLIES
};
enum class HoldAction : std::uint8_t {
  NONE,
  SET_ARMED,   // joy_button_armed := true (released)
  CLEAR_ARMED, // joy_button_armed := false (before completed(), frag_hold.inc:41)
  START_FILL,  // anim 0..kHoldMax over kHoldFill after kBarGrace
  CANCEL_FILL, // hold_reset: delete anim, progress := 0
  CONFIRM,     // STRONG_CLICK haptic + click sound
  COMPLETE,    // g->completed(): UNLOCK_HOLD_DONE or EXIT_HOLD_DONE
};
using HoldMask = std::uint8_t;
constexpr HoldMask hbit(HoldGuard g) noexcept {
  return static_cast<HoldMask>(1u << static_cast<unsigned>(g));
}
constexpr HoldMask hmask(std::initializer_list<HoldGuard> gs) noexcept {
  const unsigned m = std::accumulate(gs.begin(), gs.end(), 0U,
                                     [](unsigned acc, HoldGuard g) { return acc | hbit(g); });
  return static_cast<HoldMask>(m);
}
struct HoldGuardExpr {
  HoldMask need_true;
  HoldMask need_false;
};
using HoldActions = std::array<HoldAction, 4>;
struct HoldTransition {
  HoldState from;
  HoldInput input;
  HoldGuardExpr guard;
  HoldState to;
  HoldActions actions;
  std::string_view code;
};

inline constexpr std::array HOLD_TRANSITIONS{
    // hold_poll_cb with the self-test overlay up: cancel, nothing else (armed untouched).
    HoldTransition{HoldState::IDLE,
                   HoldInput::POLL,
                   {hbit(HoldGuard::OVERLAY), 0},
                   HoldState::IDLE,
                   {},
                   "frag_hold_poll.inc:69-76 (orig 1794-1801)"},
    HoldTransition{HoldState::FILLING,
                   HoldInput::POLL,
                   {hbit(HoldGuard::OVERLAY), 0},
                   HoldState::IDLE,
                   {HoldAction::CANCEL_FILL},
                   "frag_hold_poll.inc:69-76 (orig 1794-1801)"},
    // Released: re-arm; a fill in progress is cancelled.
    HoldTransition{HoldState::IDLE,
                   HoldInput::POLL,
                   {0, hmask({HoldGuard::OVERLAY, HoldGuard::HELD})},
                   HoldState::IDLE,
                   {HoldAction::SET_ARMED},
                   "frag_hold.inc:60-62 (orig 1012-1014)"},
    HoldTransition{HoldState::FILLING,
                   HoldInput::POLL,
                   {0, hmask({HoldGuard::OVERLAY, HoldGuard::HELD})},
                   HoldState::IDLE,
                   {HoldAction::SET_ARMED, HoldAction::CANCEL_FILL},
                   "frag_hold.inc:60-70 (orig 1012-1022)"},
    // Held, armed, applies: start (rising edge) or keep filling.
    HoldTransition{
        HoldState::IDLE,
        HoldInput::POLL,
        {hmask({HoldGuard::HELD, HoldGuard::ARMED, HoldGuard::APPLIES}), hbit(HoldGuard::OVERLAY)},
        HoldState::FILLING,
        {HoldAction::START_FILL},
        "frag_hold.inc:63-84 (orig 1015-1036)"},
    HoldTransition{
        HoldState::FILLING,
        HoldInput::POLL,
        {hmask({HoldGuard::HELD, HoldGuard::ARMED, HoldGuard::APPLIES}), hbit(HoldGuard::OVERLAY)},
        HoldState::FILLING,
        {},
        "frag_hold.inc:63-66 (orig 1015-1018)"},
    // Held but not armed (not let go since the last completion): nothing / cancel.
    HoldTransition{HoldState::IDLE,
                   HoldInput::POLL,
                   {hbit(HoldGuard::HELD), hmask({HoldGuard::OVERLAY, HoldGuard::ARMED})},
                   HoldState::IDLE,
                   {},
                   "frag_hold.inc:63-66 (orig 1015-1018)"},
    HoldTransition{HoldState::FILLING,
                   HoldInput::POLL,
                   {hbit(HoldGuard::HELD), hmask({HoldGuard::OVERLAY, HoldGuard::ARMED})},
                   HoldState::IDLE,
                   {HoldAction::CANCEL_FILL},
                   "frag_hold.inc:63-70 (orig 1015-1022)"},
    // Held and armed but applies() false (mcb_ready dropped, menu opened,
    // screen changed, lock_waiting): nothing / cancel mid-fill.
    HoldTransition{HoldState::IDLE,
                   HoldInput::POLL,
                   {hmask({HoldGuard::HELD, HoldGuard::ARMED}),
                    hmask({HoldGuard::OVERLAY, HoldGuard::APPLIES})},
                   HoldState::IDLE,
                   {},
                   "frag_hold.inc:63-66 (orig 1015-1018)"},
    HoldTransition{HoldState::FILLING,
                   HoldInput::POLL,
                   {hmask({HoldGuard::HELD, HoldGuard::ARMED}),
                    hmask({HoldGuard::OVERLAY, HoldGuard::APPLIES})},
                   HoldState::IDLE,
                   {HoldAction::CANCEL_FILL},
                   "frag_hold.inc:63-70 (orig 1015-1022)"},
    // The fill completed: no applies() re-check here.
    HoldTransition{HoldState::FILLING,
                   HoldInput::FILL_DONE,
                   {0, 0},
                   HoldState::IDLE,
                   {HoldAction::CLEAR_ARMED, HoldAction::CANCEL_FILL, HoldAction::CONFIRM,
                    HoldAction::COMPLETE},
                   "frag_hold.inc:36-53 (orig 988-1005)"},
};
inline constexpr std::size_t kHoldTransitionCount = 11;
static_assert(HOLD_TRANSITIONS.size() == kHoldTransitionCount);

// applies() of each gesture, over the session's guards plus the phase.
struct HoldApplies {
  std::string_view gesture;
  PhaseSet phases; // phases in which applies() can be true
  GuardExpr guard;
  std::string_view code;
};
// unlock: locked && !lock_waiting (= LOCKED) && Locked screen && no menu && mcb_ready, and
// (C3) POST passed: the hold does not fill before POST pass; ENTRY_PUSH (row 53) says why.
inline constexpr HoldApplies UNLOCK_APPLIES{
    "unlock", pset({Phase::LOCKED}),
    when({Guard::ON_LOCKED_SCREEN, Guard::MCB_READY, Guard::POST_OK}, {Guard::MENU_OPEN}),
    "frag_lock.inc:96-97 (orig 1161-1162); hazard-c3-spec.md 4.4"};
// exit: Drive screen && no menu. No lock or phase condition in the code; the
// phase set is where the Drive screen can be up (see TABLE.md U3).
inline constexpr HoldApplies EXIT_APPLIES{"exit", kAllPhases,
                                          when({Guard::ON_DRIVE_SCREEN}, {Guard::MENU_OPEN}),
                                          "frag_drive.inc:194 (orig 1717)"};

// One hold_poll_cb pass (frag_hold_poll.inc:65-82, orig 1790-1807): with the
// overlay up, only the overlay rows (no ENTRY_PUSH, no re-arm); otherwise
// ENTRY_PUSH first, then the unlock gesture, then the exit gesture (then
// calibrate, out of scope). The two share joy_button_armed, so a re-arm by
// the unlock poll is seen by the exit poll in the same pass.
enum class HoldPollStep : std::uint8_t { ENTRY_PUSH, UNLOCK_POLL, EXIT_POLL };
inline constexpr std::array HOLD_POLL_SEQUENCE{HoldPollStep::ENTRY_PUSH, HoldPollStep::UNLOCK_POLL,
                                               HoldPollStep::EXIT_POLL};

namespace detail {
constexpr bool hold_rows_exclusive() noexcept {
  for (std::size_t i = 0; i < HOLD_TRANSITIONS.size(); ++i) {
    for (std::size_t j = i + 1; j < HOLD_TRANSITIONS.size(); ++j) {
      const HoldTransition &a = HOLD_TRANSITIONS[i];
      const HoldTransition &b = HOLD_TRANSITIONS[j];
      if (a.from == b.from && a.input == b.input &&
          ((a.guard.need_true & b.guard.need_false) | (a.guard.need_false & b.guard.need_true)) ==
              0) {
        return false;
      }
    }
  }
  return true;
}
// The POLL rows of state `s` that match guard combination `m`.
constexpr std::ptrdiff_t hold_poll_hits(HoldState s, unsigned m) noexcept {
  return std::ranges::count_if(HOLD_TRANSITIONS, [s, m](const HoldTransition &t) {
    return t.from == s && t.input == HoldInput::POLL &&
           (m & t.guard.need_true) == t.guard.need_true && (m & t.guard.need_false) == 0;
  });
}
constexpr bool hold_state_poll_complete(HoldState s) noexcept {
  for (unsigned m = 0; m < 16; ++m) {
    if (hold_poll_hits(s, m) != 1) {
      return false;
    }
  }
  return true;
}
// Every (state, POLL, guard combination) is covered by exactly one row.
constexpr bool hold_poll_complete() noexcept {
  constexpr std::array<HoldState, 2> kStates{HoldState::IDLE, HoldState::FILLING};
  return std::ranges::all_of(kStates, hold_state_poll_complete);
}
} // namespace detail
static_assert(detail::hold_rows_exclusive(), "two hold rows can both match");
static_assert(detail::hold_poll_complete(), "a hold poll combination with no row, or two");

} // namespace hmi::drive_session
