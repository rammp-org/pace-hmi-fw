#pragma once

/// @file drive_adapter.hpp
/// @brief The drive session's adapter: samples what a decision needs, steps the session and
///        performs its actions through a port (app-main-shrink.md S1, V4).
///
/// Moved out of the main.cpp unit (main/frag_drive.inc) unchanged in behaviour: the same
/// samples, clock reads, LVGL and RTPS calls, in the same order, pinned by the drive goldens
/// (tests/host/drive_golden, GLD-001..004). Safety-relevant (CS-SAF-01): it performs the
/// DriveCommand requests and the relocks the session decides.
///
/// The adapter calls nothing but its port, `View`, a class that meets `DrivePort`: the
/// firmware's is `MainDriveView` in main/frag_drive.inc (the LVGL and rtps_comms calls); the
/// host tests use a fake. `View` is a template parameter, so every port call is a direct call
/// (CS-SAF-08). No LVGL, no ESP-IDF, nothing from main/.
///
/// Contract for every method here and every port method: called on the LVGL task (lv_task)
/// with lvgl_mutex held, from an LVGL timer, event or animation callback. Not thread-safe; one
/// owner. Allocates nothing after construction (CS-SAF-04).

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>

#include "drive_session.hpp"
#include "logger.hpp"

namespace hmi::drive_adapter {

/// @brief What one decision is sampled from, besides the clock.
struct DriveSample {
  bool link_connected;               ///< rtps_link_subject == CONNECTED
  hmi::drive_session::MibState mib;  ///< mib_state_subject, as the session's MibState
  hmi::drive_session::Screen screen; ///< lv_screen_active(), as the session's Screen
  bool menu_open;                    ///< nav_menu_open != nullptr
};

/// @brief The banners the session raises (Action::SHOW_*), in Action order.
enum class DriveBanner : std::uint8_t {
  REFUSED_DRIVE,
  REFUSED_SEAT,
  NOT_GRANTED,
  DRIVE_STOPPED,
  EXIT_REFUSED,
  DRIVE_LOST,
  REFUSED_DRIVE_MENU,
};

/// @brief The port: what the adapter needs from the UI and the link (README, "The port").
/// @details Every method: LVGL task, lvgl_mutex held; none may call back into the adapter.
template <class View>
concept DrivePort = requires(View &view, bool flag, DriveBanner banner) {
  { view.sample() } -> std::same_as<DriveSample>;
  { view.now_us() } -> std::same_as<std::int64_t>;
  view.publish(flag);
  view.ring_wait();
  view.ring_rest();
  view.lock_open_visual();
  view.unlock_timer_start();
  view.unlock_timer_cancel();
  view.unlock_timer_forget();
  view.set_locked(flag);
  view.gate_update();
  view.menu_on_arrival(flag);
  view.go_locked_screen();
  view.go_drive_screen();
  view.nav_home();
  view.show_banner(banner);
  view.refusal_feedback();
};

/// @brief The answer window (warn and exit deadlines) and the give-up window, in us: the
///        table's kDriveAnswer and kDriveWait.
inline constexpr std::int64_t kDefaultAnswerUs =
    std::chrono::duration_cast<std::chrono::microseconds>(hmi::drive_session::kDriveAnswer).count();
inline constexpr std::int64_t kDefaultWaitUs =
    std::chrono::duration_cast<std::chrono::microseconds>(hmi::drive_session::kDriveWait).count();

/// @brief The drive session's side of the LVGL task: the session, its timestamps and latches.
/// @tparam View The port (DrivePort), held by value.
template <DrivePort View> class DriveAdapter {
public:
  /// @brief Configuration.
  struct Config {
    View view;                                 ///< the port
    std::int64_t answer_us = kDefaultAnswerUs; ///< warn and exit deadline after the ask
    std::int64_t wait_us = kDefaultWaitUs;     ///< give-up deadline after the ask
    espp::Logger::Verbosity log_level = espp::Logger::Verbosity::INFO; ///< for the fault path
  };

  /// @brief LOCKED, DISABLE as the request, nothing armed. Builds the logger now, so the fault
  ///        path constructs nothing (CS-SAF-04).
  explicit DriveAdapter(const Config &config)
      : view_(config.view)
      , answer_us_(config.answer_us)
      , wait_us_(config.wait_us)
      , logger_({.tag = "drive", .level = config.log_level}) {}

  /// @brief One input now: samples the clock, then the port, steps the session and performs
  ///        its actions in order (the hold completions, the nav callbacks, the unlock timer).
  /// @return true when a row of the table acted; false otherwise and on a re-entered call.
  bool input(hmi::drive_session::Input in) {
    if (!enter()) {
      return false;
    }
    const bool acted = apply(in, env(view_.now_us()));
    busy_ = false;
    return acted;
  }

  /// @brief The 250 ms tick (rtps_poll_cb): TICK_FOLLOW on an Env sampled at the start; then
  ///        one more clock read and Env for the three deadline checks, with TABLE.md U3's
  ///        locked exit deadline between the first two (TICK_SEQUENCE, DRV-022).
  void tick() {
    using hmi::drive_session::Input;
    if (!enter()) {
      return;
    }
    (void)apply(Input::TICK_FOLLOW, env(view_.now_us()));
    const std::int64_t now = view_.now_us();
    const hmi::drive_session::Env e = env(now);
    (void)apply(Input::TICK_EXIT_DUE, e);
    exit_due_while_locked(now); // U3
    (void)apply(Input::TICK_WARN_DUE, e);
    (void)apply(Input::TICK_GIVEUP_DUE, e);
    busy_ = false;
  }

  /// @brief The unlock hold completed (rows 18-20).
  void unlock_hold_done() { (void)input(hmi::drive_session::Input::UNLOCK_HOLD_DONE); }

  /// @brief The exit hold completed (rows 21-24). Locked, it is TABLE.md U3, kept as it was
  ///        and outside the table: DISABLE, the exit latched and its deadline armed.
  void exit_hold_done() {
    if (!enter()) {
      return;
    }
    if (session_.locked()) {
      exit_ask_while_locked();
    } else {
      (void)apply(hmi::drive_session::Input::EXIT_HOLD_DONE, env(view_.now_us()));
    }
    busy_ = false;
  }

  /// @brief True in LOCKED and ASKING (the stick gate is shut).
  [[nodiscard]] bool locked() const { return session_.locked(); }

private:
  friend struct DriveAdapterTestPeer; // test-only access, defined in the test tree

  using Action = hmi::drive_session::Action;

  // A recovering check (CS-ERR-04): an input that arrives while another is being performed
  // (a port method calling back) is dropped and reported; the one in progress completes.
  bool enter() {
    if (busy_) {
      logger_.error("drive input re-entered while one is performed: dropped");
      return false;
    }
    busy_ = true;
    return true;
  }

  // The Env of one decision: the port's sample, each deadline against `now`.
  hmi::drive_session::Env env(std::int64_t now) {
    const DriveSample s = view_.sample();
    return hmi::drive_session::Env{
        .link_connected = s.link_connected,
        .mib = s.mib,
        .screen = s.screen,
        .menu_open = s.menu_open,
        .exit_elapsed = now >= exit_until_us_,
        .warn_elapsed = now >= wait_warn_us_,
        .giveup_elapsed = now >= wait_until_us_,
    };
  }

  // One input against an Env already sampled; the actions in a local array (V4).
  bool apply(hmi::drive_session::Input in, const hmi::drive_session::Env &e) {
    hmi::drive_session::Actions actions{};
    if (!session_.step(in, e, actions)) {
      // A corrupted phase or input: `actions` is the safe state, performed below.
      logger_.error("corrupted drive session state, safe state entered (input {})",
                    static_cast<int>(in));
    }
    for (const Action action : actions) {
      perform(action);
    }
    return actions[0] != Action::NONE;
  }

  // One action, by the family that owns it. NONE and a value that is no Action: nothing.
  void perform(Action action) {
    (void)(perform_request(action) || perform_deadline(action) || perform_lock(action) ||
           perform_screen(action) || perform_banner(action));
  }

  void publish() { view_.publish(request_enable_.load()); }
  void ask(bool enable) {
    request_enable_.store(enable);
    publish();
  }

  // The DriveCommand: ask, re-ask, or re-publish the request as it stands.
  bool perform_request(Action action) {
    switch (action) {
    case Action::SEND_ENABLE:
      ask_at_us_ = view_.now_us();
      ask(true);
      return true;
    case Action::SEND_DISABLE:
      ask(false);
      return true;
    case Action::PUBLISH_DRIVE:
      publish();
      return true;
    default:
      return false;
    }
  }

  // The deadlines and the exit latches.
  bool perform_deadline(Action action) {
    switch (action) {
    case Action::ARM_WARN:
      wait_warn_us_ = ask_at_us_ + answer_us_;
      return true;
    case Action::ARM_GIVEUP:
      wait_until_us_ = ask_at_us_ + wait_us_;
      return true;
    case Action::CLEAR_WARN:
      wait_warn_us_ = 0;
      return true;
    case Action::CLEAR_GIVEUP:
      wait_until_us_ = 0;
      return true;
    case Action::ARM_EXIT_DEADLINE:
      exit_until_us_ = view_.now_us() + answer_us_;
      return true;
    case Action::CLEAR_EXIT_DEADLINE:
      exit_until_us_ = 0;
      return true;
    default:
      return perform_latch(action);
    }
  }

  bool perform_latch(Action action) {
    switch (action) {
    case Action::SET_EXIT_REQUESTED:
      exit_requested_ = true;
      return true;
    case Action::CLEAR_EXIT_REQUESTED:
      exit_requested_ = false;
      return true;
    case Action::SET_THEN_MENU:
      exit_then_menu_ = true;
      return true;
    case Action::CLEAR_THEN_MENU:
      exit_then_menu_ = false;
      return true;
    default:
      return false;
    }
  }

  // The padlock, the lock state and the stick gate.
  bool perform_lock(Action action) {
    switch (action) {
    case Action::RING_WAIT:
      view_.ring_wait();
      return true;
    case Action::RING_REST:
      view_.ring_rest();
      return true;
    case Action::LOCK_OPEN_VISUAL:
      view_.lock_open_visual();
      return true;
    case Action::CANCEL_UNLOCK_TIMER:
      view_.unlock_timer_cancel();
      return true;
    case Action::START_UNLOCK_TIMER:
      view_.unlock_timer_start();
      return true;
    case Action::SET_UNLOCKED:
      view_.set_locked(false);
      return true;
    case Action::SET_LOCKED:
      view_.set_locked(true);
      return true;
    case Action::GATE_UPDATE:
      view_.gate_update();
      return true;
    default:
      return false;
    }
  }

  // The screens: the Locked screen (and the menu over it), the Drive screen, home.
  bool perform_screen(Action action) {
    switch (action) {
    case Action::OPEN_MENU_ON_ARRIVAL:
      view_.menu_on_arrival(true);
      return true;
    case Action::CLEAR_MENU_ON_ARRIVAL:
      view_.menu_on_arrival(false);
      return true;
    case Action::GO_LOCKED_SCREEN:
      view_.go_locked_screen();
      return true;
    case Action::UNLOCK_TIMER_DONE:
      view_.unlock_timer_forget();
      return true;
    case Action::GO_DRIVE_SCREEN:
      view_.go_drive_screen();
      return true;
    case Action::NAV_HOME:
      view_.nav_home();
      return true;
    default:
      return false;
    }
  }

  // The banners and the refusal feedback.
  bool perform_banner(Action action) {
    switch (action) {
    case Action::SHOW_REFUSED_DRIVE:
      view_.show_banner(DriveBanner::REFUSED_DRIVE);
      return true;
    case Action::SHOW_REFUSED_SEAT:
      view_.show_banner(DriveBanner::REFUSED_SEAT);
      return true;
    case Action::SHOW_NOT_GRANTED:
      view_.show_banner(DriveBanner::NOT_GRANTED);
      return true;
    case Action::SHOW_DRIVE_STOPPED:
      view_.show_banner(DriveBanner::DRIVE_STOPPED);
      return true;
    case Action::SHOW_EXIT_REFUSED:
      view_.show_banner(DriveBanner::EXIT_REFUSED);
      return true;
    case Action::SHOW_DRIVE_LOST:
      view_.show_banner(DriveBanner::DRIVE_LOST);
      return true;
    case Action::SHOW_REFUSED_DRIVE_MENU:
      view_.show_banner(DriveBanner::REFUSED_DRIVE_MENU);
      return true;
    case Action::REFUSAL_FEEDBACK:
      view_.refusal_feedback();
      return true;
    default:
      return false;
    }
  }

  // TABLE.md U3, kept exactly as it was and outside the table (parked for the owner;
  // removing it is a behaviour change): the exit hold has no lock check, so a relock that
  // lands between the last hold poll and the end of the fill still asks the MIB to stop and
  // latches the exit while locked, and that exit's deadline still raises its banner. The
  // session is not told; the next unlock (rows 1-2) clears the latch.
  void exit_ask_while_locked() {
    ask(false);
    exit_requested_ = true;
    exit_then_menu_ = false;
    exit_until_us_ = view_.now_us() + answer_us_;
  }

  void exit_due_while_locked(std::int64_t now) {
    if (session_.locked() && exit_until_us_ != 0 && now >= exit_until_us_) {
      exit_until_us_ = 0;
      exit_then_menu_ = false;
      view_.show_banner(DriveBanner::EXIT_REFUSED);
    }
  }

  View view_;
  std::int64_t answer_us_;
  std::int64_t wait_us_;
  // The DriveCommand request as it stands (true = ENABLE), re-published on a profile click.
  std::atomic<bool> request_enable_{false};
  std::int64_t wait_until_us_ = 0; // 0 = not waiting for an answer
  std::int64_t wait_warn_us_ = 0;  // 0 = nothing left to say about that request
  // Leaving is a request too: how long to wait before telling the user it was refused.
  std::int64_t exit_until_us_ = 0; // 0 = not waiting for the MIB to stop
  // Latched separately from the deadline: a stop the MIB grants late still reads as the
  // user leaving rather than as the chair stopping on its own.
  bool exit_requested_ = false;
  // The stop was asked by the burger key: the menu opens over Locked once it is granted.
  bool exit_then_menu_ = false;
  hmi::drive_session::DriveSession session_;
  // When SEND_ENABLE asked: ARM_WARN and ARM_GIVEUP count from it.
  std::int64_t ask_at_us_ = 0;
  bool busy_ = false; // an input is being performed (re-entry check)
  espp::Logger logger_;
};

} // namespace hmi::drive_adapter
