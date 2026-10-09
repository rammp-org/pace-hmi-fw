#pragma once
// hmi::stick::StickMonitor: the stick's plausibility check and fault state machine (hazard fix
// C2, docs/plans/hazard-c2-spec.md §2-§4). Every ADC cycle it judges the three raw reads, before
// any averaging or lowpass of ours, and steps a small state machine: INIT, OK, SUSPECT,
// RECOVERING, FAULT, CALIBRATING. Only OK lets the stick drive (through C1's output permit).
//
// The transitions are data (STICK_MONITOR_TRANSITIONS, the spec's §3.5 row by row); decide() is
// the code, one switch per state, and the host oracle checks the two agree on every state,
// input and guard combination (STK-070). A state or input outside its enum goes to the safe
// state: FAULT, latched, reason INTERNAL.
//
// Pure C++: no ESP-IDF, no espp, no allocation, no logging, no lock, no indirect call. Time is a
// parameter: the ADC-side uint32 ms clock main injects (C1 §3.4, C4 §3.1). Not used by the
// firmware yet (C2 commit 3): the island gathers RawSample and steps it in C2's later commits.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hmi::stick {

// ---------------------------------------------------------------------------------------------
// Constants (C2 §4; owner answers D3, D4, D5, D7 of hazard-decisions.md: all as proposed)
// ---------------------------------------------------------------------------------------------

/// The highest window or read maximum any axis may show, mV. Board 2's calibrated max is
/// 2960-2971 mV; the ideal axis (and the bench injection's top) is 3300 mV. 3150 sits 179 mV
/// above the highest board-2 end and 150 mV below the rail. To measure (O1, M-2): what the P4's
/// ADC reads with a wiper shorted to 3.3 V.
inline constexpr float HIGH_RAIL_MV = 3150.0f;
/// How far above its calibrated max an axis may read, mV. A calibrated end is the mean of a 1 s
/// hold with <= 30 mV spread (X/Y) or <= 60 mV (twist); single twist reads are ~50 mV p-p at
/// rest. 100 mV is 2x the twist p-p and 30x the X/Y window noise (3 mV). To measure (M-1).
inline constexpr float CAL_OVERSHOOT_MV = 100.0f;
/// The fault window, in judged samples: 30 cycles = 1.05 s at the measured 35.0 ms cycle, the
/// span of POST's WINDOW_MIN_SAMPLES (D4).
inline constexpr std::uint32_t FAULT_WINDOW_CYCLES = 30;
/// Implausible samples within the window that latch the fault. Board 2: 0 failed cycles in
/// about 14,600 (85 runs). A lasting fault latches in 3 cycles (~105 ms).
inline constexpr std::uint32_t FAULT_COUNT = 3;
/// Consecutive plausible samples that end RECOVERING (~105 ms); then C1's 300 ms neutral.
inline constexpr std::uint32_t RECOVER_CLEAN_CYCLES = 3;
/// An X/Y sequence unchanged this long is stale. One window is 1024 B / 4 B = 256 conversions
/// at 2 x 1000 Hz = 128 ms (computed; to measure: joy.xy_age_max); 300 ms is ~2.3 windows.
inline constexpr std::uint32_t XY_STALE_MS = 300;
/// INIT without a window from X and from Y this long after the first cycle: FAULT, NO_SAMPLES
/// (~8 windows).
inline constexpr std::uint32_t START_MAX_MS = 1000;
/// Twist oneshot reads per cycle; all must succeed (D7). main static_asserts it equals the
/// island's twist_oversample.
inline constexpr std::uint8_t TWIST_OVERSAMPLE = 8;
/// The sequence a channel shows before its first window (the vendored ContinuousAdc's start).
inline constexpr std::uint32_t kSequenceStart = 0;

static_assert(FAULT_COUNT >= 2, "row 5 (INIT, +S !P +F) would fire: C2 §3.5 notes");
static_assert(FAULT_WINDOW_CYCLES >= FAULT_COUNT && FAULT_WINDOW_CYCLES <= 32,
              "the window is one uint32_t bitmask");
static_assert(RECOVER_CLEAN_CYCLES >= 1);

/// The high limit of one axis: min(HIGH_RAIL_MV, its calibrated max + CAL_OVERSHOOT_MV), from
/// the calibration in use (P4). Ideal defaults (3300 mV) give 3150.
[[nodiscard]] constexpr float high_limit_mv(float cal_max_mv) {
  return std::min(HIGH_RAIL_MV, cal_max_mv + CAL_OVERSHOOT_MV);
}

