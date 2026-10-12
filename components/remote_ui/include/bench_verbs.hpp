#pragma once

/// @file bench_verbs.hpp
/// @brief The hazard fixes' bench verbs: STATE, PERMIT, CAL UNSAVED, POST RERUN, CRASH, STALL
///        and SEATFAIL. Their parser, the STATE line, and the hooks the firmware fills.
/// @details BENCH ONLY (hazard-decisions.md H1; REQ-RUI-08, REQ-RUI-06, REQ-RUI-07,
///          REQ-RUI-09). The remote
///          UI (remote_ui.cpp) answers these verbs only in a CONFIG_HMI_BENCH_STICK_INJECT build;
///          any other build answers ERR, and a release build compiles no remote UI at all, so
///          a release ELF has none of this (the `bench_?verb` symbol check in build.yml).
///
///          The remote UI is only an adapter here. Every value STATE reports and every change a
///          verb asks for comes through a hook in `Hooks`, which app_main fills from the
///          interfaces the hazard specs define (C1 §3.2 and §6, C3 §2.4 and REQ-RUI-06, C4
///          REQ-RUI-07, C2 REQ-RUI-08) and hands over once with remote_ui_attach_bench_verbs,
///          before remote_ui_start. A hook left empty makes its STATE field `null` and its verb
///          answer `ERR <verb> not wired`, so a bench script can tell "not in this firmware
///          yet" from a wrong value.
///
///          Threads: every hook runs on the remote UI's server task, never under lvgl_mutex.
///          A hook reads atomics or posts a request to the owning task (C3 §2.4: PERMIT POST is
///          applied by the POST runner); it never touches LVGL and never blocks for long. STALL
///          too: its hook posts the request and answers at once; the stalled task spins for
///          the requested time at its next cycle (C4 B5i/B5j).
///
///          The STATE line: "OK " and one JSON object, keys in this order, each `null` when
///          its hook is empty (names are the specs' enumerators):
///          - screen: the active screen's name, as SCREEN (C1 6)
///          - phase: LOCKED, ASKING, UNLOCKING, DRIVING, EXITING, EXIT_REFUSED (C1 6)
///          - notice: the Drive notice: NONE, STOPPING, MCB_DID_NOT_STOP or a hold reason (C1 2.7)
///          - banner: the refusal banner showing, by its name (REFUSED_POST, STICK_FAULT,
///            DRIVE_STOPPED, ...), or NONE (owner, 2026-10-08: graded on the bench)
///          - hold_reason: the stick's hold reason, C1 3.3's names
///          - calibrating, menu_open: booleans (C1 6)
///          - cal: {"h", "v", "twist"}, each [min, centre, max] mV in use (C1 6)
///          - post: the POST gate, NOT_RUN, PENDING, PASS, FAIL (C3 REQ-RUI-06)
///          - post_check: the blocking check's name (e.g. joy.y_cal_off), or null (C3)
///          - indicator: NONE, CHECKING, WAITING, FAILED, NOT_RUN; indicator_text (C3 2.8)
///          - reset_reason: esp_reset_reason_t without ESP_RST_: SW, PANIC, TASK_WDT... (C3)
///          - stick: the stick monitor, see StickBlock (C2 REQ-RUI-08)
///          - seat, seat_sent, seat_refused, seat_reason, seat_publish_failures: the seat path
///            (hazard-seat-spec.md REQ-RUI-09), see SeatBlock. Only in a firmware that wires
///            the seat hook: these five keys are left out, not null, without it, so the line of
///            a firmware before the seat fix keeps its exact form.
///
///          `SEATFAIL <n>` (REQ-RUI-09): the next n SeatCommand publishes return false (n 0..100;
///          0 cancels what is left).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::bench_verbs {

