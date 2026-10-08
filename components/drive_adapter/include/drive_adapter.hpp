#pragma once

/// @file drive_adapter.hpp
/// @brief The drive session's adapter: samples what a decision needs, steps the session and
///        performs its actions through a port (app-main-shrink.md S1, V4).
///
/// Moved out of the main.cpp unit (main/frag_drive.inc); since the hazard fix C1
/// (docs/plans/hazard-c1-spec.md §2.7) it also keeps the user's stop: the stop timer, the time
/// of the last DISABLE, the publish results and the Drive notice. Safety-relevant (CS-SAF-01):
/// it performs the DriveCommand requests and the relocks the session decides.
///
/// The adapter calls nothing but its port, `View`, a class that meets `DrivePort`: the
/// firmware's is drive_ui's `hmi::ui::DrivePort` (the LVGL and rtps_comms calls); the host
/// tests use a fake. `View` is a template parameter, so every port call is a direct call
/// (CS-SAF-08). No LVGL, no ESP-IDF, nothing from main/.
///
/// Contract for every method here and every port method: called on the LVGL task (lv_task)
/// with lvgl_mutex held, from an LVGL timer, event or animation callback. Not thread-safe; one
/// owner. Allocates nothing after construction (CS-SAF-04).

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "drive_notice.hpp"
#include "drive_session.hpp"
#include "logger.hpp"
#include "stick/permit_types.hpp"

namespace hmi::drive_adapter {

/// @brief What one decision is sampled from, besides the clock.
struct DriveSample {
  bool link_connected;               ///< the link CONNECTED now (live: MibStatus age < 2 s)
  hmi::drive_session::MibState mib;  ///< mib_state_subject, as the session's MibState
  hmi::drive_session::Screen screen; ///< lv_screen_active(), as the session's Screen
  bool menu_open;                    ///< nav_menu_open != nullptr
  bool calibrating;                  ///< joystick_cal_running() (C1, G5)
  hmi::stick::HoldReason hold;       ///< the stick's hold reason, as the ADC task last stored it
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
concept DrivePort = requires(View &view, bool flag, DriveBanner banner, DriveNotice notice) {
  { view.sample() } -> std::same_as<DriveSample>;
  { view.now_us() } -> std::same_as<std::int64_t>;
  { view.publish(flag) } -> std::same_as<bool>;
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
  view.show_notice(notice);
  view.refusal_feedback();
};

/// @brief A duration of the table (ms) in us.
[[nodiscard]] constexpr std::int64_t us_of(std::chrono::milliseconds ms) noexcept {
  return std::chrono::duration_cast<std::chrono::microseconds>(ms).count();
}

/// @brief The answer window (warn and exit deadlines) and the give-up window, in us: the
///        table's kDriveAnswer and kDriveWait.
inline constexpr std::int64_t kDefaultAnswerUs = us_of(hmi::drive_session::kDriveAnswer);
inline constexpr std::int64_t kDefaultWaitUs = us_of(hmi::drive_session::kDriveWait);
/// @brief The stop's timing (C1, D4), in us: the fault after the first stop, and when a
///        DISABLE re-send is due (fast before the fault, slow after; the tick's slack taken off).
inline constexpr std::int64_t kStopFaultAfterUs = us_of(hmi::drive_session::kStopFaultAfter);
inline constexpr std::int64_t kResendFastUs =
    us_of(hmi::drive_session::kStopResend - hmi::drive_session::kStopResendSlack);
inline constexpr std::int64_t kResendSlowUs =
    us_of(hmi::drive_session::kStopResendSlow - hmi::drive_session::kStopResendSlack);
static_assert(kResendFastUs == 125'000 && kResendSlowUs == 875'000);
/// @brief A failed DriveCommand publish is logged at most this often (REQ-DAD-09).
inline constexpr std::int64_t kPublishFailLogUs = 1'000'000;

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
  ///        its actions in order (the hold completions, the nav callbacks, the unlock timer);
  ///        then the Drive notice.
  /// @return true when a row of the table acted; false otherwise and on a re-entered call.
  bool input(hmi::drive_session::Input in) {
    if (!enter()) {
      return false;
    }
    const std::int64_t now = view_.now_us();
    const bool acted = apply(in, env(now), now);
    update_notice();
    busy_ = false;
    return acted;
  }

