// The calibration run model: see cal_run.hpp. Moved from main/joystick_cal.cpp (Window, Run,
// begin_step, next_direction, tick_rest, tick_direction, tick_release, the phase switch of
// tick_cb and the running checks of start/cancel) without a change in behaviour; the LVGL
// half stays in main as the view.

#include "cal_run.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>

#include "format.hpp"

namespace hmi::cal {

Decision decide(Phase from, Input input, Guard guard) noexcept {
  const Decision none{from, Action::NONE};
  switch (from) {
  case Phase::IDLE:
    if (input == Input::START && guard == Guard::NONE) {
      return {Phase::REST, Action::BEGIN};
    }
    if (input == Input::TICK && guard == Guard::NONE) {
      return {Phase::IDLE, Action::PAUSE};
    }
    return none;
  case Phase::REST:
  case Phase::DIRECTION:
  case Phase::RELEASE:
    if (input == Input::CANCEL && guard == Guard::NONE) {
      return {Phase::RESULT, Action::CANCEL};
    }
    if (input != Input::TICK) {
      return none;
    }
    switch (guard) {
    case Guard::TIMED_OUT:
      return {Phase::RESULT, Action::TIME_OUT};
    case Guard::TAKEN:
      return from == Phase::RELEASE ? none : Decision{Phase::DIRECTION, Action::NEXT};
    case Guard::LAST_TAKEN:
      return from == Phase::DIRECTION ? Decision{Phase::RELEASE, Action::NEXT} : none;
    case Guard::RELEASED:
      return from == Phase::RELEASE ? Decision{Phase::RESULT, Action::COMPLETE} : none;
    case Guard::RELEASED_BAD:
      return from == Phase::RELEASE ? Decision{Phase::RESULT, Action::REJECT} : none;
    case Guard::NONE:
    case Guard::WAITING:
    case Guard::RESULT_OVER:
      return none;
    }
    return none;
  case Phase::RESULT:
    if (input == Input::START && guard == Guard::NONE) {
      return {Phase::REST, Action::BEGIN};
    }
    if (input == Input::TICK && guard == Guard::RESULT_OVER) {
      return {Phase::IDLE, Action::SHOW_IDLE};
    }
    return none;
  }
  return none;
}

void CalibrationRun::Window::push(const Sample &mv) {
  samples[next] = mv;
  next = (next + 1) % kHoldTicks;
  count = std::min<std::size_t>(count + 1, kHoldTicks);
}

float CalibrationRun::Window::spread(std::size_t axis) const {
  float lo = samples[0][axis], hi = lo;
  for (std::size_t i = 1; i < count; ++i) {
    lo = std::min(lo, samples[i][axis]);
    hi = std::max(hi, samples[i][axis]);
  }
  return hi - lo;
}

float CalibrationRun::Window::mean(std::size_t axis) const {
  const float sum =
      std::accumulate(std::begin(samples), std::begin(samples) + static_cast<std::ptrdiff_t>(count),
                      0.0f, [axis](float acc, const Sample &s) { return acc + s[axis]; });
  return sum / static_cast<float>(count);
}

bool CalibrationRun::running() const {
  return phase_ == Phase::REST || phase_ == Phase::DIRECTION || phase_ == Phase::RELEASE;
}

int CalibrationRun::step() const {
  return phase_ == Phase::DIRECTION ? static_cast<int>(direction_) + 2
                                    : (phase_ == Phase::REST ? 1 : kStepCount);
}

std::string CalibrationRun::prompt() const {
  const char *text = "Let go of the joystick";
  if (phase_ == Phase::REST) {
    text = "Let go of the joystick and keep still";
  } else if (phase_ == Phase::DIRECTION) {
    text = kDirections[direction_].prompt;
  }
  return fmt::format("Step {} of {}\n{}", step(), kStepCount, text);
}

void CalibrationRun::begin_step(Phase phase) {
  phase_ = phase;
  ticks_ = 0;
  window_.clear();
}

// What each phase does with a period's sample before its guard is read.
void CalibrationRun::fill_window(const Sample &mv) {
  switch (phase_) {
  case Phase::REST:
    if (ticks_ > config_.settle_ticks) {
      window_.push(mv);
    }
    return;
  case Phase::DIRECTION: {
    const Direction &d = kDirections[direction_];
    if ((mv[d.axis] - record_[d.axis].center_mv) * d.sign < kFullTravelMv) {
      window_.clear(); // not there yet, or let go early: start the hold again
    } else {
      window_.push(mv);
    }
    return;
  }
  case Phase::RELEASE:
    for (std::size_t axis = 0; axis < kAxisCount; ++axis) {
      if (std::fabs(mv[axis] - record_[axis].center_mv) > kReleasedMv) {
        window_.clear();
        return;
      }
    }
    window_.push(mv);
    return;
  case Phase::IDLE:
  case Phase::RESULT:
    return;
  }
}

Guard CalibrationRun::tick_guard() const {
  bool taken = false;
  switch (phase_) {
  case Phase::IDLE:
    return Guard::NONE;
  case Phase::RESULT:
    return ticks_ >= config_.result_ticks ? Guard::RESULT_OVER : Guard::WAITING;
  case Phase::REST:
    taken = ticks_ > config_.settle_ticks && window_.full();
    for (std::size_t axis = 0; taken && axis < kAxisCount; ++axis) {
      taken = !(window_.spread(axis) > kSteadyMv[axis]); // not <=: a NaN spread counts as steady
    }
    if (taken) {
      return Guard::TAKEN;
    }
    break;
  case Phase::DIRECTION: {
    const std::size_t axis = kDirections[direction_].axis;
    if (window_.full() && !(window_.spread(axis) > kSteadyMv[axis])) { // as above, for NaN
      return direction_ + 1 < kDirections.size() ? Guard::TAKEN : Guard::LAST_TAKEN;
    }
    break;
  }
  case Phase::RELEASE:
    if (window_.full()) {
      return plausible(record_) ? Guard::RELEASED : Guard::RELEASED_BAD;
    }
    break;
  }
  return ticks_ > config_.step_timeout_ticks ? Guard::TIMED_OUT : Guard::WAITING;
}

void CalibrationRun::apply(Phase from, Decision decision) {
  switch (decision.action) {
  case Action::NONE:
  case Action::PAUSE:
    return;
  case Action::BEGIN:
    direction_ = 0;
    record_ = Record{};
    begin_step(Phase::REST);
    return;
  case Action::NEXT:
    if (from == Phase::REST) {
      for (std::size_t axis = 0; axis < kAxisCount; ++axis) {
        record_[axis].center_mv = window_.mean(axis);
      }
      direction_ = 0;
    } else {
      const Direction &d = kDirections[direction_];
      (d.sign > 0 ? record_[d.axis].max_mv : record_[d.axis].min_mv) = window_.mean(d.axis);
      if (decision.to == Phase::DIRECTION) {
        ++direction_;
      }
    }
    begin_step(decision.to);
    return;
  case Action::COMPLETE:
  case Action::REJECT:
  case Action::TIME_OUT:
  case Action::CANCEL:
    begin_step(Phase::RESULT);
    return;
  case Action::SHOW_IDLE:
    phase_ = Phase::IDLE; // the period count runs on, as it always has
    return;
  }
}

Action CalibrationRun::handle(Input input, const Sample &mv) {
  const Phase from = phase_;
  from_step_ = step();
  Guard guard = Guard::NONE;
  if (input == Input::TICK) {
    ++ticks_;
    fill_window(mv);
    guard = tick_guard();
  }
  const Decision decision = decide(from, input, guard);
  apply(from, decision);
  return decision.action;
}

} // namespace hmi::cal