/// @brief What a bench verb line asks for.
enum class Verb : std::uint8_t {
  STATE,       ///< `STATE`: one JSON line (C1 §6, C3, C2)
  PERMIT_POST, ///< `PERMIT POST not_run|pending|pass|fail` (C1 §3.2; C3: applied by the runner)
  CAL_UNSAVED, ///< `CAL UNSAVED`: RAM only, joystick_cal_measured() false until reboot (C1 §6)
  POST_RERUN,  ///< `POST RERUN`: the POST runner restarts from NOT_RUN (C3 REQ-RUI-06)
  CRASH,       ///< `CRASH`: abort, a PANIC reset (C3 REQ-RUI-06)
  STALL,       ///< `STALL UI|ADC|CADC <ms>` (C4 REQ-RUI-07, C2 O14)
  SEATFAIL,    ///< `SEATFAIL <n>`: the next n seat publishes fail (seat REQ-RUI-09)
};

/// @brief PERMIT POST's values: C1 §3.2's PostGate, in its order.
enum class PostGateName : std::uint8_t { NOT_RUN, PENDING, PASS, FAIL };
/// @brief What STALL stalls: the UI task (a spin before its heartbeat), the ADC task (a spin
///        after its cycle) or espp's ContinuousAdc task (suspended by the ADC task, C2 O14).
enum class StallTarget : std::uint8_t { UI, ADC, CADC };

/// @brief The longest STALL accepted, ms. C4 asks for 3000 (a watchdog trip at 2 s); C2 for 1000.
inline constexpr std::uint32_t kStallMaxMs = 10000;
/// @brief The most seat publishes one SEATFAIL fails (the seat bench asks for 1).
inline constexpr std::uint32_t kSeatFailMax = 100;

/// @brief One parsed bench verb.
struct Request {
  Verb verb = Verb::STATE;
  PostGateName post = PostGateName::NOT_RUN; ///< PERMIT_POST only
  StallTarget stall = StallTarget::UI;       ///< STALL only
  std::uint32_t stall_ms = 0;                ///< STALL only, 1..kStallMaxMs
  std::uint32_t seat_fail = 0;               ///< SEATFAIL only, 0..kSeatFailMax
};

/// @brief What parse() made of a line.
struct Parsed {
  bool bench_verb = false;        ///< the first word is one of the bench verbs
  std::optional<Request> request; ///< set when the line is well formed
  std::string_view usage;         ///< when bench_verb and not well formed: the usage text
};

/// @brief The calibration in use, one axis, mV (rounded).
struct CalAxis {
  std::int32_t min_mv = 0;
  std::int32_t centre_mv = 0;
  std::int32_t max_mv = 0;
};
/// @brief Horizontal, vertical, twist (the calibration record's order).
using CalRecord = std::array<CalAxis, 3>;

/// @brief C2's implausible kinds, in its §2.2 order (P1..P6).
inline constexpr std::array<std::string_view, 6> kImplausibleKinds{
    "MISSING", "NAN", "NEGATIVE", "HIGH", "STALE", "TWIST_PARTIAL"};

/// @brief The stick monitor as STATE reports it (C2 REQ-RUI-08: state, reason, fault reason,
///        counters, xy_age_max_ms; C2 M-1: the maxima; C2-11: the stick's key).
struct StickBlock {
  std::string state;        ///< INIT, OK, SUSPECT, RECOVERING, FAULT, CALIBRATING
  std::string health;       ///< NOT_MONITORED, OK, CHECK, FAULT
  std::string reason;       ///< the last implausible kind, or NONE
  std::string reason_axis;  ///< H, V, TWIST, or "" when the reason names no axis
  std::string fault_reason; ///< the latched reason (a kind, NO_SAMPLES, INTERNAL) or NONE
  std::string fault_axis;   ///< its axis: H, V, TWIST, or "" (C2-3: "HIGH horizontal")
  std::uint32_t suspect_onsets = 0;
  std::uint32_t faults = 0;
  std::array<std::uint32_t, kImplausibleKinds.size()> bad{}; ///< per kind, kImplausibleKinds order
  std::uint32_t xy_age_max_ms = 0;
  std::array<std::int32_t, 3> max_mv{}; ///< largest window/oneshot maximum seen, h v twist
  std::int32_t joy_key = 0;             ///< the stick's key as the keypad sees it (0 = none)
};