// ---------------------------------------------------------------------------------------------
// The raw sample and its classification (C2 §2)
// ---------------------------------------------------------------------------------------------

/// One X/Y channel's window from the vendored ContinuousAdc. When the mean is empty (the read
/// failed) the max and the sequence carry no information and are not looked at.
struct XyWindowMv {
  std::optional<float> mean_mv; ///< espp's mean of the window, mV
  float max_mv;                 ///< the window's largest conversion, mV
  std::uint32_t sequence;       ///< +1 for each window holding a conversion of this channel
};

/// What the monitor judges each cycle: the three raw reads before our averaging and lowpass.
struct RawSample {
  XyWindowMv horizontal;              ///< ADC1_CH1 (GPIO17)
  XyWindowMv vertical;                ///< ADC1_CH0 (GPIO16)
  std::optional<float> twist_mean_mv; ///< mean of the successful oneshot reads
  float twist_max_mv;                 ///< largest successful read
  std::uint8_t twist_reads;           ///< successful reads, 0..TWIST_OVERSAMPLE
};

/// Each axis's high limit, mV (high_limit_mv of the calibration in use).
struct HighLimitsMv {
  float horizontal;
  float vertical;
  float twist;
};

/// Why a sample is implausible (C2 §2.2), and the fault reasons LATCH can store.
enum class SampleFault : std::uint8_t {
  NONE,          ///< plausible
  MISSING,       ///< P1: a mean is missing
  NAN_VALUE,     ///< P2: a mean or max is NaN or +-inf (the spec's NAN; <cmath> owns that name)
  NEGATIVE,      ///< P3: a mean below 0 mV
  HIGH,          ///< P4: a max above its high limit
  STALE,         ///< P5: an X/Y sequence unchanged for XY_STALE_MS
  TWIST_PARTIAL, ///< P6: fewer than TWIST_OVERSAMPLE twist reads succeeded
  NO_SAMPLES,    ///< fault reason only: no X and Y window by START_MAX_MS (row 2)
  INTERNAL,      ///< fault reason only: a state or input outside its enum (safe state)
};
inline constexpr std::size_t kSampleFaultCount = 9;

enum class StickAxis : std::uint8_t { NONE, HORIZONTAL, VERTICAL, TWIST };

struct SampleReason {
  SampleFault kind;
  StickAxis axis;
};
inline constexpr SampleReason kPlausible{SampleFault::NONE, StickAxis::NONE};

namespace classify_detail {
// The first of the three axes (0 horizontal, 1 vertical, 2 twist) for which @p fails holds,
// or 3 when none does.
template <typename Fails> [[nodiscard]] constexpr std::size_t first_axis(Fails fails) noexcept {
  constexpr std::array<std::size_t, 3> kAxes{0, 1, 2};
  const auto it = std::ranges::find_if(kAxes, fails);
  return it == kAxes.end() ? kAxes.size() : *it;
}
} // namespace classify_detail

/// The sample's reason: the first failing check in P1..P6 order, axes horizontal, vertical,
/// twist; kPlausible when every check holds. 0 mV up to the calibrated min is plausible on
/// purpose (D2 residual). `stale_h` / `stale_v`: P5 for that channel, judged by the monitor.
[[nodiscard]] constexpr SampleReason classify(const RawSample &s, const HighLimitsMv &limits,
                                              bool stale_h, bool stale_v) noexcept {
  using enum SampleFault;
  const std::array<const std::optional<float> *, 3> means{&s.horizontal.mean_mv,
                                                          &s.vertical.mean_mv, &s.twist_mean_mv};
  const std::array<float, 3> maxes{s.horizontal.max_mv, s.vertical.max_mv, s.twist_max_mv};
  const std::array<float, 3> limit{limits.horizontal, limits.vertical, limits.twist};
  const std::array<StickAxis, 3> axes{StickAxis::HORIZONTAL, StickAxis::VERTICAL, StickAxis::TWIST};
  using classify_detail::first_axis;
  if (const std::size_t i = first_axis([&](std::size_t a) { return !means[a]->has_value(); });
      i < axes.size()) { // P1
    return {MISSING, axes[i]};
  }
  if (const std::size_t i = first_axis(
          [&](std::size_t a) { return !std::isfinite(**means[a]) || !std::isfinite(maxes[a]); });
      i < axes.size()) { // P2
    return {NAN_VALUE, axes[i]};
  }
  if (const std::size_t i = first_axis([&](std::size_t a) { return **means[a] < 0.0f; });
      i < axes.size()) { // P3
    return {NEGATIVE, axes[i]};
  }
  if (const std::size_t i = first_axis([&](std::size_t a) { return maxes[a] > limit[a]; });
      i < axes.size()) { // P4
    return {HIGH, axes[i]};
  }
  if (stale_h || stale_v) { // P5
    return {STALE, stale_h ? StickAxis::HORIZONTAL : StickAxis::VERTICAL};
  }
  if (s.twist_reads < TWIST_OVERSAMPLE) { // P6
    return {TWIST_PARTIAL, StickAxis::TWIST};
  }
  return kPlausible;
}

