#pragma once
// The quick POST evaluator: facts in, a verdict per check and an overall state out
// (README, REQ-POST-01..13). Pure: no ESP-IDF, no FreeRTOS, no LVGL, no allocation, no logging.
// Not wired into the firmware yet: that is hazard-fixes C3 (README, "Not yet wired").

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "post/checks.hpp"
#include "post/facts.hpp"

namespace hmi::post {

/// One check's verdict.
enum class Verdict : uint8_t {
  PENDING, ///< not judged yet: its facts are missing or too few
  PASS,    ///< the measurement is inside the row's limits
  FAIL,    ///< it is not, or the facts contradict themselves
};

/// Why a check has its verdict.
enum class Reason : uint8_t {
  OK,                 ///< PASS
  NOT_MEASURED,       ///< PENDING: the facts it needs are not in yet
  TOO_FEW_SAMPLES,    ///< PENDING: the window is shorter than WINDOW_MIN_SAMPLES
  BELOW_MIN,          ///< FAIL: value < lo
  ABOVE_MAX,          ///< FAIL: value > hi
  BAD_FACTS,          ///< FAIL: the facts contradict themselves (e.g. min > max)
  NOT_SAVED,          ///< FAIL: no calibration saved in flash
  DEVICE_MISSING,     ///< FAIL: an expected I2C device did not answer
  UNCLEAN_RESET,      ///< FAIL: panic, watchdog, brownout, glitch, lock-up or efuse reset
  UNKNOWN_RESET,      ///< FAIL: the reset reason is unknown, or not one ESP-IDF v6.0 names
  IMAGE_NOT_VERIFIED, ///< FAIL: the image's own SHA-256 check did not pass
  IMAGE_STATE,        ///< FAIL: the running image is INVALID, ABORTED or NEW
  BUTTON_PRESSED,     ///< FAIL: the stick button was down during the window
};

/// The POST as a whole.
enum class Overall : uint8_t {
  PENDING, ///< no latched FAIL yet, and something is not PASS yet
  PASS,    ///< every required check passed
  FAIL,    ///< a required latched check failed
};

/// One check's outcome.
struct Result {
  Id id = Id::COUNT;
  Verdict verdict = Verdict::PENDING;
  Reason reason = Reason::NOT_MEASURED;
  int32_t value = 0; ///< the measurement, in the row's unit; 0 while PENDING
};

/// Every check's outcome, in CHECKS' order, and the overall state.
struct Report {
  std::array<Result, CHECK_COUNT> results{};
  Overall overall = Overall::PENDING;
};

/// One row of the overall state's transition table (CS-SAF-02).
struct OverallTransition {
  Overall from;      ///< the latched overall state
  Overall evaluated; ///< what the newest merged report says
  Overall to;        ///< the new latched overall state
};

/// The overall state machine: PENDING follows the evaluation; PASS and FAIL never change
/// until the next reset (REQ-POST-10). Every (from, evaluated) pair is listed.
inline constexpr std::array OVERALL_TRANSITIONS{
    OverallTransition{Overall::PENDING, Overall::PENDING, Overall::PENDING},
    OverallTransition{Overall::PENDING, Overall::PASS, Overall::PASS},
    OverallTransition{Overall::PENDING, Overall::FAIL, Overall::FAIL},
    OverallTransition{Overall::PASS, Overall::PENDING, Overall::PASS},
    OverallTransition{Overall::PASS, Overall::PASS, Overall::PASS},
    OverallTransition{Overall::PASS, Overall::FAIL, Overall::PASS},
    OverallTransition{Overall::FAIL, Overall::PENDING, Overall::FAIL},
    OverallTransition{Overall::FAIL, Overall::PASS, Overall::FAIL},
    OverallTransition{Overall::FAIL, Overall::FAIL, Overall::FAIL},
};

/// @brief The table lists each (from, evaluated) pair of the three states exactly once.
/// @return true when the table is complete and has no duplicate
consteval bool overall_table_complete() {
  constexpr std::array<Overall, 3> states{Overall::PENDING, Overall::PASS, Overall::FAIL};
  const auto listed_once = [](Overall from, Overall evaluated) {
    return std::count_if(OVERALL_TRANSITIONS.begin(), OVERALL_TRANSITIONS.end(),
                         [&](const OverallTransition &t) {
                           return t.from == from && t.evaluated == evaluated;
                         }) == 1;
  };
  return OVERALL_TRANSITIONS.size() == states.size() * states.size() &&
         std::all_of(states.begin(), states.end(), [&](Overall from) {
           return std::all_of(states.begin(), states.end(),
                              [&](Overall evaluated) { return listed_once(from, evaluated); });
         });
}
static_assert(overall_table_complete(), "OVERALL_TRANSITIONS must list every pair exactly once");

/// @brief PASS and FAIL are absorbing: no row leaves them.
/// @return true when every row from PASS or FAIL stays there
consteval bool overall_ends_absorb() {
  return std::all_of(
      OVERALL_TRANSITIONS.begin(), OVERALL_TRANSITIONS.end(),
      [](const OverallTransition &t) { return t.from == Overall::PENDING || t.to == t.from; });
}
static_assert(overall_ends_absorb(), "a POST PASS or FAIL must hold until the next reset");

/// @brief Judges one check on the facts (REQ-POST-02..08).
/// @param id the check; COUNT (or any value past it) gives FAIL BAD_FACTS
/// @param facts what was gathered so far
/// @return its verdict, reason and measured value
[[nodiscard]] Result evaluate_check(Id id, const Facts &facts) noexcept;

/// @brief Judges every check on the facts, statelessly, and derives the overall verdict.
/// @param facts what was gathered so far; Facts{} gives every check PENDING
/// @return the report
[[nodiscard]] Report evaluate(const Facts &facts) noexcept;

/// @brief The overall verdict of a set of results against their table (REQ-POST-09).
/// FAIL when a REQUIRED LATCHED check failed; else PENDING when a REQUIRED check is not PASS
/// (a LIVE FAIL only delays); else PASS. OPTIONAL checks never count. An empty table, or
/// results that do not line up with the table (count or id), give FAIL.
/// @param table the check rows
/// @param results one per row, in the same order
/// @return the overall verdict
[[nodiscard]] Overall overall_of(std::span<const Check> table,
                                 std::span<const Result> results) noexcept;

/// @brief The overall state machine's step: OVERALL_TRANSITIONS, as code (REQ-POST-10).
/// @param from the latched overall state
/// @param evaluated what the newest merged report says
/// @return the new latched overall state; a value outside the enum gives FAIL
[[nodiscard]] Overall next_overall(Overall from, Overall evaluated) noexcept;

/// @brief Folds a newer evaluation into the latched report (REQ-POST-10, REQ-POST-11).
/// A LATCHED check keeps a PASS or FAIL it already has; a LIVE check always takes the newer
/// result; once the overall state is PASS or FAIL the previous report is returned unchanged.
/// @param previous the latched report (start from evaluate(Facts{}))
/// @param now evaluate() on the newest facts
/// @return the new latched report
[[nodiscard]] Report merge(const Report &previous, const Report &now) noexcept;

/// @brief The check to show the user while the POST is not PASS (REQ-POST-12).
/// @param report a report
/// @return the first REQUIRED check that FAILed, else the first REQUIRED one PENDING, in
/// table order; nullopt when every required check passed
[[nodiscard]] std::optional<Id> blocking_check(const Report &report) noexcept;

/// @brief A verdict's name, as the POST line prints it (TS-POST-05).
/// @param verdict a verdict
/// @return "PENDING", "PASS" or "FAIL"; "?" for a value outside the enum
[[nodiscard]] std::string_view to_string(Verdict verdict) noexcept;
/// @brief An overall state's name.
/// @param overall a state
/// @return "PENDING", "PASS" or "FAIL"; "?" for a value outside the enum
[[nodiscard]] std::string_view to_string(Overall overall) noexcept;
/// @brief A reason's name, for logs.
/// @param reason a reason
/// @return its enumerator's name in lower case; "?" for a value outside the enum
[[nodiscard]] std::string_view to_string(Reason reason) noexcept;

} // namespace hmi::post