/// @brief The seat path as STATE reports it (hazard-seat-spec.md REQ-RUI-09).
struct SeatBlock {
  /// Per seat axis, in RAMMP_SEAT_AXIS_TABLE's order: the raw value the HMI holds, or none
  /// while it is unknown ("--" on screen).
  std::vector<std::optional<std::int32_t>> axes;
  std::uint32_t sent = 0;             ///< SeatCommands published (the publish returned true)
  std::uint32_t refused = 0;          ///< presses the seat gate refused
  std::string reason;                 ///< the last press' outcome: a SeatReason name, or NONE
  std::uint32_t publish_failures = 0; ///< publishes that returned false
};

/// @brief Everything STATE reports. An empty optional is `null` on the line.
struct State {
  std::optional<std::string> screen;
  std::optional<std::string> phase;
  std::optional<std::string> notice;
  std::optional<std::string> banner;
  std::optional<std::string> hold_reason;
  std::optional<bool> calibrating;
  std::optional<bool> menu_open;
  std::optional<CalRecord> cal;
  std::optional<std::string> post;
  std::optional<std::string> post_check; ///< also null when the gate names no check
  std::optional<std::string> indicator;
  std::optional<std::string> indicator_text;
  std::optional<std::string> reset_reason;
  std::optional<StickBlock> stick;
  std::optional<SeatBlock> seat; ///< left out of the line when empty (see the file comment)
};

/// @brief The firmware's side of the verbs. Each is filled by the lane that owns the value
///        (see the file comment); an empty one is reported, never guessed.
struct Hooks {
  // STATE fields
  std::function<std::string()> phase;                     ///< C1: the drive session's phase
  std::function<std::string()> notice;                    ///< C1: the adapter's last Drive notice
  std::function<std::string()> banner;                    ///< the refusal banner up, or NONE
  std::function<std::string()> hold_reason;               ///< C1: OutputPermit's hold-reason atomic
  std::function<bool()> calibrating;                      ///< C1: joystick_cal_running()
  std::function<bool()> menu_open;                        ///< C1: the burger menu
  std::function<CalRecord()> cal;                         ///< C1: joystick_cal_current()
  std::function<std::string()> post;                      ///< C3: the POST gate atomic
  std::function<std::optional<std::string>()> post_check; ///< C3: the blocking check
  std::function<std::string()> indicator;                 ///< C3: post_indicator()'s kind
  std::function<std::string()> indicator_text;            ///< C3: its text
  std::function<std::string()> reset_reason;              ///< C3: esp_reset_reason() at boot
  std::function<StickBlock()> stick;                      ///< C2: the monitor's atomics
  std::function<SeatBlock()> seat;                        ///< seat REQ-RUI-09: the seat path
  // verbs; each returns false when the firmware refused the request
  std::function<bool(PostGateName)> permit_post;         ///< C1 §3.2 / C3 §2.4 (via the runner)
  std::function<bool()> cal_unsaved;                     ///< C1 §6
  std::function<bool()> post_rerun;                      ///< C3 REQ-RUI-06
  std::function<bool(StallTarget, std::uint32_t)> stall; ///< C4 REQ-RUI-07, C2 O14; posts
  std::function<bool(std::uint32_t)> seat_fail;          ///< seat REQ-RUI-09: `SEATFAIL <n>`
};