  /// @brief The 250 ms tick (rtps_poll_cb): TICK_FOLLOW (`TICK_SEQUENCE[0]`) on an Env
  ///        sampled at the start; then one more clock read and Env for the other sub-steps of
  ///        `TICK_SEQUENCE`, in its order (DRV-116); then the Drive notice.
  void tick() {
    using hmi::drive_session::Input;
    using hmi::drive_session::TICK_SEQUENCE;
    static_assert(TICK_SEQUENCE[0] == Input::TICK_FOLLOW, "the tick starts with follow-state");
    if (!enter()) {
      return;
    }
    const std::int64_t first = view_.now_us();
    (void)apply(TICK_SEQUENCE[0], env(first), first);
    const std::int64_t now = view_.now_us();
    const hmi::drive_session::Env e = env(now);
    for (std::size_t i = 1; i < TICK_SEQUENCE.size(); ++i) {
      (void)apply(TICK_SEQUENCE[i], e, now);
    }
    update_notice();
    busy_ = false;
  }

  /// @brief The unlock hold completed (rows 18-20).
  void unlock_hold_done() { (void)input(hmi::drive_session::Input::UNLOCK_HOLD_DONE); }

  /// @brief The exit hold completed: rows 21-24 unlocked, rows 42-43 locked (C1; was U3).
  void exit_hold_done() { (void)input(hmi::drive_session::Input::EXIT_HOLD_DONE); }

  /// @brief True in LOCKED and ASKING (the stick gate is shut).
  [[nodiscard]] bool locked() const { return session_.locked(); }
  /// @brief The Drive notice last handed to the port.
  [[nodiscard]] DriveNotice notice() const { return notice_; }
  /// @brief DriveCommand publishes the link refused, since start (H6, REQ-DAD-09).
  [[nodiscard]] std::uint32_t publish_failures() const { return publish_failures_; }
  /// @brief Stops the MCB did not obey within kStopFaultAfter ("MCB did not stop"), since start.
  [[nodiscard]] std::uint32_t stop_faults() const { return stop_faults_; }

private:
  friend struct DriveAdapterTestPeer; // test-only access, defined in the test tree

  // For the enumerators (Action::X). Parameters spell the type out: an enum, passed by value.
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

  // The Env of one decision: the port's sample, each deadline and the stop's timers against
  // `now` (hazard-c1-spec.md §2.1). The sample's hold reason is kept for the notice.
  hmi::drive_session::Env env(std::int64_t now) {
    const DriveSample s = view_.sample();
    hold_ = s.hold;
    return hmi::drive_session::Env{
        .link_connected = s.link_connected,
        .mib = s.mib,
        .screen = s.screen,
        .menu_open = s.menu_open,
        .exit_elapsed = now >= exit_until_us_,
        .warn_elapsed = now >= wait_warn_us_,
        .giveup_elapsed = now >= wait_until_us_,
        .calibrating = s.calibrating,
        .stop_fault_elapsed = stop_first_us_ != 0 && now - stop_first_us_ >= kStopFaultAfterUs,
        .resend = resend_due(now),
    };
  }

  // Which DISABLE re-send is due: SLOW implies FAST (hazard-c1-spec.md §2.1).
  [[nodiscard]] hmi::drive_session::Resend resend_due(std::int64_t now) const {
    using hmi::drive_session::Resend;
    const std::int64_t since = now - last_disable_us_;
    if (since >= kResendSlowUs) {
      return Resend::SLOW;
    }
    return since >= kResendFastUs ? Resend::FAST : Resend::NOT_DUE;
  }