// ---------------------------------------------------------------------------------------------
// The state machine's vocabulary (C2 §3.1-§3.4)
// ---------------------------------------------------------------------------------------------

enum class MonitorState : std::uint8_t { INIT, OK, SUSPECT, RECOVERING, FAULT, CALIBRATING };
inline constexpr std::size_t kMonitorStateCount = 6;

/// One input per ADC cycle, chosen by select_input (§3.2).
enum class MonitorInput : std::uint8_t { SAMPLE, CAL_START, CAL_END };
inline constexpr std::size_t kMonitorInputCount = 3;

/// §3.2: CAL_START when a calibration runs and the state is not CALIBRATING; CAL_END when none
/// runs and the state is CALIBRATING; SAMPLE otherwise. `calibrating` is sampled once a cycle
/// and the same value goes to the pipeline (REQ-CTL-15).
[[nodiscard]] constexpr MonitorInput select_input(bool calibrating, MonitorState state) {
  if (state == MonitorState::CALIBRATING) {
    return calibrating ? MonitorInput::SAMPLE : MonitorInput::CAL_END;
  }
  return calibrating ? MonitorInput::CAL_START : MonitorInput::SAMPLE;
}

/// The guards (§3.3), one bit each.
enum class MonitorGuard : std::uint8_t {
  PLAUSIBLE,       ///< P: this sample passes §2.2
  STARTED,         ///< S: X and Y have each delivered a window
  START_TIMED_OUT, ///< T: now - first cycle >= START_MAX_MS
  REACHES_FAULT,   ///< F: implausible samples in the window, this one counted, >= FAULT_COUNT
  CLEAN,           ///< C: consecutive plausible samples, this one counted, >= RECOVER_CLEAN_CYCLES
  LATCHED,         ///< L: the fault latch is set
  NEW_VALID_CAL,   ///< V: the validated-calibration generation moved since CAL_START
};
inline constexpr std::size_t kMonitorGuardCount = 7;
using GuardBits = std::uint8_t;

[[nodiscard]] constexpr GuardBits gbit(MonitorGuard g) {
  return static_cast<GuardBits>(1U << static_cast<unsigned>(g));
}

/// A row's guard: these bits must be set and these clear; the rest are not looked at.
struct GuardTerm {
  GuardBits need_true;
  GuardBits need_false;
};
[[nodiscard]] constexpr bool matches(GuardTerm t, GuardBits g) {
  return (g & t.need_true) == t.need_true && (g & t.need_false) == 0;
}

/// The actions (§3.4), run left to right.
enum class MonitorAction : std::uint8_t {
  NONE,             ///< padding
  PUSH_GOOD,        ///< window <- good; clean run + 1
  PUSH_BAD,         ///< window <- bad; clean run := 0; count the kind; store the reason
  NOTE_SUSPECT,     ///< suspect onsets + 1
  LATCH,            ///< latch := true; faults + 1; fault reason := the sample's reason
  LATCH_NO_SAMPLES, ///< LATCH with fault reason NO_SAMPLES (row 2)
  COUNT_KIND,       ///< count the sample's implausible kind, if any (diagnostics only)
  SUSPEND,          ///< store the calibration generation
  RESUME,           ///< window emptied; clean run := 0
  UNLATCH,          ///< latch := false
};
inline constexpr std::size_t kMonitorMaxActions = 2;
using MonitorActions = std::array<MonitorAction, kMonitorMaxActions>;

/// One row of STICK_MONITOR_TRANSITIONS.
struct MonitorTransition {
  int row; ///< the spec's row number (C2 §3.5)
  MonitorState from;
  MonitorInput input;
  GuardTerm guard;
  MonitorState to;
  MonitorActions actions;
};

/// What decide() returns: the row taken, or none (row 0: nothing changes, TS-UNIT-08).
struct MonitorDecision {
  int row;
  MonitorState to;
  MonitorActions actions;
};