namespace detail {

inline constexpr std::string_view kStateUsage = "usage: STATE";
inline constexpr std::string_view kPermitUsage = "usage: PERMIT POST not_run|pending|pass|fail";
inline constexpr std::string_view kCalUsage = "usage: CAL UNSAVED";
inline constexpr std::string_view kPostUsage = "usage: POST RERUN";
inline constexpr std::string_view kCrashUsage = "usage: CRASH";
inline constexpr std::string_view kStallUsage = "usage: STALL UI|ADC|CADC <ms> (ms 1..10000)";
/// SEATFAIL's usage text.
inline constexpr std::string_view kSeatFailUsage = "usage: SEATFAIL <n> (n 0..100)";

/// Splits on single or repeated spaces; at most N words, the rest counted in `extra`.
template <std::size_t N> struct Words {
  std::array<std::string_view, N> word{};
  std::size_t count = 0;
  bool extra = false;
};

template <std::size_t N> [[nodiscard]] constexpr Words<N> split(std::string_view line) {
  Words<N> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && line[i] == ' ') {
      ++i;
    }
    const std::size_t start = i;
    while (i < line.size() && line[i] != ' ') {
      ++i;
    }
    if (i == start) {
      break;
    }
    if (out.count == N) {
      out.extra = true;
      break;
    }
    out.word[out.count++] = line.substr(start, i - start);
  }
  return out;
}

/// Decimal digits only, 1..kStallMaxMs.
[[nodiscard]] constexpr std::optional<std::uint32_t> stall_ms(std::string_view text) {
  if (text.empty() || text.size() > 5) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<std::uint32_t>(c - '0');
  }
  if (value == 0U || value > kStallMaxMs) {
    return std::nullopt;
  }
  return value;
}

/// @brief SEATFAIL's count: decimal digits only, 0..kSeatFailMax.
/// @param text The word after SEATFAIL.
/// @return The count, or nullopt when the word is not one.
[[nodiscard]] constexpr std::optional<std::uint32_t> seat_fail_count(std::string_view text) {
  if (text.empty() || text.size() > 3) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<std::uint32_t>(c - '0');
  }
  if (value > kSeatFailMax) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] constexpr std::optional<PostGateName> post_name(std::string_view text) {
  if (text == "not_run") {
    return PostGateName::NOT_RUN;
  }
  if (text == "pending") {
    return PostGateName::PENDING;
  }
  if (text == "pass") {
    return PostGateName::PASS;
  }
  if (text == "fail") {
    return PostGateName::FAIL;
  }
  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<StallTarget> stall_target(std::string_view text) {
  if (text == "UI") {
    return StallTarget::UI;
  }
  if (text == "ADC") {
    return StallTarget::ADC;
  }
  if (text == "CADC") {
    return StallTarget::CADC;
  }
  return std::nullopt;
}

[[nodiscard]] constexpr Parsed usage(std::string_view text) {
  return Parsed{.bench_verb = true, .request = std::nullopt, .usage = text};
}

[[nodiscard]] constexpr Parsed ok(const Request &request) {
  return Parsed{.bench_verb = true, .request = request, .usage = {}};
}

[[nodiscard]] constexpr Parsed parse_permit(const Words<3> &w) {
  if (w.count != 3 || w.extra) {
    return usage(kPermitUsage);
  }
  if (w.word[1] == "POST") {
    const auto value = post_name(w.word[2]);
    return value ? ok(Request{.verb = Verb::PERMIT_POST, .post = *value}) : usage(kPermitUsage);
  }
  return usage(kPermitUsage);
}

[[nodiscard]] constexpr Parsed parse_stall(const Words<3> &w) {
  if (w.count != 3 || w.extra) {
    return usage(kStallUsage);
  }
  const auto target = stall_target(w.word[1]);
  const auto ms = stall_ms(w.word[2]);
  if (!target || !ms) {
    return usage(kStallUsage);
  }
  return ok(Request{.verb = Verb::STALL, .stall = *target, .stall_ms = *ms});
}

/// @brief `SEATFAIL <n>`.
/// @param w The line's words.
/// @return The request, or SEATFAIL's usage.
[[nodiscard]] constexpr Parsed parse_seat_fail(const Words<3> &w) {
  if (w.count != 2 || w.extra) {
    return usage(kSeatFailUsage);
  }
  const auto n = seat_fail_count(w.word[1]);
  return n ? ok(Request{.verb = Verb::SEATFAIL, .seat_fail = *n}) : usage(kSeatFailUsage);
}

/// A verb of exactly two words, `first second` (CAL UNSAVED, POST RERUN).
[[nodiscard]] constexpr Parsed parse_pair(const Words<3> &w, std::string_view second, Verb verb,
                                          std::string_view text) {
  if (w.count != 2 || w.extra || w.word[1] != second) {
    return usage(text);
  }
  return ok(Request{.verb = verb});
}

/// A verb of one word (STATE, CRASH).
[[nodiscard]] constexpr Parsed parse_single(const Words<3> &w, Verb verb, std::string_view text) {
  if (w.count != 1) {
    return usage(text);
  }
  return ok(Request{.verb = verb});
}

/// JSON string body: quotes, backslashes and control characters escaped.
inline void put_string(std::string &out, std::string_view text) {
  out += '"';
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20U) {
      constexpr std::string_view kHex = "0123456789abcdef";
      const auto u = static_cast<unsigned char>(c);
      out += "\\u00";
      out += kHex[static_cast<std::size_t>(u >> 4U)];
      out += kHex[static_cast<std::size_t>(u & 0x0FU)];
    } else {
      out += c;
    }
  }
  out += '"';
}