  // One input against an Env already sampled at `now`; the actions in a local array (V4).
  bool apply(hmi::drive_session::Input in, const hmi::drive_session::Env &e, std::int64_t now) {
    hmi::drive_session::Actions actions{};
    if (!session_.step(in, e, actions)) {
      // A corrupted phase or input: `actions` is the safe state, performed below.
      logger_.error("corrupted drive session state, safe state entered (input {})",
                    static_cast<int>(in));
    }
    now_ = now;
    for (const hmi::drive_session::Action action : actions) {
      perform(action);
    }
    return actions[0] != Action::NONE;
  }

  // The Drive notice after an input or a tick: handed to the port once per change.
  void update_notice() {
    const DriveNotice n = drive_notice(session_.notice(), hold_);
    if (n != notice_) {
      notice_ = n;
      view_.show_notice(n);
    }
  }

  // One action, by the family that owns it. NONE and a value that is no Action: nothing.
  void perform(hmi::drive_session::Action action) {
    (void)(perform_request(action) || perform_deadline(action) || perform_lock(action) ||
           perform_screen(action) || perform_banner(action) || perform_stop(action));
  }

  // The DriveCommand, checked (H6): a refused publish is counted and logged at most once a
  // second; the re-send schedule does not change (the next re-send is the retry). Every
  // DISABLE published is the last DISABLE the re-send counts from.
  void publish() {
    const bool enable = request_enable_.load();
    if (!enable) {
      last_disable_us_ = now_;
    }
    if (view_.publish(enable)) {
      return;
    }
    ++publish_failures_;
    if (last_fail_log_us_ == 0 || now_ - last_fail_log_us_ >= kPublishFailLogUs) {
      last_fail_log_us_ = now_;
      logger_.warn("DriveCommand {} not published ({} failed since start)",
                   enable ? "ENABLE" : "DISABLE", publish_failures_);
    }
  }
  void ask(bool enable) {
    request_enable_.store(enable);
    publish();
  }

  // The DriveCommand: ask, re-ask, or re-publish the request as it stands.
  bool perform_request(hmi::drive_session::Action action) {
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
  bool perform_deadline(hmi::drive_session::Action action) {
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

  bool perform_latch(hmi::drive_session::Action action) {
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
  bool perform_lock(hmi::drive_session::Action action) {
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
  bool perform_screen(hmi::drive_session::Action action) {
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
  bool perform_banner(hmi::drive_session::Action action) {
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

  // The user's stop (C1, G4): the stop timer and the fault.
  bool perform_stop(hmi::drive_session::Action action) {
    switch (action) {
    case Action::ARM_STOP_TIMER:
      stop_first_us_ = now_;
      return true;
    case Action::RAISE_STOP_FAULT:
      ++stop_faults_;
      logger_.error("MCB did not stop: still ENABLED {} ms after the first DISABLE ({} since "
                    "start)",
                    (now_ - stop_first_us_) / 1000, stop_faults_);
      return true;
    case Action::CLEAR_STOP_FAULT:
      stop_first_us_ = 0;
      return true;
    default:
      return false;
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
  // The user's stop (C1): the first stop of this exit (0 = not armed; ARM_STOP_TIMER,
  // CLEAR_STOP_FAULT) and the last DISABLE published (the re-send counts from it).
  std::int64_t stop_first_us_ = 0;
  std::int64_t last_disable_us_ = 0;
  // The publish results (H6) and the stop faults, counted; the last failure logged.
  std::uint32_t publish_failures_ = 0;
  std::uint32_t stop_faults_ = 0;
  std::int64_t last_fail_log_us_ = 0;
  // The clock of the decision whose actions are being performed.
  std::int64_t now_ = 0;
  // The stick's hold reason from the latest sample, and the notice last shown.
  hmi::stick::HoldReason hold_ = hmi::stick::HoldReason::NONE;
  DriveNotice notice_ = DriveNotice::NONE;
  bool busy_ = false; // an input is being performed (re-entry check)
  espp::Logger logger_;
};

} // namespace hmi::drive_adapter