namespace monitor_detail {
using enum MonitorState;
using enum MonitorInput;
using enum MonitorAction;
inline constexpr GuardBits P = gbit(MonitorGuard::PLAUSIBLE);
inline constexpr GuardBits S = gbit(MonitorGuard::STARTED);
inline constexpr GuardBits T = gbit(MonitorGuard::START_TIMED_OUT);
inline constexpr GuardBits F = gbit(MonitorGuard::REACHES_FAULT);
inline constexpr GuardBits C = gbit(MonitorGuard::CLEAN);
inline constexpr GuardBits L = gbit(MonitorGuard::LATCHED);
inline constexpr GuardBits V = gbit(MonitorGuard::NEW_VALID_CAL);
constexpr GuardTerm when(GuardBits t, GuardBits f) { return {t, f}; }
inline constexpr GuardTerm kAny{0, 0};
inline constexpr MonitorActions kNo{NONE, NONE};
inline constexpr MonitorActions kGood{PUSH_GOOD, NONE};
inline constexpr MonitorActions kBad{PUSH_BAD, NONE};
inline constexpr MonitorActions kSuspect{PUSH_BAD, NOTE_SUSPECT};
inline constexpr MonitorActions kLatch{PUSH_BAD, LATCH};
} // namespace monitor_detail

// ---------------------------------------------------------------------------------------------
// The table (C2 §3.5, row by row). Guards C and F are judged after this sample is counted.
// ---------------------------------------------------------------------------------------------
// clang-format off
inline constexpr std::array<MonitorTransition, 26> STICK_MONITOR_TRANSITIONS = [] {
  using namespace monitor_detail;
  return std::array<MonitorTransition, 26>{{
    {1,  INIT,        SAMPLE,    when(0, S | T),     INIT,        kNo},
    {2,  INIT,        SAMPLE,    when(T, S),         FAULT,       {LATCH_NO_SAMPLES, NONE}},
    {3,  INIT,        SAMPLE,    when(S | P, 0),     RECOVERING,  kGood},
    {4,  INIT,        SAMPLE,    when(S, P | F),     SUSPECT,     kSuspect},
    {5,  INIT,        SAMPLE,    when(S | F, P),     FAULT,       kLatch},
    {6,  OK,          SAMPLE,    when(P, 0),         OK,          kGood},
    {7,  OK,          SAMPLE,    when(0, P | F),     SUSPECT,     kSuspect},
    {8,  OK,          SAMPLE,    when(F, P),         FAULT,       kLatch},
    {9,  SUSPECT,     SAMPLE,    when(P, C),         RECOVERING,  kGood},
    {10, SUSPECT,     SAMPLE,    when(P | C, 0),     OK,          kGood},
    {11, SUSPECT,     SAMPLE,    when(0, P | F),     SUSPECT,     kBad},
    {12, SUSPECT,     SAMPLE,    when(F, P),         FAULT,       kLatch},
    {13, RECOVERING,  SAMPLE,    when(P, C),         RECOVERING,  kGood},
    {14, RECOVERING,  SAMPLE,    when(P | C, 0),     OK,          kGood},
    {15, RECOVERING,  SAMPLE,    when(0, P | F),     SUSPECT,     kSuspect},
    {16, RECOVERING,  SAMPLE,    when(F, P),         FAULT,       kLatch},
    {17, FAULT,       SAMPLE,    kAny,               FAULT,       {COUNT_KIND, NONE}},
    {18, INIT,        CAL_START, kAny,               CALIBRATING, {SUSPEND, NONE}},
    {19, OK,          CAL_START, kAny,               CALIBRATING, {SUSPEND, NONE}},
    {20, SUSPECT,     CAL_START, kAny,               CALIBRATING, {SUSPEND, NONE}},
    {21, RECOVERING,  CAL_START, kAny,               CALIBRATING, {SUSPEND, NONE}},
    {22, FAULT,       CAL_START, kAny,               CALIBRATING, {SUSPEND, NONE}},
    {23, CALIBRATING, SAMPLE,    kAny,               CALIBRATING, kNo},
    {24, CALIBRATING, CAL_END,   when(V, 0),         RECOVERING,  {RESUME, UNLATCH}},
    {25, CALIBRATING, CAL_END,   when(L, V),         FAULT,       {RESUME, NONE}},
    {26, CALIBRATING, CAL_END,   when(0, V | L),     RECOVERING,  {RESUME, NONE}},
  }};
}();
// clang-format on