inline void put_key(std::string &out, std::string_view key) {
  if (out.back() != '{') {
    out += ',';
  }
  put_string(out, key);
  out += ':';
}

inline void put_opt(std::string &out, std::string_view key, const std::optional<std::string> &v) {
  put_key(out, key);
  if (v) {
    put_string(out, *v);
  } else {
    out += "null";
  }
}

inline void put_opt(std::string &out, std::string_view key, const std::optional<bool> &v) {
  put_key(out, key);
  out += !v ? "null" : (*v ? "true" : "false");
}

inline void put_triple(std::string &out, std::string_view key, std::int32_t a, std::int32_t b,
                       std::int32_t c) {
  put_key(out, key);
  out += '[' + std::to_string(a) + ',' + std::to_string(b) + ',' + std::to_string(c) + ']';
}

inline void put_cal(std::string &out, const std::optional<CalRecord> &cal) {
  put_key(out, "cal");
  if (!cal) {
    out += "null";
    return;
  }
  out += '{';
  constexpr std::array<std::string_view, 3> kAxes{"h", "v", "twist"};
  for (std::size_t i = 0; i < kAxes.size(); ++i) {
    const CalAxis &axis = (*cal)[i];
    put_triple(out, kAxes[i], axis.min_mv, axis.centre_mv, axis.max_mv);
  }
  out += '}';
}

inline void put_stick(std::string &out, const std::optional<StickBlock> &stick) {
  put_key(out, "stick");
  if (!stick) {
    out += "null";
    return;
  }
  const StickBlock &s = *stick;
  out += '{';
  put_opt(out, "state", std::optional<std::string>{s.state});
  put_opt(out, "health", std::optional<std::string>{s.health});
  put_opt(out, "reason", std::optional<std::string>{s.reason});
  put_opt(out, "reason_axis", std::optional<std::string>{s.reason_axis});
  put_opt(out, "fault_reason", std::optional<std::string>{s.fault_reason});
  put_opt(out, "fault_axis", std::optional<std::string>{s.fault_axis});
  put_key(out, "suspect_onsets");
  out += std::to_string(s.suspect_onsets);
  put_key(out, "faults");
  out += std::to_string(s.faults);
  put_key(out, "bad");
  out += '{';
  for (std::size_t i = 0; i < kImplausibleKinds.size(); ++i) {
    put_key(out, kImplausibleKinds[i]);
    out += std::to_string(s.bad[i]);
  }
  out += '}';
  put_key(out, "xy_age_max_ms");
  out += std::to_string(s.xy_age_max_ms);
  put_triple(out, "max_mv", s.max_mv[0], s.max_mv[1], s.max_mv[2]);
  put_key(out, "joy_key");
  out += std::to_string(s.joy_key);
  out += '}';
}

