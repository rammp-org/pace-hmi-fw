#pragma once
// The POST runner (hazard-c3-spec.md §2.3-2.7, REQ-POST-15..17): the facts, the latched report
// and the start time, ticked by the UI task's 250 ms poll before the drive adapter's tick. It
// is the POST gate's only writer (through its port).
//
// Pure: no ESP-IDF, FreeRTOS or LVGL. What it needs from the board comes through `Port`, which
// main fills (the IDF facts, the gate's write end, the serial log): every call is a direct
// call on the template argument.
//
// Interim (decision C6, a CS-SAF-04 deviation until the islands work): it runs on the UI task,
// not on a safety island. A stalled UI task leaves the gate PENDING, which holds the stick.

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "post/checks.hpp"
#include "post/facts.hpp"
#include "post/post.hpp"
#include "stick/permit_types.hpp"

namespace hmi::post {

using hmi::stick::PostGate;

/// Decision C4: a LATCHED check still PENDING this long after the runner's first tick fails
/// the POST ("timed out"). LIVE checks have no budget. Expected: about 1.3 s.
inline constexpr std::uint32_t POST_BUDGET_MS = 3000;

/// The runner's tick: the UI poll's period (hmi_ui UiPoll::PERIOD_MS, the drive table's
/// kTickPeriod; main static_asserts the three are equal).
inline constexpr std::uint32_t POST_TICK_MS = 250;

/// @brief The gate an overall state stores: PENDING, PASS and FAIL one to one (REQ-POST-15).
/// @param overall the latched overall state
/// @return its gate; a value outside the enum gives FAIL
[[nodiscard]] constexpr PostGate gate_of(Overall overall) noexcept {
  switch (overall) {
  case Overall::PENDING:
    return PostGate::PENDING;
  case Overall::PASS:
    return PostGate::PASS;
  case Overall::FAIL:
    return PostGate::FAIL;
  }
  return PostGate::FAIL;
}
static_assert(gate_of(Overall::PENDING) == PostGate::PENDING &&
                  gate_of(Overall::PASS) == PostGate::PASS &&
                  gate_of(Overall::FAIL) == PostGate::FAIL,
              "Overall maps onto PostGate one to one (POST-049)");

/// @brief A reset reason's name for the logs and the About screen: the self test's
///        `reset_reason_name` (main/selftest_names.cpp), kept in step by a parity test (POST-047).
/// @param reason the reason
/// @return its name; "other" for a reason the self test does not name
[[nodiscard]] std::string_view reset_reason_name(ResetReason reason) noexcept;

/// @brief A reset reason as ESP-IDF names it, without `ESP_RST_` (the bench's STATE line).
/// @param reason the reason
/// @return e.g. "SW", "PANIC", "TASK_WDT"; "?" for a value the enum does not name
[[nodiscard]] std::string_view reset_reason_id(ResetReason reason) noexcept;

/// One POST line, built in a fixed buffer (no allocation).
struct Line {
  std::array<char, 128> text{}; ///< NUL-terminated
  std::size_t len = 0;          ///< characters before the NUL
  /// @return the line, without its NUL
  [[nodiscard]] std::string_view view() const noexcept { return {text.data(), len}; }
};

/// @brief A check's TS-POST-05 line: `POST <name> <PASS|FAIL|SKIP> <value> <unit> [<lo>,<hi>]`
///        (the unit and its space are left out when the row has none). A LATCHED check still
///        PENDING prints FAIL (no measurement is a FAIL, TS-POST-03); a LIVE one prints SKIP.
/// @param row the check's row
/// @param result its result in the latched report
/// @return the line
[[nodiscard]] Line check_line(const Check &row, const Result &result) noexcept;
/// @brief `POST RESULT <PASS|FAIL> <passed>/<total> <ms>`.
/// @param report the latched report (overall PASS or FAIL)
/// @param ms milliseconds since the runner's first tick
/// @return the line
[[nodiscard]] Line result_line(const Report &report, std::uint32_t ms) noexcept;
/// @brief `POST reset reason: <name> (<esp_reset_reason value>)`.
/// @param reason the last reset's reason
/// @return the line
[[nodiscard]] Line reset_line(ResetReason reason) noexcept;
/// @brief `POST waiting: <name> <value> <unit>` (the unit and its space left out when none).
/// @param row the blocking LIVE check's row
/// @param result its result
/// @return the line
[[nodiscard]] Line waiting_line(const Check &row, const Result &result) noexcept;

/// The cause line when the POST fails by its budget with no window and the stick task
/// (Read ADC) is not running (owner, 2026-10-08): its own line, the TS-POST-05 lines unchanged.
inline constexpr std::string_view STICK_TASK_CAUSE_LINE = "POST cause: Read ADC not running";

/// @brief Whether a LATCHED check is still PENDING (what the budget fails).
/// @param report a report
/// @return true when some LATCHED row's verdict is PENDING
[[nodiscard]] bool latched_pending(const Report &report) noexcept;

/// What the runner needs from the board. Each call runs on the runner's task (the UI task).
template <class P>
concept PostPort = requires(P &port, PostGate gate, std::string_view line) {
  { port.reset_reason() } -> std::same_as<ResetReason>;
  { port.image() } -> std::same_as<ImageFacts>;
  { port.calibration() } -> std::same_as<Calibration>;
  { port.memory() } -> std::same_as<std::optional<MemoryFacts>>; // nothing: not measurable
  { port.stacks() } -> std::same_as<StackFacts>;
  { port.i2c() } -> std::same_as<I2cSet>;
  { port.stick_task_running() } -> std::same_as<bool>; // read only at the budget
  port.store_gate(gate);                               // release store of the POST gate
  port.print(line);                                    // one line on the serial log
};

/// @brief The POST runner. One instance, on the UI task.
/// @tparam Port What it needs from the board (PostPort).
template <class Port> class PostRunner {
  static_assert(PostPort<Port>, "PostRunner: Port must provide the PostPort calls");

public:
  /// @brief A runner that has not run: the gate is NOT_RUN until its first tick.
  /// @param port the board's side
  constexpr explicit PostRunner(Port port) noexcept
      : port_(port) {}

  /// @brief One UI tick (REQ-POST-15, REQ-POST-16, REQ-POST-17).
  /// @details The first tick stores PENDING and starts the budget. A new window is merged into
  ///          the latched report; the first window also gathers the boot facts, once, and logs
  ///          the reset reason. The gate is stored only when it changes, and never moves back
  ///          from PASS or FAIL. At PASS or FAIL the POST lines are printed once.
  /// @param now_ms the UI task's clock, ms (modulo 2^32)
  /// @param window a window read from the mailbox this tick (CHANGED), else nothing
  void tick(std::uint32_t now_ms, const std::optional<StickWindow> &window) noexcept {
    if (forced_) {
      return; // a bench override holds until the next rerun
    }
    if (!started_) {
      start(now_ms);
    }
    if (decided()) {
      return; // PASS and FAIL hold until the next reset
    }
    if (window) {
      take_window(*window);
    }
    latched_ = merge(latched_, evaluate(facts_));
    const std::uint32_t elapsed_ms = now_ms - start_ms_;
    if (latched_.overall == Overall::PENDING && elapsed_ms >= POST_BUDGET_MS &&
        latched_pending(latched_)) {
      latched_.overall = Overall::FAIL; // TS-POST-03: no measurement is a FAIL
      timed_out_ = true;
      // No window ever came: was the stick task started at all? Asked only now, at the
      // budget: Read ADC starts after the runner's first ticks (owner, 2026-10-08).
      stick_task_missing_ = !gathered_ && !port_.stick_task_running();
    }
    store(gate_of(next_overall(overall_, latched_.overall)));
    if (decided()) {
      print_lines(elapsed_ms);
    } else {
      note_waiting();
    }
  }

  /// @brief Bench only (`POST RERUN`): back to NOT_RUN, facts dropped; the next tick starts
  ///        again and gathers the boot facts again.
  void rerun() noexcept {
    forced_ = false;
    facts_ = Facts{};
    latched_ = Report{};
    overall_ = Overall::PENDING;
    started_ = false;
    gathered_ = false;
    timed_out_ = false;
    stick_task_missing_ = false;
    waiting_on_.reset();
    store(PostGate::NOT_RUN);
  }

  /// @brief Bench only (`PERMIT POST <value>`, C1 §3.2, applied by the runner, C3 §2.4): stores
  ///        @p gate and holds it; ticks change nothing until rerun().
  /// @param gate the gate the bench asks for
  void force(PostGate gate) noexcept {
    forced_ = true;
    if (gate != gate_) {
      gate_ = gate;
      port_.store_gate(gate);
    }
  }

  /// @return the gate as last stored
  [[nodiscard]] PostGate gate() const noexcept { return gate_; }
  /// @return the latched report
  [[nodiscard]] const Report &report() const noexcept { return latched_; }
  /// @return true when the POST failed by its budget
  [[nodiscard]] bool timed_out() const noexcept { return timed_out_; }
  /// @return true when it failed by its budget with no window and the stick task not running:
  ///         the cause of the adc.valid FAIL
  [[nodiscard]] bool stick_task_missing() const noexcept { return stick_task_missing_; }
  /// @return the facts gathered so far
  [[nodiscard]] const Facts &facts() const noexcept { return facts_; }
  /// @brief Time since the runner's first tick; 0 before it.
  /// @param now_ms the UI task's clock, ms
  /// @return milliseconds, modulo 2^32
  [[nodiscard]] std::uint32_t since_start_ms(std::uint32_t now_ms) const noexcept {
    return started_ ? now_ms - start_ms_ : 0U;
  }

private:
  void start(std::uint32_t now_ms) noexcept {
    started_ = true;
    start_ms_ = now_ms;
    latched_ = evaluate(Facts{});
    overall_ = Overall::PENDING;
    store(PostGate::PENDING);
  }

  [[nodiscard]] bool decided() const noexcept {
    return gate_ == PostGate::PASS || gate_ == PostGate::FAIL;
  }

  void take_window(const StickWindow &window) noexcept {
    facts_.window = window;
    if (gathered_) {
      return;
    }
    gathered_ = true;
    facts_.reset = port_.reset_reason();
    facts_.image = port_.image();
    facts_.cal = port_.calibration();
    facts_.memory = port_.memory();
    facts_.stacks = port_.stacks();
    facts_.i2c = port_.i2c();
    port_.print(reset_line(*facts_.reset).view());
  }

  void store(PostGate gate) noexcept {
    overall_ = latched_.overall;
    if (gate != gate_) {
      gate_ = gate;
      port_.store_gate(gate);
    }
  }

  void print_lines(std::uint32_t elapsed_ms) noexcept {
    if (!gathered_) { // no window ever came (a dead ADC): the reset reason is still logged
      port_.print(reset_line(port_.reset_reason()).view());
    }
    for (std::size_t i = 0; i < CHECK_COUNT; ++i) {
      port_.print(check_line(CHECKS[i], latched_.results[i]).view());
    }
    if (stick_task_missing_) {
      port_.print(STICK_TASK_CAUSE_LINE);
    }
    port_.print(result_line(latched_, elapsed_ms).view());
  }

  void note_waiting() noexcept {
    const std::optional<Id> blocking = blocking_check(latched_);
    if (blocking == waiting_on_) {
      return;
    }
    waiting_on_ = blocking;
    if (blocking && check(*blocking).kind == Kind::LIVE) {
      port_.print(waiting_line(check(*blocking), latched_.results[index_of(*blocking)]).view());
    }
  }

  Port port_;
  Facts facts_{};
  Report latched_{};
  Overall overall_ = Overall::PENDING;
  PostGate gate_ = PostGate::NOT_RUN;
  std::uint32_t start_ms_ = 0;
  bool started_ = false;
  bool gathered_ = false;
  bool timed_out_ = false;
  bool stick_task_missing_ = false; ///< the budget found no window and no Read ADC task
  bool forced_ = false;             ///< a bench override holds the gate
  std::optional<Id> waiting_on_;
};

} // namespace hmi::post