/// The oracle's lookup: the one matching row, or row 0 (nothing changes).
[[nodiscard]] constexpr MonitorDecision find_row(MonitorState s, MonitorInput in, GuardBits g) {
  const auto it = std::ranges::find_if(STICK_MONITOR_TRANSITIONS, [=](const MonitorTransition &t) {
    return t.from == s && t.input == in && matches(t.guard, g);
  });
  if (it == STICK_MONITOR_TRANSITIONS.end()) {
    return {0, s, monitor_detail::kNo};
  }
  return {it->row, it->to, it->actions};
}

// ---------------------------------------------------------------------------------------------
// decide(): the code. One switch on the state, each branch naming its rows (REQ-STK-25).
// ---------------------------------------------------------------------------------------------
namespace monitor_detail {

[[nodiscard]] constexpr bool has(GuardBits g, GuardBits b) { return (g & b) != 0; }
[[nodiscard]] constexpr MonitorDecision row(int n, MonitorState to, MonitorActions a) {
  return {n, to, a};
}
[[nodiscard]] constexpr MonitorDecision stay(MonitorState s) { return {0, s, kNo}; }

// A bad sample from a judged state: SUSPECT (onset or not) or FAULT.
[[nodiscard]] constexpr MonitorDecision bad(GuardBits g, int suspect_row, int fault_row,
                                            const MonitorActions &suspect) {
  return has(g, F) ? row(fault_row, FAULT, kLatch) : row(suspect_row, SUSPECT, suspect);
}

[[nodiscard]] constexpr MonitorDecision on_init(GuardBits g) {
  if (!has(g, S)) {
    return has(g, T) ? row(2, FAULT, {LATCH_NO_SAMPLES, NONE}) : row(1, INIT, kNo); // rows 2, 1
  }
  return has(g, P) ? row(3, RECOVERING, kGood) : bad(g, 4, 5, kSuspect); // rows 3, 4, 5
}

[[nodiscard]] constexpr MonitorDecision on_sample(MonitorState s, GuardBits g) {
  switch (s) {
  case INIT:
    return on_init(g);
  case OK:
    return has(g, P) ? row(6, OK, kGood) : bad(g, 7, 8, kSuspect); // rows 6, 7, 8
  case SUSPECT:
    if (has(g, P)) {
      return has(g, C) ? row(10, OK, kGood) : row(9, RECOVERING, kGood); // rows 10, 9
    }
    return bad(g, 11, 12, kBad); // rows 11, 12
  case RECOVERING:
    if (has(g, P)) {
      return has(g, C) ? row(14, OK, kGood) : row(13, RECOVERING, kGood); // rows 14, 13
    }
    return bad(g, 15, 16, kSuspect); // rows 15, 16
  case FAULT:
    return row(17, FAULT, {COUNT_KIND, NONE}); // row 17
  case CALIBRATING:
    return row(23, CALIBRATING, kNo); // row 23
  default:
    return stay(s); // a state outside the enum: the caller goes to the safe state
  }
}

[[nodiscard]] constexpr MonitorDecision on_cal_start(MonitorState s) {
  switch (s) {
  case INIT:
    return row(18, CALIBRATING, {SUSPEND, NONE});
  case OK:
    return row(19, CALIBRATING, {SUSPEND, NONE});
  case SUSPECT:
    return row(20, CALIBRATING, {SUSPEND, NONE});
  case RECOVERING:
    return row(21, CALIBRATING, {SUSPEND, NONE});
  case FAULT:
    return row(22, CALIBRATING, {SUSPEND, NONE});
  case CALIBRATING:
    return stay(s); // §3.2 cannot build it; nothing changes
  default:
    return stay(s); // a state outside the enum: the caller goes to the safe state
  }
}

[[nodiscard]] constexpr MonitorDecision on_cal_end(MonitorState s, GuardBits g) {
  if (s != CALIBRATING) {
    return stay(s); // §3.2 cannot build it; nothing changes
  }
  if (has(g, V)) {
    return row(24, RECOVERING, {RESUME, UNLATCH}); // row 24
  }
  return has(g, L) ? row(25, FAULT, {RESUME, NONE}) : row(26, RECOVERING, {RESUME, NONE});
}

[[nodiscard]] constexpr bool valid_state(MonitorState s) {
  return static_cast<std::size_t>(s) < kMonitorStateCount;
}
[[nodiscard]] constexpr bool valid_input(MonitorInput in) {
  return static_cast<std::size_t>(in) < kMonitorInputCount;
}

} // namespace monitor_detail