/// @brief A key and its count.
/// @param out The line so far.
/// @param key The key.
/// @param value The count.
inline void put_count(std::string &out, std::string_view key, std::uint32_t value) {
  put_key(out, key);
  out += std::to_string(value);
}

/// @brief The seat keys, top level after `stick`; nothing at all when the hook is not wired.
/// @param out The line so far.
/// @param seat The seat block, or nullopt.
inline void put_seat(std::string &out, const std::optional<SeatBlock> &seat) {
  if (!seat) {
    return;
  }
  put_key(out, "seat");
  out += '[';
  for (std::size_t i = 0; i < seat->axes.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    out += seat->axes[i] ? std::to_string(*seat->axes[i]) : std::string("null");
  }
  out += ']';
  put_count(out, "seat_sent", seat->sent);
  put_count(out, "seat_refused", seat->refused);
  put_opt(out, "seat_reason", std::optional<std::string>{seat->reason});
  put_count(out, "seat_publish_failures", seat->publish_failures);
}

template <typename T, typename Fn> [[nodiscard]] std::optional<T> call(const Fn &fn) {
  if (!fn) {
    return std::nullopt;
  }
  return std::optional<T>{fn()};
}

/// "OK", or "ERR <what> refused" / "ERR <what> not wired in this firmware (<spec>)".
[[nodiscard]] inline std::string verdict(std::optional<bool> applied, std::string_view what,
                                         std::string_view spec) {
  if (!applied) {
    return "ERR " + std::string(what) + " not wired in this firmware (" + std::string(spec) + ")";
  }
  return *applied ? std::string("OK") : "ERR " + std::string(what) + " refused";
}

} // namespace detail

/// @brief Parses one remote-UI line. `bench_verb` is false when the first word is none of
///        STATE, PERMIT, CAL, POST, CRASH, STALL, SEATFAIL (the remote UI then tries its own
///        verbs).
///        Verbs and their keywords are upper case, values lower case, as the specs write them.
/// @param line One line, without its newline.
/// @return The request, or the usage text when the verb is known and the line is not.
[[nodiscard]] constexpr Parsed parse(std::string_view line) {
  const auto w = detail::split<3>(line);
  if (w.count == 0) {
    return Parsed{};
  }
  const std::string_view verb = w.word[0];
  if (verb == "STATE") {
    return detail::parse_single(w, Verb::STATE, detail::kStateUsage);
  }
  if (verb == "PERMIT") {
    return detail::parse_permit(w);
  }
  if (verb == "CAL") {
    return detail::parse_pair(w, "UNSAVED", Verb::CAL_UNSAVED, detail::kCalUsage);
  }
  if (verb == "POST") {
    return detail::parse_pair(w, "RERUN", Verb::POST_RERUN, detail::kPostUsage);
  }
  if (verb == "CRASH") {
    return detail::parse_single(w, Verb::CRASH, detail::kCrashUsage);
  }
  if (verb == "STALL") {
    return detail::parse_stall(w);
  }
  if (verb == "SEATFAIL") {
    return detail::parse_seat_fail(w);
  }
  return Parsed{};
}

/// @brief The STATE line's JSON object (the file comment's table, in that order).
/// @param state What the hooks reported.
/// @return One line of JSON, no newline.
[[nodiscard]] inline std::string format_state(const State &state) {
  std::string out = "{";
  detail::put_opt(out, "screen", state.screen);
  detail::put_opt(out, "phase", state.phase);
  detail::put_opt(out, "notice", state.notice);
  detail::put_opt(out, "banner", state.banner);
  detail::put_opt(out, "hold_reason", state.hold_reason);
  detail::put_opt(out, "calibrating", state.calibrating);
  detail::put_opt(out, "menu_open", state.menu_open);
  detail::put_cal(out, state.cal);
  detail::put_opt(out, "post", state.post);
  detail::put_opt(out, "post_check", state.post_check);
  detail::put_opt(out, "indicator", state.indicator);
  detail::put_opt(out, "indicator_text", state.indicator_text);
  detail::put_opt(out, "reset_reason", state.reset_reason);
  detail::put_stick(out, state.stick);
  detail::put_seat(out, state.seat);
  out += '}';
  return out;
}

