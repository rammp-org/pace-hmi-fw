#pragma once
// The POST's persistent fault indicator (hazard-c3-spec.md §2.8, REQ-POST-18): what the TopBar
// shows for the POST gate, on every screen but Boot. Not a banner: it does not time out and
// cannot be dismissed. Pure: the gate, the latched report and the time since the POST started
// in, the indicator out; the TopBar view draws it and hmi_rtps_spec holds its words.

#include <cstdint>
#include <optional>

#include "post/checks.hpp"
#include "post/post.hpp"
#include "post/runner.hpp"
#include "stick/permit_types.hpp"

namespace hmi::post {

/// What the indicator shows.
enum class IndicatorKind : std::uint8_t {
  NONE,     ///< POST passed: nothing
  CHECKING, ///< "Start-up check": a latched check is still gathering (grey)
  WAITING,  ///< a live check (the stick at rest) is not met yet: its text (amber)
  FAILED,   ///< "Start-up check failed: <text>" and what to do (red)
  NOT_RUN,  ///< "Start-up check not run", from the budget on (red)
};

/// Its colour.
enum class IndicatorColour : std::uint8_t {
  NONE,  ///< not shown
  GREY,  ///< checking
  AMBER, ///< waiting on the user
  RED,   ///< failed, or not run
};

/// The indicator: kind, colour, the check it names (if any) and whether the POST failed by its
/// budget (the text then reads "Start-up check timed out:").
struct Indicator {
  IndicatorKind kind = IndicatorKind::NONE;
  IndicatorColour colour = IndicatorColour::NONE;
  std::optional<Id> check;
  bool timed_out = false;
};

/// @brief The indicator for a gate and its report (§2.8's table).
/// @param gate the POST gate as loaded
/// @param report the runner's latched report
/// @param timed_out the runner failed the POST by its budget
/// @param since_start_ms for NOT_RUN: how long the gate has been NOT_RUN (from boot, or from a
///        bench rerun); CHECKING before POST_BUDGET_MS, NOT_RUN from it
/// @return what the TopBar shows; a gate outside the enum shows FAILED, red, naming no check
[[nodiscard]] Indicator post_indicator(hmi::stick::PostGate gate, const Report &report,
                                       bool timed_out, std::uint32_t since_start_ms) noexcept;

} // namespace hmi::post