/// The transition for (state, input, guards): the table's row, or row 0 with the state as it
/// is. A state or input outside its enum also gives row 0 here: StickMonitor::transition
/// screens them first and goes to the safe state.
[[nodiscard]] constexpr MonitorDecision decide(MonitorState s, MonitorInput in,
                                               GuardBits g) noexcept {
  using namespace monitor_detail;
  switch (in) {
  case MonitorInput::SAMPLE:
    return on_sample(s, g);
  case MonitorInput::CAL_START:
    return on_cal_start(s);
  case MonitorInput::CAL_END:
    return on_cal_end(s, g);
  default:
    return stay(s);
  }
}

// ---------------------------------------------------------------------------------------------
// Table invariants (C2 §3.5 notes, STK-071), checked at compile time
// ---------------------------------------------------------------------------------------------
namespace monitor_detail {

[[nodiscard]] constexpr bool exclusive(GuardTerm a, GuardTerm b) {
  return (a.need_true & b.need_false) != 0 || (a.need_false & b.need_true) != 0;
}
[[nodiscard]] constexpr bool rows_exclusive() {
  for (std::size_t i = 0; i < STICK_MONITOR_TRANSITIONS.size(); ++i) {
    for (std::size_t j = i + 1; j < STICK_MONITOR_TRANSITIONS.size(); ++j) {
      const MonitorTransition &a = STICK_MONITOR_TRANSITIONS[i];
      const MonitorTransition &b = STICK_MONITOR_TRANSITIONS[j];
      if (a.from == b.from && a.input == b.input && !exclusive(a.guard, b.guard)) {
        return false;
      }
    }
  }
  return true;
}
[[nodiscard]] constexpr bool rows_numbered() {
  for (std::size_t i = 0; i < STICK_MONITOR_TRANSITIONS.size(); ++i) {
    if (STICK_MONITOR_TRANSITIONS[i].row != static_cast<int>(i) + 1) {
      return false;
    }
  }
  return true;
}
[[nodiscard]] constexpr bool has_action(const MonitorTransition &t, MonitorAction a) {
  return std::ranges::find(t.actions, a) != t.actions.end();
}
[[nodiscard]] constexpr bool every_non_fault_state_has_a_way_out() {
  for (std::size_t s = 0; s < kMonitorStateCount; ++s) {
    const auto state = static_cast<MonitorState>(s);
    const bool out = std::ranges::any_of(STICK_MONITOR_TRANSITIONS, [state](const auto &t) {
      return t.from == state && t.to != state;
    });
    if (state != FAULT && !out) {
      return false;
    }
  }
  return true;
}
[[nodiscard]] constexpr bool fault_left_only_by_cal_start() {
  return std::ranges::all_of(STICK_MONITOR_TRANSITIONS, [](const auto &t) {
    return t.from != FAULT || t.to == FAULT || t.input == CAL_START;
  });
}
[[nodiscard]] constexpr bool latch_only_into_fault() {
  return std::ranges::all_of(STICK_MONITOR_TRANSITIONS, [](const auto &t) {
    return (!has_action(t, LATCH) && !has_action(t, LATCH_NO_SAMPLES)) || t.to == FAULT;
  });
}
[[nodiscard]] constexpr bool unlatch_only_on_row_24() {
  return std::ranges::all_of(STICK_MONITOR_TRANSITIONS,
                             [](const auto &t) { return !has_action(t, UNLATCH) || t.row == 24; });
}

} // namespace monitor_detail

static_assert(monitor_detail::rows_numbered(), "rows in the spec's order, 1..26");
static_assert(monitor_detail::rows_exclusive(), "no two rows match one combination");
static_assert(monitor_detail::every_non_fault_state_has_a_way_out(), "CS-SAF-02");
static_assert(monitor_detail::fault_left_only_by_cal_start(), "FAULT is latched (REQ-STK-22)");
static_assert(monitor_detail::latch_only_into_fault());
static_assert(monitor_detail::unlatch_only_on_row_24());

// ---------------------------------------------------------------------------------------------
// The monitor
// ---------------------------------------------------------------------------------------------

/// What the monitor allows (C2 §3.1): OK only in OK, FAULT in FAULT, CHECK otherwise. C2's
/// wiring maps it onto C1's StickHealth.
enum class MonitorHealth : std::uint8_t { OK, CHECK, FAULT };