/// @brief Reads every STATE field from its hook.
/// @param hooks The firmware's hooks.
/// @param screen The active screen's name (the remote UI reads it under lvgl_mutex).
/// @return The state, empty hooks as empty fields.
[[nodiscard]] inline State collect_state(const Hooks &hooks, std::optional<std::string> screen) {
  State s;
  s.screen = std::move(screen);
  s.phase = detail::call<std::string>(hooks.phase);
  s.notice = detail::call<std::string>(hooks.notice);
  s.banner = detail::call<std::string>(hooks.banner);
  s.hold_reason = detail::call<std::string>(hooks.hold_reason);
  s.calibrating = detail::call<bool>(hooks.calibrating);
  s.menu_open = detail::call<bool>(hooks.menu_open);
  s.cal = detail::call<CalRecord>(hooks.cal);
  s.post = detail::call<std::string>(hooks.post);
  s.post_check = hooks.post_check ? hooks.post_check() : std::nullopt;
  s.indicator = detail::call<std::string>(hooks.indicator);
  s.indicator_text = detail::call<std::string>(hooks.indicator_text);
  s.reset_reason = detail::call<std::string>(hooks.reset_reason);
  s.stick = detail::call<StickBlock>(hooks.stick);
  s.seat = detail::call<SeatBlock>(hooks.seat);
  return s;
}

/// @brief The reply to a request, from the hooks. CRASH is not answered here: the remote UI
///        replies and aborts itself.
/// @param request A parsed request (not CRASH).
/// @param hooks The firmware's hooks.
/// @param screen The active screen's name, for STATE.
/// @return One reply line without its newline: "OK", "OK {...}" or "ERR ...".
[[nodiscard]] inline std::string answer(const Request &request, const Hooks &hooks,
                                        std::optional<std::string> screen) {
  switch (request.verb) {
  case Verb::STATE:
    return "OK " + format_state(collect_state(hooks, std::move(screen)));
  case Verb::PERMIT_POST:
    return detail::verdict(hooks.permit_post ? std::optional<bool>{hooks.permit_post(request.post)}
                                             : std::nullopt,
                           "PERMIT POST", "C1 3.2 POST gate hook");
  case Verb::CAL_UNSAVED:
    return detail::verdict(detail::call<bool>(hooks.cal_unsaved), "CAL UNSAVED",
                           "C1 6 joystick_cal_measured");
  case Verb::POST_RERUN:
    return detail::verdict(detail::call<bool>(hooks.post_rerun), "POST RERUN",
                           "C3 REQ-RUI-06 POST runner");
  case Verb::STALL:
    return detail::verdict(hooks.stall
                               ? std::optional<bool>{hooks.stall(request.stall, request.stall_ms)}
                               : std::nullopt,
                           "STALL", "C4 REQ-RUI-07 / C2 O14");
  case Verb::SEATFAIL:
    return detail::verdict(hooks.seat_fail ? std::optional<bool>{hooks.seat_fail(request.seat_fail)}
                                           : std::nullopt,
                           "SEATFAIL", "seat REQ-RUI-09");
  case Verb::CRASH:
    return "ERR CRASH is answered by the remote UI";
  }
  return "ERR unknown bench verb";
}

} // namespace hmi::bench_verbs

/// @brief Hands the remote UI the bench verbs' hooks. Defined in remote_ui.cpp (bench builds
///        only); call it once from app_main before remote_ui_start, in a
///        CONFIG_HMI_BENCH_STICK_INJECT build only (an `if constexpr (BENCH_STICK_INJECT)`
///        block), so a release build references none of it.
/// @param hooks The hooks; copied.
void remote_ui_attach_bench_verbs(const hmi::bench_verbs::Hooks &hooks);
