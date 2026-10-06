#pragma once

/**
 * @file cal_run.hpp
 * @brief The calibration run as a plain C++ model: which step it is on, what it has measured,
 *        and what the view must do next. No LVGL, no storage, no logging (CS-UI-03).
 *
 * The run prompts, one step at a time: let go, push left, right, forward, back, twist
 * clockwise, counter-clockwise, let go. The view (main/joystick_cal.cpp) feeds it START,
 * CANCEL and one TICK per timer period with the latest raw sample, and carries out the Action
 * each call returns: prompts, feedback, saving, the result text.
 *
 * Time is counted in periods of the view's timer (kTickMs), not read from a clock: that is
 * how the firmware has always timed the run, and it is what the tests pin. A late timer
 * stretches a step; it never shortens one.
 *
 * The run is a state machine specified by kTransitions (CS-SAF-02), written from the code as
 * it was at dev_refactor fa3f13a. decide() is the hand-written transition function; the L1
 * oracle test (CAL-3xx) checks it against the table for every phase, input and guard.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "cal_record.hpp"

namespace hmi::cal {

/// One raw ADC reading of every axis, in Record order, mV.
using Sample = std::array<float, kAxisCount>;

/// The view's timer period. The ADC task samples at the same rate, so every period normally
/// sees a fresh sample.
inline constexpr std::uint32_t kTickMs = 33;
inline constexpr int kTicksPerSecond = static_cast<int>(1000 / kTickMs); // 30
/// Time to read the first prompt and take a hand off the stick before rest is sampled.
inline constexpr int kSettleTicks = kTicksPerSecond * 3 / 2;
/// A position is taken once the axis has held it this long...
inline constexpr int kHoldTicks = kTicksPerSecond;
/// ...moving no more than this, peak to peak. Twist is the noisy pot.
inline constexpr std::array<float, kAxisCount> kSteadyMv = {30.0f, 30.0f, 60.0f};
/// Within this of rest on every axis counts as let go, for the last step.
inline constexpr float kReleasedMv = 150.0f;
/// Each step's limit: more periods than this without being taken ends the run.
inline constexpr int kStepTimeoutTicks = kTicksPerSecond * 20;
/// How long the result stays up before the screen's own text comes back.
inline constexpr int kResultTicks = kTicksPerSecond * 3;
/// Let go, six directions, let go.
inline constexpr int kStepCount = 8;

/// One end of travel the run asks for.
struct Direction {
  std::size_t axis; ///< index into Record
  float sign;       ///< +1: this end reads above rest (fills max_mv), -1: below (min_mv)
  const char *prompt;
};
/// In the order they are asked for. Signs follow main.cpp's AXIS WIRING note: horizontal
/// reads higher to the right, vertical reads LOWER pushed forward, twist reads higher
/// clockwise.
inline constexpr std::array<Direction, 6> kDirections{{
    {0, -1.0f, "Push the joystick fully LEFT and hold it"},
    {0, +1.0f, "Push the joystick fully RIGHT and hold it"},
    {1, -1.0f, "Push the joystick fully FORWARD and hold it"},
    {1, +1.0f, "Pull the joystick fully BACK and hold it"},
    {2, +1.0f, "Twist the joystick fully CLOCKWISE and hold it"},
    {2, -1.0f, "Twist the joystick fully COUNTER-CLOCKWISE and hold it"},
}};

enum class Phase : std::uint8_t {
  IDLE,      ///< no run, nothing shown
  REST,      ///< step 1: let go; rest is sampled
  DIRECTION, ///< steps 2-7: one end (direction() says which)
  RELEASE,   ///< step 8: let go again
  RESULT,    ///< the outcome is shown
};
inline constexpr std::size_t kPhaseCount = 5;

enum class Input : std::uint8_t {
  START,  ///< the view starts a run (it sends this only when none is going)
  CANCEL, ///< the menu, the button, or leaving the screen
  TICK,   ///< one timer period, with the latest sample
};
inline constexpr std::size_t kInputCount = 3;

/// What the extended state says about an input. START and CANCEL carry NONE; a TICK in IDLE
/// carries NONE; any other TICK carries exactly one of the rest.
enum class Guard : std::uint8_t {
  NONE,
  WAITING,      ///< the step is neither taken nor timed out; the result is still up
  TAKEN,        ///< rest, or an end that is not the last, held steady for kHoldTicks
  LAST_TAKEN,   ///< the last end held steady for kHoldTicks
  RELEASED,     ///< let go for kHoldTicks, and the record is plausible()
  RELEASED_BAD, ///< let go for kHoldTicks, but the record is not plausible()
  TIMED_OUT,    ///< more than kStepTimeoutTicks in this step
  RESULT_OVER,  ///< the result has been up for kResultTicks
};
inline constexpr std::size_t kGuardCount = 8;

/// What the view must do after a transition.
enum class Action : std::uint8_t {
  NONE,
  BEGIN,     ///< mark running, prompt(), resume the timer, log "calibration started"
  NEXT,      ///< a step was taken: feedback, then prompt()
  COMPLETE,  ///< save record(), use it, show the outcome
  REJECT,    ///< log record() as rejected, show "travel too short"
  TIME_OUT,  ///< log from_step(), show "Timed out"
  CANCEL,    ///< log why, show "cancelled"
  SHOW_IDLE, ///< put the screen's own text back, pause the timer
  PAUSE,     ///< pause the timer
};

struct Transition {
  Phase from;
  Input input;
  Guard guard;
  Phase to;
  Action action;
};

/// The specification (CS-SAF-02). A (phase, input, guard) not listed changes nothing: the
/// phase stays and the action is NONE (a TICK still counts the period and fills the window).
inline constexpr std::array<Transition, 19> kTransitions{{
    {Phase::IDLE, Input::START, Guard::NONE, Phase::REST, Action::BEGIN},
    {Phase::IDLE, Input::TICK, Guard::NONE, Phase::IDLE, Action::PAUSE},

    {Phase::REST, Input::CANCEL, Guard::NONE, Phase::RESULT, Action::CANCEL},
    {Phase::REST, Input::TICK, Guard::TAKEN, Phase::DIRECTION, Action::NEXT},
    {Phase::REST, Input::TICK, Guard::TIMED_OUT, Phase::RESULT, Action::TIME_OUT},

    {Phase::DIRECTION, Input::CANCEL, Guard::NONE, Phase::RESULT, Action::CANCEL},
    {Phase::DIRECTION, Input::TICK, Guard::TAKEN, Phase::DIRECTION, Action::NEXT},
    {Phase::DIRECTION, Input::TICK, Guard::LAST_TAKEN, Phase::RELEASE, Action::NEXT},
    {Phase::DIRECTION, Input::TICK, Guard::TIMED_OUT, Phase::RESULT, Action::TIME_OUT},

    {Phase::RELEASE, Input::CANCEL, Guard::NONE, Phase::RESULT, Action::CANCEL},
    {Phase::RELEASE, Input::TICK, Guard::RELEASED, Phase::RESULT, Action::COMPLETE},
    {Phase::RELEASE, Input::TICK, Guard::RELEASED_BAD, Phase::RESULT, Action::REJECT},
    {Phase::RELEASE, Input::TICK, Guard::TIMED_OUT, Phase::RESULT, Action::TIME_OUT},

    {Phase::RESULT, Input::START, Guard::NONE, Phase::REST, Action::BEGIN},
    {Phase::RESULT, Input::TICK, Guard::RESULT_OVER, Phase::IDLE, Action::SHOW_IDLE},

    // Waiting is listed for the record; it changes nothing, as an unlisted row would.
    {Phase::REST, Input::TICK, Guard::WAITING, Phase::REST, Action::NONE},
    {Phase::DIRECTION, Input::TICK, Guard::WAITING, Phase::DIRECTION, Action::NONE},
    {Phase::RELEASE, Input::TICK, Guard::WAITING, Phase::RELEASE, Action::NONE},
    {Phase::RESULT, Input::TICK, Guard::WAITING, Phase::RESULT, Action::NONE},
}};

struct Decision {
  Phase to;
  Action action;
};

/// The transition function: a hand-written switch, checked against kTransitions by the
/// oracle test. Pure.
[[nodiscard]] Decision decide(Phase from, Input input, Guard guard) noexcept;

/// The run. Lives on the UI task (it is the view's model); not thread-safe.
class CalibrationRun {
public:
  struct Config {
    int settle_ticks = kSettleTicks;
    int step_timeout_ticks = kStepTimeoutTicks;
    int result_ticks = kResultTicks;
  };

  explicit CalibrationRun(const Config &config)
      : config_(config) {}

  /// Takes one input (a TICK with the latest sample), moves the run, and says what the view
  /// must do.
  [[nodiscard]] Action handle(Input input, const Sample &mv = {});

  Phase phase() const { return phase_; }
  /// Whether a run is going: REST, DIRECTION or RELEASE.
  bool running() const;
  /// Which end is asked for, in DIRECTION (index into kDirections).
  std::size_t direction() const { return direction_; }
  /// The step number, 1..8: REST 1, DIRECTION 2..7, any other phase 8.
  int step() const;
  /// step() as it was when the last handle() began: the step a TIME_OUT ended.
  int from_step() const { return from_step_; }
  /// "Step <n> of 8\n<what to do>": the prompt for the current step.
  std::string prompt() const;
  /// What has been measured so far; the whole record once COMPLETE or REJECT is returned.
  const Record &record() const { return record_; }

private:
  // The last kHoldTicks samples of all three axes.
  struct Window {
    std::array<Sample, kHoldTicks> samples{};
    std::size_t count = 0;
    std::size_t next = 0;

    void clear() { count = next = 0; }
    void push(const Sample &mv);
    bool full() const { return count == kHoldTicks; }
    float spread(std::size_t axis) const;
    float mean(std::size_t axis) const;
  };

  void begin_step(Phase phase);
  void fill_window(const Sample &mv);
  Guard tick_guard() const;
  void apply(Phase from, Decision decision);

  Config config_;
  Phase phase_ = Phase::IDLE;
  std::size_t direction_ = 0;
  int ticks_ = 0; // since the current step began
  int from_step_ = kStepCount;
  Window window_;
  Record record_{};
};

} // namespace hmi::cal