[[nodiscard]] constexpr MonitorHealth health_of(MonitorState s) {
  return s == MonitorState::OK      ? MonitorHealth::OK
         : s == MonitorState::FAULT ? MonitorHealth::FAULT
                                    : MonitorHealth::CHECK;
}
/// Whether the stick's menu keys are live in @p s: OK and RECOVERING (§3.1, D11).
[[nodiscard]] constexpr bool keys_live(MonitorState s) {
  return s == MonitorState::OK || s == MonitorState::RECOVERING;
}

/// The monitor. One instance, owned by the ADC task; step() once per cycle.
class StickMonitor {
public:
  /// Diagnostics (REQ-STK-24): counters only go up.
  struct Counters {
    std::uint32_t suspect_onsets = 0;
    std::uint32_t faults = 0;
    std::array<std::uint32_t, kSampleFaultCount> implausible{}; ///< samples per kind
    std::uint32_t xy_age_max_ms = 0; ///< largest X/Y sequence age since the first window
  };

  /// One cycle: tracks the X/Y sequences, judges @p sample, then takes the row for
  /// (state, @p input, guards). `cal_generation` is joystick_cal's validated-record counter
  /// (REQ-CAL-10); @p now_ms the ADC-side clock.
  constexpr void step(MonitorInput input, const RawSample &sample, const HighLimitsMv &limits,
                      std::uint32_t cal_generation, std::uint32_t now_ms) noexcept {
    track(sample, now_ms);
    const bool stale_h = xy_stale(0, now_ms);
    const bool stale_v = xy_stale(1, now_ms);
    const SampleReason reason = classify(sample, limits, stale_h, stale_v);
    transition(input, guards(reason, cal_generation, now_ms), reason, cal_generation);
  }

  /// The transition alone, on given guards (step() calls it; the oracle drives it directly).
  /// A state, input or action outside its enum: the safe state (FAULT, latched, INTERNAL).
  constexpr void transition(MonitorInput input, GuardBits g, SampleReason reason,
                            std::uint32_t cal_generation) noexcept {
    if (!monitor_detail::valid_state(state_) || !monitor_detail::valid_input(input)) {
      enter_safe_state();
      return;
    }
    apply(decide(state_, input, g), reason, cal_generation);
  }

  [[nodiscard]] constexpr MonitorState state() const { return state_; }
  [[nodiscard]] constexpr MonitorHealth health() const { return health_of(state_); }
  [[nodiscard]] constexpr bool latched() const { return latched_; }
  [[nodiscard]] constexpr SampleReason last_reason() const { return last_reason_; }
  [[nodiscard]] constexpr SampleReason fault_reason() const { return fault_reason_; }
  [[nodiscard]] constexpr const Counters &counters() const { return counters_; }
  /// Implausible samples in the window now (0..FAULT_WINDOW_CYCLES).
  [[nodiscard]] constexpr std::uint32_t window_bad() const { return bad_in(window_); }
  [[nodiscard]] constexpr std::uint32_t clean_run() const { return clean_run_; }

private:
  friend struct StickMonitorTestPeer; // the host tests set and compare the whole state

  static constexpr std::uint32_t kWindowMask =
      FAULT_WINDOW_CYCLES == 32 ? 0xFFFF'FFFFU : (1U << FAULT_WINDOW_CYCLES) - 1U;
  static constexpr std::uint32_t bad_in(std::uint32_t window) {
    return static_cast<std::uint32_t>(std::popcount(window & kWindowMask));
  }

  // The sequences: a change (any difference, a wrap included) restarts the channel's age; a
  // missing read tells nothing. Before the first cycle a channel's sequence is kSequenceStart.
  constexpr void track(const RawSample &s, std::uint32_t now_ms) noexcept {
    if (!has_first_) {
      has_first_ = true;
      first_ms_ = now_ms;
      changed_ms_ = {now_ms, now_ms};
    }
    const std::array<const XyWindowMv *, 2> xy{&s.horizontal, &s.vertical};
    for (std::size_t i = 0; i < 2; ++i) {
      if (xy[i]->mean_mv.has_value() && xy[i]->sequence != seen_seq_[i]) {
        seen_seq_[i] = xy[i]->sequence;
        changed_ms_[i] = now_ms;
        started_[i] = true;
      }
      if (started_[i]) {
        counters_.xy_age_max_ms = std::max(counters_.xy_age_max_ms, now_ms - changed_ms_[i]);
      }
    }
  }
  [[nodiscard]] constexpr bool xy_stale(std::size_t i, std::uint32_t now_ms) const noexcept {
    return now_ms - changed_ms_[i] >= XY_STALE_MS; // uint32 modulo 2^32, as C4's REQ-CTL-07
  }

