// The quick POST evaluator (README, REQ-POST-01..13). Each check is measured by a switch on its
// Id (no function pointers, CS-SAF-02) and judged against its CHECKS row. Safety code: every
// switch names every enumerator and its default goes to FAIL (CS-TYP-06); facts that
// contradict themselves FAIL rather than assert (CS-ERR-04).

#include "post/post.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ranges>

namespace hmi::post {
namespace {

/// What measuring a check found.
enum class Got : uint8_t {
  VALUE,   ///< a measurement, judged against the row's limits
  PENDING, ///< not measurable yet
  INVALID, ///< the facts contradict themselves: FAIL whatever the limits
};

struct Measured {
  Got got = Got::PENDING;
  int32_t value = 0;
  Reason reason = Reason::NOT_MEASURED; ///< PENDING/INVALID: why; VALUE: a yes/no row's FAIL
};

constexpr Measured value_of(int32_t value, Reason if_fail = Reason::OK) {
  return Measured{.got = Got::VALUE, .value = value, .reason = if_fail};
}
constexpr Measured pending(Reason why) {
  return Measured{.got = Got::PENDING, .value = 0, .reason = why};
}
constexpr Measured invalid(Reason why) {
  return Measured{.got = Got::INVALID, .value = 0, .reason = why};
}
constexpr int32_t yes_no(bool yes) { return yes ? 1 : 0; }

/// int64 arithmetic on int32 facts cannot overflow; this brings the answer back.
constexpr int32_t saturate(int64_t value) {
  constexpr int64_t LO = std::numeric_limits<int32_t>::min();
  constexpr int64_t HI = std::numeric_limits<int32_t>::max();
  return static_cast<int32_t>(std::clamp(value, LO, HI));
}

/// Which axis a check reads: the member of the window and of the calibration.
using RestOf = AxisRest StickWindow::*;
using CalOf = AxisCal Calibration::*;

/// The window's axis, if it can be judged: enough samples, and min <= mean <= max.
Measured axis_gate(const AxisRest &rest) {
  if (rest.samples < static_cast<uint32_t>(WINDOW_MIN_SAMPLES)) {
    return pending(Reason::TOO_FEW_SAMPLES);
  }
  if (rest.min_mv > rest.mean_mv || rest.mean_mv > rest.max_mv) {
    return invalid(Reason::BAD_FACTS);
  }
  return value_of(0);
}

// --- the stick window ---------------------------------------------------------------------

Measured measure_adc_valid(const Facts &facts) {
  if (!facts.window) {
    return pending(Reason::NOT_MEASURED);
  }
  const StickWindow &w = *facts.window;
  if (w.valid_cycles > w.cycles) {
    return invalid(Reason::BAD_FACTS);
  }
  if (w.cycles < static_cast<uint32_t>(WINDOW_MIN_SAMPLES)) {
    return pending(Reason::TOO_FEW_SAMPLES);
  }
  // Rounds down, so a share just under the limit never rounds up into it.
  const uint64_t permille = uint64_t{w.valid_cycles} * 1000U / w.cycles;
  return value_of(static_cast<int32_t>(permille));
}

Measured measure_rest(const Facts &facts, RestOf rest_of, CalOf cal_of) {
  if (!facts.window || !facts.cal) {
    return pending(Reason::NOT_MEASURED);
  }
  const AxisRest &rest = (*facts.window).*rest_of;
  const Measured gate = axis_gate(rest);
  if (gate.got != Got::VALUE) {
    return gate;
  }
  const int64_t off = int64_t{rest.mean_mv} - int64_t{((*facts.cal).*cal_of).centre_mv};
  return value_of(saturate(off < 0 ? -off : off));
}

Measured measure_still(const Facts &facts, RestOf rest_of) {
  if (!facts.window) {
    return pending(Reason::NOT_MEASURED);
  }
  const AxisRest &rest = (*facts.window).*rest_of;
  const Measured gate = axis_gate(rest);
  if (gate.got != Got::VALUE) {
    return gate;
  }
  return value_of(saturate(int64_t{rest.max_mv} - int64_t{rest.min_mv}));
}

Measured measure_button(const Facts &facts) {
  if (!facts.window) {
    return pending(Reason::NOT_MEASURED);
  }
  if (facts.window->cycles < static_cast<uint32_t>(WINDOW_MIN_SAMPLES)) {
    return pending(Reason::TOO_FEW_SAMPLES);
  }
  return value_of(yes_no(facts.window->button_idle), Reason::BUTTON_PRESSED);
}

// --- calibration and board ----------------------------------------------------------------

Measured measure_cal_saved(const Facts &facts) {
  if (!facts.cal) {
    return pending(Reason::NOT_MEASURED);
  }
  return value_of(yes_no(facts.cal->saved), Reason::NOT_SAVED);
}

/// The least travel either side of centre over the three axes; centre outside (min, max)
/// gives zero or less, so it fails the same limit.
Measured measure_cal_span(const Facts &facts) {
  if (!facts.cal) {
    return pending(Reason::NOT_MEASURED);
  }
  int64_t least = std::numeric_limits<int64_t>::max();
  for (const AxisCal *a : {&facts.cal->x, &facts.cal->y, &facts.cal->twist}) {
    least = std::min({least, int64_t{a->centre_mv} - int64_t{a->min_mv},
                      int64_t{a->max_mv} - int64_t{a->centre_mv}});
  }
  return value_of(saturate(least));
}

Measured measure_i2c(const Facts &facts) {
  if (!facts.i2c) {
    return pending(Reason::NOT_MEASURED);
  }
  const auto missing = std::count_if(EXPECTED_I2C.begin(), EXPECTED_I2C.end(),
                                     [&](uint8_t address) { return !facts.i2c->has(address); });
  return value_of(static_cast<int32_t>(missing), Reason::DEVICE_MISSING);
}

/// Clean: a reset someone asked for or power came up. Unclean: the firmware or the supply
/// failed. Anything else is unknown, and fails too (REQ-POST-05).
Measured classify_reset(ResetReason reason) {
  switch (reason) {
  case ResetReason::POWERON:
  case ResetReason::EXT:
  case ResetReason::SW:
  case ResetReason::DEEPSLEEP:
  case ResetReason::SDIO:
  case ResetReason::USB:
  case ResetReason::JTAG:
    return value_of(1);
  case ResetReason::PANIC:
  case ResetReason::INT_WDT:
  case ResetReason::TASK_WDT:
  case ResetReason::WDT:
  case ResetReason::BROWNOUT:
  case ResetReason::EFUSE:
  case ResetReason::PWR_GLITCH:
  case ResetReason::CPU_LOCKUP:
    return value_of(0, Reason::UNCLEAN_RESET);
  case ResetReason::UNKNOWN:
  default:
    return value_of(0, Reason::UNKNOWN_RESET);
  }
}

Measured measure_reset(const Facts &facts) {
  if (!facts.reset) {
    return pending(Reason::NOT_MEASURED);
  }
  return classify_reset(*facts.reset);
}

Measured measure_image(const Facts &facts) {
  if (!facts.image) {
    return pending(Reason::NOT_MEASURED);
  }
  if (!facts.image->verified) {
    return value_of(0, Reason::IMAGE_NOT_VERIFIED);
  }
  switch (facts.image->state) {
  case OtaImageState::VALID:
  case OtaImageState::UNDEFINED:
  case OtaImageState::PENDING_VERIFY:
    return value_of(1);
  case OtaImageState::NEW:
  case OtaImageState::INVALID:
  case OtaImageState::ABORTED:
  default:
    return value_of(0, Reason::IMAGE_STATE);
  }
}

Measured measure_memory(const Facts &facts, int32_t MemoryFacts::*field) {
  if (!facts.memory) {
    return pending(Reason::NOT_MEASURED);
  }
  return value_of((*facts.memory).*field);
}

Measured measure_stack(const Facts &facts, int32_t StackFacts::*field) {
  if (!facts.stacks) {
    return pending(Reason::NOT_MEASURED);
  }
  return value_of((*facts.stacks).*field);
}

// --- dispatch and judgement ---------------------------------------------------------------

Measured measure(Id id, const Facts &facts) {
  switch (id) {
  case Id::ADC_VALID:
    return measure_adc_valid(facts);
  case Id::CAL_SAVED:
    return measure_cal_saved(facts);
  case Id::CAL_SPAN:
    return measure_cal_span(facts);
  case Id::I2C_MISSING:
    return measure_i2c(facts);
  case Id::RESET_CLEAN:
    return measure_reset(facts);
  case Id::IMAGE_OK:
    return measure_image(facts);
  case Id::MEM_INT_MIN:
    return measure_memory(facts, &MemoryFacts::internal_free_min_b);
  case Id::MEM_INT_BLOCK:
    return measure_memory(facts, &MemoryFacts::internal_largest_b);
  case Id::MEM_DMA_MIN:
    return measure_memory(facts, &MemoryFacts::dma_free_min_b);
  case Id::MEM_PSRAM_FREE:
    return measure_memory(facts, &MemoryFacts::psram_free_b);
  case Id::STK_ADC:
    return measure_stack(facts, &StackFacts::adc_free_b);
  case Id::STK_UI:
    return measure_stack(facts, &StackFacts::ui_free_b);
  case Id::REST_X:
    return measure_rest(facts, &StickWindow::x, &Calibration::x);
  case Id::REST_Y:
    return measure_rest(facts, &StickWindow::y, &Calibration::y);
  case Id::REST_TWIST:
    return measure_rest(facts, &StickWindow::twist, &Calibration::twist);
  case Id::STILL_X:
    return measure_still(facts, &StickWindow::x);
  case Id::STILL_Y:
    return measure_still(facts, &StickWindow::y);
  case Id::STILL_TWIST:
    return measure_still(facts, &StickWindow::twist);
  case Id::BUTTON_IDLE:
    return measure_button(facts);
  case Id::COUNT:
  default:
    return invalid(Reason::BAD_FACTS);
  }
}

Result judge(const Check &row, const Measured &m) {
  switch (m.got) {
  case Got::PENDING:
    return Result{.id = row.id, .verdict = Verdict::PENDING, .reason = m.reason, .value = 0};
  case Got::VALUE:
    if (row.in_limits(m.value)) {
      return Result{.id = row.id, .verdict = Verdict::PASS, .reason = Reason::OK, .value = m.value};
    }
    if (m.reason != Reason::OK) {
      return Result{.id = row.id, .verdict = Verdict::FAIL, .reason = m.reason, .value = m.value};
    }
    return Result{.id = row.id,
                  .verdict = Verdict::FAIL,
                  .reason = m.value < row.lo ? Reason::BELOW_MIN : Reason::ABOVE_MAX,
                  .value = m.value};
  case Got::INVALID:
  default:
    return Result{.id = row.id, .verdict = Verdict::FAIL, .reason = m.reason, .value = m.value};
  }
}

/// From PENDING the latched state follows the evaluation; a value outside the enum is FAIL.
Overall follow(Overall evaluated) {
  switch (evaluated) {
  case Overall::PENDING:
    return Overall::PENDING;
  case Overall::PASS:
    return Overall::PASS;
  case Overall::FAIL:
  default:
    return Overall::FAIL;
  }
}

} // namespace

Result evaluate_check(Id id, const Facts &facts) noexcept {
  if (index_of(id) >= CHECK_COUNT) {
    return Result{.id = id, .verdict = Verdict::FAIL, .reason = Reason::BAD_FACTS, .value = 0};
  }
  return judge(check(id), measure(id, facts));
}

Report evaluate(const Facts &facts) noexcept {
  Report report;
  std::ranges::transform(CHECKS, report.results.begin(),
                         [&facts](const Check &row) { return evaluate_check(row.id, facts); });
  report.overall = overall_of(CHECKS, report.results);
  return report;
}

Overall overall_of(std::span<const Check> table, std::span<const Result> results) noexcept {
  if (table.empty() || table.size() != results.size()) { // nothing judged is never a PASS
    return Overall::FAIL;
  }
  bool waiting = false;
  for (std::size_t i = 0; i < table.size(); ++i) {
    const Check &row = table[i];
    if (results[i].id != row.id) {
      return Overall::FAIL;
    }
    if (row.need == Need::OPTIONAL) {
      continue;
    }
    switch (results[i].verdict) {
    case Verdict::PASS:
      break;
    case Verdict::PENDING:
      waiting = true;
      break;
    case Verdict::FAIL:
      if (row.kind != Kind::LIVE) { // a value outside the enum counts as LATCHED
        return Overall::FAIL;
      }
      waiting = true;
      break;
    default:
      return Overall::FAIL;
    }
  }
  return waiting ? Overall::PENDING : Overall::PASS;
}

Overall next_overall(Overall from, Overall evaluated) noexcept {
  switch (from) {
  case Overall::PENDING:
    return follow(evaluated);
  case Overall::PASS:
    return Overall::PASS;
  case Overall::FAIL:
  default:
    return Overall::FAIL;
  }
}

Report merge(const Report &previous, const Report &now) noexcept {
  if (previous.overall == Overall::PASS || previous.overall == Overall::FAIL) {
    return previous;
  }
  Report out;
  for (const auto &[row, kept, newer, slot] :
       std::views::zip(CHECKS, previous.results, now.results, out.results)) {
    const bool latched =
        row.kind == Kind::LATCHED && kept.id == row.id && kept.verdict != Verdict::PENDING;
    slot = latched ? kept : newer;
  }
  out.overall = next_overall(previous.overall, overall_of(CHECKS, out.results));
  return out;
}

std::optional<Id> blocking_check(const Report &report) noexcept {
  std::optional<Id> first_pending;
  for (const auto &[row, result] : std::views::zip(CHECKS, report.results)) {
    if (row.need == Need::OPTIONAL) {
      continue;
    }
    if (result.verdict == Verdict::FAIL) {
      return row.id;
    }
    if (result.verdict != Verdict::PASS && !first_pending) {
      first_pending = row.id;
    }
  }
  return first_pending;
}

std::string_view to_string(Verdict verdict) noexcept {
  switch (verdict) {
  case Verdict::PENDING:
    return "PENDING";
  case Verdict::PASS:
    return "PASS";
  case Verdict::FAIL:
    return "FAIL";
  default:
    return "?";
  }
}

std::string_view to_string(Overall overall) noexcept {
  switch (overall) {
  case Overall::PENDING:
    return "PENDING";
  case Overall::PASS:
    return "PASS";
  case Overall::FAIL:
    return "FAIL";
  default:
    return "?";
  }
}

std::string_view to_string(Reason reason) noexcept {
  switch (reason) {
  case Reason::OK:
    return "ok";
  case Reason::NOT_MEASURED:
    return "not_measured";
  case Reason::TOO_FEW_SAMPLES:
    return "too_few_samples";
  case Reason::BELOW_MIN:
    return "below_min";
  case Reason::ABOVE_MAX:
    return "above_max";
  case Reason::BAD_FACTS:
    return "bad_facts";
  case Reason::NOT_SAVED:
    return "not_saved";
  case Reason::DEVICE_MISSING:
    return "device_missing";
  case Reason::UNCLEAN_RESET:
    return "unclean_reset";
  case Reason::UNKNOWN_RESET:
    return "unknown_reset";
  case Reason::IMAGE_NOT_VERIFIED:
    return "image_not_verified";
  case Reason::IMAGE_STATE:
    return "image_state";
  case Reason::BUTTON_PRESSED:
    return "button_pressed";
  default:
    return "?";
  }
}

} // namespace hmi::post
