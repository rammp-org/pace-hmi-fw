#pragma once

/// @file drive_session.hpp
/// @brief The drive session state machine: lock, ask, unlock, drive, exit (CS-SAF-02).
///
/// The hand-written transition function for the AS-IS table in drive_session_table.hpp. The
/// table is the spec and the test oracle (TS-UNIT-08); this class is the code checked against
/// it. Pure C++: no LVGL, no ESP-IDF, no clock. Time, link, MIB state, screen and menu arrive as
/// an `Env` sampled by the caller, so a step is fully deterministic (CS-HAL-03).
///
/// The session decides; the caller acts. `step()` returns the table's actions, in order, and the
/// caller performs each one with the LVGL and RTPS calls it stands for, on the UI task. The
/// session holds only the phase and the table's hidden variables; the deadlines' timestamps,
/// the padlock widgets and the subjects stay with the caller.

#include <cstddef>

#include "drive_session_table.hpp"

namespace hmi::drive_session {

/// @brief The drive session: Phase plus the table's five hidden variables.
/// @details Not thread-safe: one owner, the UI (LVGL) task. Allocates nothing.
class DriveSession {
public:
  /// @brief Starts LOCKED with every hidden variable clear (drive_request = DISABLE).
  constexpr DriveSession() noexcept = default;

  /// @brief Applies one input (CS-SAF-02).
  /// @details A row of the table that matches (phase, input, guards) moves the phase to the
  ///          row's `to`, updates the hidden variables and writes the row's actions to @p out,
  ///          in order, padded with `Action::NONE`. No matching row: nothing changes and @p out
  ///          is all `NONE`. A phase or input outside its enum (a corrupted value) sends the
  ///          session to the safe state: LOCKED, DISABLE sent, no menu on arrival, gate
  ///          update requested.
  /// @param in The input. A tick is the four inputs of `TICK_SEQUENCE`, in order: TICK_FOLLOW
  ///           on the Env at the tick's start, the three deadline checks on one Env sampled
  ///           after TICK_FOLLOW's actions were performed (README, "Model").
  /// @param env The environment sampled when the input arrived.
  /// @param out The actions for the caller to perform, in order.
  /// @return false only when a corrupted value forced the safe state; the caller logs it.
  [[nodiscard]] bool step(Input in, const Env &env, Actions &out) noexcept;

  /// @brief Enters the safe state on the caller's request (e.g. a failed check upstream).
  /// @param out The safe-state actions for the caller to perform, in order.
  void enter_safe_state(Actions &out) noexcept;

  /// @brief The current phase.
  [[nodiscard]] constexpr Phase phase() const noexcept { return phase_; }

  /// @brief True in LOCKED and ASKING: the stick gate is shut (`stick_drives`).
  [[nodiscard]] constexpr bool locked() const noexcept {
    return phase_ == Phase::LOCKED || phase_ == Phase::ASKING;
  }

  /// @brief The value of drive_request: true = ENABLE, false = DISABLE (PUBLISH_DRIVE sends it).
  [[nodiscard]] constexpr bool request_enable() const noexcept {
    return (hidden_ & bit(Guard::REQUEST_ENABLE)) != 0;
  }

  /// @brief The hidden variables, as the table's guard bits (a subset of `kHiddenGuards`).
  [[nodiscard]] constexpr GuardMask hidden() const noexcept { return hidden_; }

  /// @brief The safe-state actions, in order (also what a corrupted value produces).
  static constexpr Actions SAFE_STATE_ACTIONS{
      Action::SEND_DISABLE,         Action::CLEAR_WARN,      Action::CLEAR_EXIT_DEADLINE,
      Action::CLEAR_EXIT_REQUESTED, Action::CLEAR_THEN_MENU, Action::CLEAR_MENU_ON_ARRIVAL,
      Action::CANCEL_UNLOCK_TIMER,  Action::RING_REST,       Action::GO_LOCKED_SCREEN,
      Action::SET_LOCKED,           Action::GATE_UPDATE};

private:
  friend struct DriveSessionTestPeer; // test-only access, defined in the test tree

  /// What one phase handler decided: the next phase and the actions, or a fault.
  struct Outcome {
    Phase to;
    Actions actions;
    bool ok;
  };
  /// A row taken (or, with the current phase and no actions, no row).
  [[nodiscard]] static constexpr Outcome go(Phase to, const Actions &actions) noexcept {
    return Outcome{.to = to, .actions = actions, .ok = true};
  }
  /// A corrupted phase or input: the caller of decide() enters the safe state.
  [[nodiscard]] static constexpr Outcome fault() noexcept {
    return Outcome{.to = Phase::LOCKED, .actions = Actions{}, .ok = false};
  }

  [[nodiscard]] Outcome decide(Input in, GuardMask g) const noexcept;
  [[nodiscard]] Outcome on_locked(Input in, GuardMask g) const noexcept;
  [[nodiscard]] Outcome on_asking(Input in, GuardMask g) const noexcept;
  [[nodiscard]] Outcome on_unlocking(Input in, GuardMask g) const noexcept;
  [[nodiscard]] Outcome on_driving(Input in, GuardMask g) const noexcept;
  [[nodiscard]] Outcome on_exiting(Input in, GuardMask g) const noexcept;
  [[nodiscard]] Outcome on_exit_refused(Input in, GuardMask g) const noexcept;
  [[nodiscard]] Outcome stay() const noexcept;
  void perform(const Outcome &o, Actions &out) noexcept;

  Phase phase_ = Phase::LOCKED;
  // The hidden variables, one Guard bit each: WARN_ARMED (drive_wait_warn_us != 0),
  // GIVEUP_ARMED (drive_wait_until_us != 0), THEN_MENU (drive_exit_then_menu),
  // REQUEST_ENABLE (drive_request == ENABLE), UNLOCK_TIMER_ARMED (unlock_advance_timer set).
  GuardMask hidden_ = 0;
};

} // namespace hmi::drive_session