  [[nodiscard]] constexpr GuardBits guards(SampleReason reason, std::uint32_t cal_generation,
                                           std::uint32_t now_ms) const noexcept {
    using namespace monitor_detail;
    const bool plausible = reason.kind == SampleFault::NONE;
    const std::uint32_t pushed = (window_ << 1U) | (plausible ? 0U : 1U);
    const std::uint32_t clean = plausible ? clean_run_ + 1U : 0U;
    const unsigned g = (plausible ? P : 0U) | (started_[0] && started_[1] ? S : 0U) |
                       (now_ms - first_ms_ >= START_MAX_MS ? T : 0U) |
                       (bad_in(pushed) >= FAULT_COUNT ? F : 0U) |
                       (clean >= RECOVER_CLEAN_CYCLES ? C : 0U) | (latched_ ? L : 0U) |
                       (cal_generation != generation_at_start_ ? V : 0U);
    return static_cast<GuardBits>(g);
  }

  constexpr void latch(SampleReason reason) noexcept {
    latched_ = true;
    ++counters_.faults;
    fault_reason_ = reason;
  }
  constexpr void count_kind(SampleReason reason) noexcept {
    if (reason.kind != SampleFault::NONE) {
      ++counters_.implausible[static_cast<std::size_t>(reason.kind)];
    }
  }
  constexpr void push(bool bad) noexcept {
    window_ = ((window_ << 1U) | (bad ? 1U : 0U)) & kWindowMask;
    clean_run_ = bad ? 0U : clean_run_ + 1U;
  }

  // Takes a decision: its actions left to right, then its state. Row 0 changes nothing
  // (TS-UNIT-08: any combination not listed changes nothing).
  constexpr void apply(const MonitorDecision &d, SampleReason reason,
                       std::uint32_t cal_generation) noexcept {
    if (d.row == 0) {
      return;
    }
    // Left to right; all_of stops at the first action it cannot perform.
    const bool done = std::ranges::all_of(
        d.actions, [&](MonitorAction a) { return perform(a, reason, cal_generation); });
    if (!done) {
      enter_safe_state();
      return;
    }
    state_ = d.to;
  }

  constexpr void enter_safe_state() noexcept {
    state_ = MonitorState::FAULT;
    latch({SampleFault::INTERNAL, StickAxis::NONE});
  }

  // One action; false for a value outside the enum (corrupted memory: the safe state).
  [[nodiscard]] constexpr bool perform(MonitorAction a, SampleReason reason,
                                       std::uint32_t cal_generation) noexcept {
    switch (a) {
    case MonitorAction::PUSH_GOOD:
      push(false);
      break;
    case MonitorAction::PUSH_BAD:
      push(true);
      count_kind(reason);
      last_reason_ = reason;
      break;
    case MonitorAction::NOTE_SUSPECT:
      ++counters_.suspect_onsets;
      break;
    case MonitorAction::LATCH:
      latch(reason);
      break;
    case MonitorAction::LATCH_NO_SAMPLES:
      latch({SampleFault::NO_SAMPLES, StickAxis::NONE});
      break;
    case MonitorAction::COUNT_KIND:
      count_kind(reason);
      break;
    case MonitorAction::SUSPEND:
      generation_at_start_ = cal_generation;
      break;
    case MonitorAction::RESUME:
      window_ = 0;
      clean_run_ = 0;
      break;
    case MonitorAction::UNLATCH:
      latched_ = false;
      break;
    case MonitorAction::NONE:
      break;
    default:
      return false;
    }
    return true;
  }

  MonitorState state_ = MonitorState::INIT;
  bool latched_ = false;
  std::uint32_t window_ = 0; ///< bit 0 = the newest judged sample; 1 = implausible
  std::uint32_t clean_run_ = 0;
  std::uint32_t generation_at_start_ = 0;
  SampleReason last_reason_ = kPlausible;
  SampleReason fault_reason_ = kPlausible;
  Counters counters_{};
  bool has_first_ = false;
  std::uint32_t first_ms_ = 0;
  std::array<std::uint32_t, 2> seen_seq_{kSequenceStart, kSequenceStart}; ///< H, V
  std::array<std::uint32_t, 2> changed_ms_{};
  std::array<bool, 2> started_{};
};

} // namespace hmi::stick
