// The POST indicator's state (indicator.hpp, REQ-POST-18): hazard-c3-spec.md §2.8's table as
// a switch on the gate.

#include "post/indicator.hpp"

namespace hmi::post {
namespace {

Indicator pending_indicator(const Report &report) {
  const std::optional<Id> blocking = blocking_check(report);
  if (blocking && check(*blocking).kind == Kind::LIVE) {
    return {.kind = IndicatorKind::WAITING,
            .colour = IndicatorColour::AMBER,
            .check = blocking,
            .timed_out = false};
  }
  return {.kind = IndicatorKind::CHECKING,
          .colour = IndicatorColour::GREY,
          .check = blocking,
          .timed_out = false};
}

} // namespace

Indicator post_indicator(hmi::stick::PostGate gate, const Report &report, bool timed_out,
                         std::uint32_t since_start_ms) noexcept {
  using hmi::stick::PostGate;
  switch (gate) {
  case PostGate::PASS:
    return {};
  case PostGate::PENDING:
    return pending_indicator(report);
  case PostGate::FAIL:
    return {.kind = IndicatorKind::FAILED,
            .colour = IndicatorColour::RED,
            .check = blocking_check(report),
            .timed_out = timed_out};
  case PostGate::NOT_RUN:
    if (since_start_ms < POST_BUDGET_MS) {
      return {.kind = IndicatorKind::CHECKING,
              .colour = IndicatorColour::GREY,
              .check = std::nullopt,
              .timed_out = false};
    }
    return {.kind = IndicatorKind::NOT_RUN,
            .colour = IndicatorColour::RED,
            .check = std::nullopt,
            .timed_out = false};
  }
  return {.kind = IndicatorKind::FAILED,
          .colour = IndicatorColour::RED,
          .check = std::nullopt,
          .timed_out = false};
}

} // namespace hmi::post
