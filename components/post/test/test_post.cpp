// L1 tests for the quick POST evaluator (components/post, README REQ-POST-01..13). One
// behaviour per case; table-driven cases iterate CHECKS or a constexpr table (TS-UNIT-02).

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "fixtures.hpp"
#include "post/post.hpp"
#include "test_case.hpp"

namespace {

using namespace hmi::post;
using post_test::good_facts;
using post_test::set_measured;

constexpr int32_t I32_MIN = std::numeric_limits<int32_t>::min();
constexpr int32_t I32_MAX = std::numeric_limits<int32_t>::max();

// Names the check and the case in a failure message.
struct Where {
  std::array<char, 128> text{};
  Where(Id id, const char *what, long long value) {
    std::snprintf(text.data(), text.size(), "%.*s %s %lld", static_cast<int>(check(id).name.size()),
                  check(id).name.data(), what, value);
  }
  [[nodiscard]] const char *c_str() const { return text.data(); }
};

void expect(const Result &r, Id id, Verdict verdict, Reason reason, const Where &where) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(id), static_cast<int>(r.id), where.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(to_string(verdict).data(), to_string(r.verdict).data(),
                                   where.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(to_string(reason).data(), to_string(r.reason).data(),
                                   where.c_str());
}

const Result &result_of(const Report &report, Id id) { return report.results[index_of(id)]; }

bool is_window_check(Id id) { return id == Id::ADC_VALID || check(id).kind == Kind::LIVE; }

// The verdict a row gives `value` when its facts are otherwise healthy.
Result judged_at(Id id, int32_t value, bool &made) {
  Facts f = good_facts();
  made = set_measured(f, id, value);
  return evaluate_check(id, f);
}

// The FAIL reason a row gives a value outside its limits.
Reason fail_reason(const Check &row, int32_t value) {
  if (!row.is_yes_no() && row.id != Id::I2C_MISSING) {
    return value < row.lo ? Reason::BELOW_MIN : Reason::ABOVE_MAX;
  }
  switch (row.id) {
  case Id::CAL_SAVED:
    return Reason::NOT_SAVED;
  case Id::I2C_MISSING:
    return Reason::DEVICE_MISSING;
  case Id::RESET_CLEAN:
    return Reason::UNCLEAN_RESET;
  case Id::IMAGE_OK:
    return Reason::IMAGE_STATE;
  case Id::BUTTON_IDLE:
    return Reason::BUTTON_PRESSED;
  default:
    return Reason::BAD_FACTS;
  }
}

// One limit of one row: `value` must PASS, and `outside` (one step past it) must FAIL.
// Returns how many of the two could be built from facts.
int check_edge(const Check &row, int32_t value, int32_t outside) {
  int built = 0;
  bool made = false;
  const Result in = judged_at(row.id, value, made);
  if (made) {
    ++built;
    expect(in, row.id, Verdict::PASS, Reason::OK, Where(row.id, "edge", value));
    TEST_ASSERT_EQUAL_INT_MESSAGE(value, in.value, Where(row.id, "edge", value).c_str());
  }
  const Result out = judged_at(row.id, outside, made);
  if (made) {
    ++built;
    expect(out, row.id, Verdict::FAIL, fail_reason(row, outside), Where(row.id, "past", outside));
    TEST_ASSERT_EQUAL_INT_MESSAGE(outside, out.value, Where(row.id, "past", outside).c_str());
  }
  return built;
}

} // namespace

TEST_CASE("POST-001 the check table holds its invariants: ids in order, unique names, lo <= hi, "
          "the C3 latched/live split, latched rows first",
          "[post][table]") {
  TEST_ASSERT_TRUE(ids_match_rows());
  TEST_ASSERT_TRUE(rows_well_formed());
  TEST_ASSERT_TRUE(names_unique());
  TEST_ASSERT_TRUE(kinds_follow_c3());
  TEST_ASSERT_TRUE(latched_rows_first());
  TEST_ASSERT_TRUE(all_required());
  TEST_ASSERT_EQUAL_size_t(static_cast<std::size_t>(Id::COUNT), CHECK_COUNT);
  for (const Check &c : CHECKS) {
    TEST_ASSERT_EQUAL_PTR(&c, &check(c.id));
    TEST_ASSERT_TRUE_MESSAGE(c.is_yes_no() == (c.lo == 1 && c.hi == 1),
                             Where(c.id, "yes/no", 0).c_str());
  }
  // the latched set is exactly C3's hardware list
  for (const Id id : {Id::ADC_VALID, Id::CAL_SAVED, Id::CAL_SPAN, Id::I2C_MISSING, Id::RESET_CLEAN,
                      Id::IMAGE_OK, Id::MEM_INT_MIN, Id::MEM_INT_BLOCK, Id::MEM_DMA_MIN,
                      Id::MEM_PSRAM_FREE, Id::STK_ADC, Id::STK_UI}) {
    TEST_ASSERT_TRUE_MESSAGE(check(id).kind == Kind::LATCHED, Where(id, "latched", 0).c_str());
  }
}

TEST_CASE("POST-002 with no facts every check is PENDING, not measured, and the POST is PENDING",
          "[post]") {
  const Report r = evaluate(Facts{});
  for (const Check &c : CHECKS) {
    const Result &res = result_of(r, c.id);
    expect(res, c.id, Verdict::PENDING, Reason::NOT_MEASURED, Where(c.id, "empty", 0));
    TEST_ASSERT_EQUAL_INT(0, res.value);
  }
  TEST_ASSERT_EQUAL_STRING("PENDING", to_string(r.overall).data());
}

TEST_CASE("POST-003 a healthy boot passes every check with its measured value, and the POST "
          "passes",
          "[post]") {
  const Report r = evaluate(good_facts());
  for (const Check &c : CHECKS) {
    expect(result_of(r, c.id), c.id, Verdict::PASS, Reason::OK, Where(c.id, "healthy", 0));
  }
  TEST_ASSERT_EQUAL_INT(1000, result_of(r, Id::ADC_VALID).value);
  TEST_ASSERT_EQUAL_INT(1600, result_of(r, Id::CAL_SPAN).value);
  TEST_ASSERT_EQUAL_INT(1496, result_of(r, Id::STK_ADC).value);
  TEST_ASSERT_EQUAL_INT(2, result_of(r, Id::STILL_X).value);
  TEST_ASSERT_EQUAL_INT(60, result_of(r, Id::STILL_TWIST).value);
  TEST_ASSERT_EQUAL_STRING("PASS", to_string(r.overall).data());
  TEST_ASSERT_FALSE(blocking_check(r).has_value());
  const Report again = evaluate(good_facts()); // stateless: same facts, same report
  for (std::size_t i = 0; i < CHECK_COUNT; ++i) {
    TEST_ASSERT_EQUAL_INT(r.results[i].value, again.results[i].value);
    TEST_ASSERT_TRUE(r.results[i].verdict == again.results[i].verdict);
  }
}

TEST_CASE("POST-004 every row passes at its limits and fails one step past them, with its "
          "reason and value; a latched FAIL fails the POST, a live FAIL only delays it",
          "[post][table]") {
  for (const Check &row : CHECKS) {
    int built = check_edge(row, row.lo, row.lo == I32_MIN ? row.lo : row.lo - 1);
    if (row.hi != ANY_HI) {
      built += check_edge(row, row.hi, row.hi + 1);
    }
    // every row has a pass and a fail at a boundary
    TEST_ASSERT_TRUE_MESSAGE(built >= 2, Where(row.id, "edges built", built).c_str());

    Facts f = good_facts();
    const int32_t bad = row.hi != ANY_HI ? row.hi + 1 : row.lo - 1;
    bool made = set_measured(f, row.id, bad);
    if (!made) {
      made = set_measured(f, row.id, row.lo - 1);
    }
    TEST_ASSERT_TRUE_MESSAGE(made, Where(row.id, "fail case", bad).c_str());
    const Overall want = row.kind == Kind::LATCHED ? Overall::FAIL : Overall::PENDING;
    TEST_ASSERT_EQUAL_STRING_MESSAGE(to_string(want).data(), to_string(evaluate(f).overall).data(),
                                     Where(row.id, "overall", 0).c_str());
    TEST_ASSERT_TRUE_MESSAGE(blocking_check(evaluate(f)) == row.id,
                             Where(row.id, "blocking", 0).c_str());
  }
}

TEST_CASE("POST-005 each missing fact group makes exactly its own checks PENDING, so the POST "
          "waits",
          "[post]") {
  struct Group {
    void (*drop)(Facts &);
    bool (*uses)(Id);
  };
  constexpr std::array groups{
      Group{[](Facts &f) { f.window.reset(); }, [](Id id) { return is_window_check(id); }},
      Group{[](Facts &f) { f.cal.reset(); },
            [](Id id) {
              return id == Id::CAL_SAVED || id == Id::CAL_SPAN || id == Id::REST_X ||
                     id == Id::REST_Y || id == Id::REST_TWIST;
            }},
      Group{[](Facts &f) { f.i2c.reset(); }, [](Id id) { return id == Id::I2C_MISSING; }},
      Group{[](Facts &f) { f.reset.reset(); }, [](Id id) { return id == Id::RESET_CLEAN; }},
      Group{[](Facts &f) { f.image.reset(); }, [](Id id) { return id == Id::IMAGE_OK; }},
      Group{[](Facts &f) { f.memory.reset(); },
            [](Id id) {
              return id == Id::MEM_INT_MIN || id == Id::MEM_INT_BLOCK || id == Id::MEM_DMA_MIN ||
                     id == Id::MEM_PSRAM_FREE;
            }},
      Group{[](Facts &f) { f.stacks.reset(); },
            [](Id id) { return id == Id::STK_ADC || id == Id::STK_UI; }},
  };
  for (std::size_t g = 0; g < groups.size(); ++g) {
    Facts f = good_facts();
    groups[g].drop(f);
    const Report r = evaluate(f);
    for (const Check &c : CHECKS) {
      const bool uses = groups[g].uses(c.id);
      expect(result_of(r, c.id), c.id, uses ? Verdict::PENDING : Verdict::PASS,
             uses ? Reason::NOT_MEASURED : Reason::OK,
             Where(c.id, "group", static_cast<long long>(g)));
    }
    TEST_ASSERT_EQUAL_STRING("PENDING", to_string(r.overall).data());
  }
}

TEST_CASE("POST-006 a window shorter than WINDOW_MIN_SAMPLES is PENDING, never FAIL; at the "
          "minimum it is judged",
          "[post][window]") {
  const auto short_by = [](uint32_t n) {
    Facts f = good_facts();
    f.window->cycles = n;
    f.window->valid_cycles = 0;    // would fail adc.valid if it were judged
    f.window->button_idle = false; // would fail the button if it were judged
    for (hmi::post::AxisRest *a : {&f.window->x, &f.window->y, &f.window->twist}) {
      a->samples = n;
      a->max_mv = a->min_mv + 900; // would fail stillness if it were judged
    }
    return evaluate(f);
  };
  const auto min = static_cast<uint32_t>(WINDOW_MIN_SAMPLES);
  const Report too_short = short_by(min - 1);
  const Report enough = short_by(min);
  for (const Check &c : CHECKS) {
    if (!is_window_check(c.id) || c.id == Id::REST_X || c.id == Id::REST_Y ||
        c.id == Id::REST_TWIST) {
      continue; // REST_* stay inside min..max; they are covered by the samples gate below
    }
    expect(result_of(too_short, c.id), c.id, Verdict::PENDING, Reason::TOO_FEW_SAMPLES,
           Where(c.id, "samples", min - 1));
    TEST_ASSERT_TRUE_MESSAGE(result_of(enough, c.id).verdict == Verdict::FAIL,
                             Where(c.id, "samples", min).c_str());
  }
  TEST_ASSERT_EQUAL_STRING("PENDING", to_string(too_short.overall).data());
  TEST_ASSERT_EQUAL_STRING("FAIL", to_string(enough.overall).data()); // adc.valid latched
  Facts f = good_facts();
  f.window->x.samples = min - 1;
  expect(evaluate_check(Id::REST_X, f), Id::REST_X, Verdict::PENDING, Reason::TOO_FEW_SAMPLES,
         Where(Id::REST_X, "samples", min - 1));
  f.window->x.samples = min;
  expect(evaluate_check(Id::REST_X, f), Id::REST_X, Verdict::PASS, Reason::OK,
         Where(Id::REST_X, "samples", min));
}

TEST_CASE("POST-007 the ADC share rounds down, never up into the limit", "[post][window]") {
  Facts f = good_facts();
  f.window->cycles = 1001;
  f.window->valid_cycles = 991; // 990.0 per mille
  const Result at = evaluate_check(Id::ADC_VALID, f);
  TEST_ASSERT_EQUAL_INT(990, at.value);
  TEST_ASSERT_TRUE(at.verdict == Verdict::PASS);
  f.window->valid_cycles = 990; // 989.01 per mille
  const Result below = evaluate_check(Id::ADC_VALID, f);
  TEST_ASSERT_EQUAL_INT(989, below.value);
  expect(below, Id::ADC_VALID, Verdict::FAIL, Reason::BELOW_MIN, Where(Id::ADC_VALID, "", 989));
  f.window->cycles = 30;
  f.window->valid_cycles = 29; // one failed read in a 30-cycle window
  TEST_ASSERT_TRUE(evaluate_check(Id::ADC_VALID, f).verdict == Verdict::FAIL);
}

TEST_CASE("POST-008 more valid ADC cycles than cycles is BAD_FACTS and fails the POST",
          "[post][window]") {
  Facts f = good_facts();
  f.window->valid_cycles = f.window->cycles + 1;
  const Report r = evaluate(f);
  expect(result_of(r, Id::ADC_VALID), Id::ADC_VALID, Verdict::FAIL, Reason::BAD_FACTS,
         Where(Id::ADC_VALID, "valid>cycles", 0));
  TEST_ASSERT_EQUAL_STRING("FAIL", to_string(r.overall).data());
}

TEST_CASE("POST-009 an axis whose mean is outside its min..max is BAD_FACTS: its rest and "
          "stillness checks fail, which only delays the POST",
          "[post][window]") {
  for (const int which : {0, 1}) {
    Facts f = good_facts();
    if (which == 0) {
      f.window->y.min_mv = f.window->y.mean_mv + 1;
    } else {
      f.window->y.max_mv = f.window->y.mean_mv - 1;
    }
    const Report r = evaluate(f);
    for (const Id id : {Id::REST_Y, Id::STILL_Y}) {
      expect(result_of(r, id), id, Verdict::FAIL, Reason::BAD_FACTS, Where(id, "case", which));
    }
    TEST_ASSERT_EQUAL_STRING("PENDING", to_string(r.overall).data());
  }
}

TEST_CASE("POST-010 extreme facts saturate instead of overflowing (UBSan): offset, peak-to-peak "
          "and calibration span",
          "[post][hostile]") {
  Facts f = good_facts();
  f.window->x = AxisRest{.mean_mv = I32_MAX, .min_mv = I32_MIN, .max_mv = I32_MAX, .samples = 30};
  f.cal->x = AxisCal{.min_mv = I32_MAX, .centre_mv = I32_MIN, .max_mv = I32_MIN};
  const Report r = evaluate(f);
  expect(result_of(r, Id::REST_X), Id::REST_X, Verdict::FAIL, Reason::ABOVE_MAX,
         Where(Id::REST_X, "extreme", 0));
  TEST_ASSERT_EQUAL_INT(I32_MAX, result_of(r, Id::REST_X).value);
  TEST_ASSERT_EQUAL_INT(I32_MAX, result_of(r, Id::STILL_X).value);
  expect(result_of(r, Id::CAL_SPAN), Id::CAL_SPAN, Verdict::FAIL, Reason::BELOW_MIN,
         Where(Id::CAL_SPAN, "extreme", 0));
  TEST_ASSERT_EQUAL_INT(I32_MIN, result_of(r, Id::CAL_SPAN).value);
  TEST_ASSERT_EQUAL_STRING("FAIL", to_string(r.overall).data());
}

TEST_CASE("POST-011 the rest offset is absolute: as far below the centre fails as far above",
          "[post][window]") {
  Facts f = good_facts();
  f.window->twist.mean_mv = 1650 - REST_TWIST_MAX_MV;
  f.window->twist.min_mv = f.window->twist.mean_mv - 10;
  const Result at = evaluate_check(Id::REST_TWIST, f);
  TEST_ASSERT_EQUAL_INT(REST_TWIST_MAX_MV, at.value);
  TEST_ASSERT_TRUE(at.verdict == Verdict::PASS);
  f.window->twist.mean_mv -= 1;
  f.window->twist.min_mv -= 1;
  const Result past = evaluate_check(Id::REST_TWIST, f);
  TEST_ASSERT_EQUAL_INT(REST_TWIST_MAX_MV + 1, past.value);
  expect(past, Id::REST_TWIST, Verdict::FAIL, Reason::ABOVE_MAX, Where(Id::REST_TWIST, "", 0));
}

TEST_CASE("POST-012 a calibration whose centre is not between its ends, on any axis, fails "
          "the span",
          "[post][cal]") {
  using Member = AxisCal Calibration::*;
  for (const Member axis : {&Calibration::x, &Calibration::y, &Calibration::twist}) {
    Facts f = good_facts();
    (*f.cal).*axis = AxisCal{.min_mv = 1700, .centre_mv = 1650, .max_mv = 3300};
    const Result r = evaluate_check(Id::CAL_SPAN, f);
    TEST_ASSERT_EQUAL_INT(-50, r.value);
    expect(r, Id::CAL_SPAN, Verdict::FAIL, Reason::BELOW_MIN, Where(Id::CAL_SPAN, "order", -50));
    (*f.cal).*axis = AxisCal{.min_mv = 0, .centre_mv = 2400, .max_mv = 3300}; // short top
    TEST_ASSERT_EQUAL_INT(900, evaluate_check(Id::CAL_SPAN, f).value);
  }
}

TEST_CASE("POST-013 every ESP-IDF v6.0 reset reason is classified: power-on, pin, software, "
          "sleep, SDIO, USB and JTAG pass; panics, watchdogs, brownout, efuse, glitch, lock-up "
          "and unknown fail",
          "[post][reset]") {
  struct Row {
    ResetReason reason;
    Verdict verdict;
    Reason why;
  };
  constexpr std::array rows{
      Row{ResetReason::UNKNOWN, Verdict::FAIL, Reason::UNKNOWN_RESET},
      Row{ResetReason::POWERON, Verdict::PASS, Reason::OK},
      Row{ResetReason::EXT, Verdict::PASS, Reason::OK},
      Row{ResetReason::SW, Verdict::PASS, Reason::OK},
      Row{ResetReason::PANIC, Verdict::FAIL, Reason::UNCLEAN_RESET},
      Row{ResetReason::INT_WDT, Verdict::FAIL, Reason::UNCLEAN_RESET},
      Row{ResetReason::TASK_WDT, Verdict::FAIL, Reason::UNCLEAN_RESET},
      Row{ResetReason::WDT, Verdict::FAIL, Reason::UNCLEAN_RESET},
      Row{ResetReason::DEEPSLEEP, Verdict::PASS, Reason::OK},
      Row{ResetReason::BROWNOUT, Verdict::FAIL, Reason::UNCLEAN_RESET},
      Row{ResetReason::SDIO, Verdict::PASS, Reason::OK},
      Row{ResetReason::USB, Verdict::PASS, Reason::OK},
      Row{ResetReason::JTAG, Verdict::PASS, Reason::OK},
      Row{ResetReason::EFUSE, Verdict::FAIL, Reason::UNCLEAN_RESET},
      Row{ResetReason::PWR_GLITCH, Verdict::FAIL, Reason::UNCLEAN_RESET},
      Row{ResetReason::CPU_LOCKUP, Verdict::FAIL, Reason::UNCLEAN_RESET},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(i), static_cast<int>(rows[i].reason)); // all, in order
    Facts f = good_facts();
    f.reset = rows[i].reason;
    const Result r = evaluate_check(Id::RESET_CLEAN, f);
    expect(r, Id::RESET_CLEAN, rows[i].verdict, rows[i].why,
           Where(Id::RESET_CLEAN, "reason", static_cast<long long>(i)));
    TEST_ASSERT_EQUAL_INT(rows[i].verdict == Verdict::PASS ? 1 : 0, r.value);
  }
}

TEST_CASE("POST-014 a reset reason ESP-IDF v6.0 does not name fails as unknown", "[post][reset]") {
  for (const int32_t raw : {16, 17, 255, -1, I32_MIN, I32_MAX}) {
    Facts f = good_facts();
    f.reset = static_cast<ResetReason>(raw);
    expect(evaluate_check(Id::RESET_CLEAN, f), Id::RESET_CLEAN, Verdict::FAIL,
           Reason::UNKNOWN_RESET, Where(Id::RESET_CLEAN, "raw", raw));
  }
}

TEST_CASE("POST-015 the image passes only when verified and VALID, UNDEFINED or PENDING_VERIFY",
          "[post][image]") {
  struct Row {
    OtaImageState state;
    bool passes;
  };
  constexpr std::array rows{
      Row{OtaImageState::NEW, false},
      Row{OtaImageState::PENDING_VERIFY, true},
      Row{OtaImageState::VALID, true},
      Row{OtaImageState::INVALID, false},
      Row{OtaImageState::ABORTED, false},
      Row{OtaImageState::UNDEFINED, true},
      Row{static_cast<OtaImageState>(5U), false},
  };
  for (const Row &row : rows) {
    for (const bool verified : {true, false}) {
      Facts f = good_facts();
      f.image = ImageFacts{.verified = verified, .state = row.state};
      const Result r = evaluate_check(Id::IMAGE_OK, f);
      const Where where(Id::IMAGE_OK, verified ? "verified" : "unverified",
                        static_cast<long long>(row.state));
      if (!verified) {
        expect(r, Id::IMAGE_OK, Verdict::FAIL, Reason::IMAGE_NOT_VERIFIED, where);
      } else if (row.passes) {
        expect(r, Id::IMAGE_OK, Verdict::PASS, Reason::OK, where);
      } else {
        expect(r, Id::IMAGE_OK, Verdict::FAIL, Reason::IMAGE_STATE, where);
      }
    }
  }
}

TEST_CASE("POST-016 each expected I2C device missing alone fails; extra devices do not matter",
          "[post][i2c]") {
  for (std::size_t skip = 0; skip < EXPECTED_I2C.size(); ++skip) {
    Facts f = good_facts();
    I2cSet found;
    for (std::size_t i = 0; i < EXPECTED_I2C.size(); ++i) {
      if (i != skip) {
        found.add(EXPECTED_I2C[i]);
      }
    }
    f.i2c = found;
    const Result r = evaluate_check(Id::I2C_MISSING, f);
    TEST_ASSERT_EQUAL_INT(1, r.value);
    expect(r, Id::I2C_MISSING, Verdict::FAIL, Reason::DEVICE_MISSING,
           Where(Id::I2C_MISSING, "skip", EXPECTED_I2C[skip]));
  }
  Facts f = good_facts();
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    f.i2c->add(a); // every non-reserved address answers
  }
  TEST_ASSERT_TRUE(evaluate_check(Id::I2C_MISSING, f).verdict == Verdict::PASS);
  f.i2c = I2cSet{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EXPECTED_I2C.size()),
                        evaluate_check(Id::I2C_MISSING, f).value);
}

TEST_CASE("POST-017 the I2C set holds exactly the addresses added, and ignores bit 7",
          "[post][i2c]") {
  for (int added = 0; added < 128; ++added) {
    I2cSet s;
    s.add(static_cast<uint8_t>(added));
    for (int probe = 0; probe < 256; ++probe) {
      const bool want = (probe & 0x7F) == added;
      TEST_ASSERT_TRUE(s.has(static_cast<uint8_t>(probe)) == want);
    }
  }
}

TEST_CASE("POST-018 a pressed stick button fails the button check, which only delays the POST",
          "[post][window]") {
  Facts f = good_facts();
  f.window->button_idle = false;
  const Report r = evaluate(f);
  expect(result_of(r, Id::BUTTON_IDLE), Id::BUTTON_IDLE, Verdict::FAIL, Reason::BUTTON_PRESSED,
         Where(Id::BUTTON_IDLE, "pressed", 0));
  TEST_ASSERT_EQUAL_STRING("PENDING", to_string(r.overall).data());
  TEST_ASSERT_TRUE(blocking_check(r) == Id::BUTTON_IDLE);
}

namespace {
// A table to exercise what CHECKS cannot: OPTIONAL rows (none are in CHECKS today).
constexpr std::array<Check, 4> MIXED{
    Check{Id::ADC_VALID, "latched.req", "", 1, 1, Kind::LATCHED, Need::REQUIRED, "r"},
    Check{Id::CAL_SAVED, "latched.opt", "", 1, 1, Kind::LATCHED, Need::OPTIONAL, "o"},
    Check{Id::CAL_SPAN, "live.req", "", 1, 1, Kind::LIVE, Need::REQUIRED, "l"},
    Check{Id::I2C_MISSING, "live.opt", "", 1, 1, Kind::LIVE, Need::OPTIONAL, "lo"},
};
std::array<Result, 4> mixed(Verdict a, Verdict b, Verdict c, Verdict d) {
  return {Result{Id::ADC_VALID, a, Reason::OK, 0}, Result{Id::CAL_SAVED, b, Reason::OK, 0},
          Result{Id::CAL_SPAN, c, Reason::OK, 0}, Result{Id::I2C_MISSING, d, Reason::OK, 0}};
}
Overall over(Verdict a, Verdict b, Verdict c, Verdict d) {
  const auto r = mixed(a, b, c, d);
  return overall_of(MIXED, r);
}
} // namespace

TEST_CASE("POST-020 the overall verdict: a required latched FAIL fails, a required live FAIL or "
          "any required PENDING waits, OPTIONAL rows never count",
          "[post][overall]") {
  constexpr Verdict P = Verdict::PASS;
  constexpr Verdict F = Verdict::FAIL;
  constexpr Verdict W = Verdict::PENDING;
  TEST_ASSERT_TRUE(over(P, P, P, P) == Overall::PASS);
  TEST_ASSERT_TRUE(over(P, F, P, F) == Overall::PASS);    // optional fails are reported only
  TEST_ASSERT_TRUE(over(P, W, P, W) == Overall::PASS);    // optional pending does not wait
  TEST_ASSERT_TRUE(over(F, P, P, P) == Overall::FAIL);    // required latched FAIL
  TEST_ASSERT_TRUE(over(P, P, F, P) == Overall::PENDING); // required live FAIL only delays
  TEST_ASSERT_TRUE(over(W, P, P, P) == Overall::PENDING);
  TEST_ASSERT_TRUE(over(P, P, W, P) == Overall::PENDING);
  TEST_ASSERT_TRUE(over(W, P, F, P) == Overall::PENDING);
  TEST_ASSERT_TRUE(over(F, P, F, P) == Overall::FAIL); // latched FAIL beats a live one
  TEST_ASSERT_TRUE(over(P, P, W, F) == Overall::PENDING);
}

TEST_CASE("POST-021 results that do not line up with their table, an empty table, or values "
          "outside the enums fail the POST",
          "[post][overall][hostile]") {
  constexpr Verdict P = Verdict::PASS;
  auto r = mixed(P, P, P, P);
  TEST_ASSERT_TRUE(overall_of(std::span<const Check>(MIXED).first(3), r) == Overall::FAIL);
  TEST_ASSERT_TRUE(overall_of(std::span<const Check>{}, std::span<const Result>{}) ==
                   Overall::FAIL);
  r[2].id = Id::REST_X;
  TEST_ASSERT_TRUE(overall_of(MIXED, r) == Overall::FAIL);
  r = mixed(P, P, static_cast<Verdict>(7), P);
  TEST_ASSERT_TRUE(overall_of(MIXED, r) == Overall::FAIL);
  auto odd = MIXED;
  odd[2].kind = static_cast<Kind>(9); // an unknown kind counts as latched
  r = mixed(P, P, Verdict::FAIL, P);
  TEST_ASSERT_TRUE(overall_of(odd, r) == Overall::FAIL);
}

TEST_CASE("POST-022 next_overall follows OVERALL_TRANSITIONS for every pair, and goes to FAIL "
          "for a state outside the enum (oracle, TS-UNIT-08)",
          "[post][overall][oracle]") {
  for (const OverallTransition &t : OVERALL_TRANSITIONS) {
    TEST_ASSERT_TRUE(next_overall(t.from, t.evaluated) == t.to);
  }
  TEST_ASSERT_TRUE(overall_table_complete());
  TEST_ASSERT_TRUE(overall_ends_absorb());
  const auto odd = static_cast<Overall>(3);
  TEST_ASSERT_TRUE(next_overall(odd, Overall::PASS) == Overall::FAIL);
  TEST_ASSERT_TRUE(next_overall(Overall::PENDING, odd) == Overall::FAIL);
  TEST_ASSERT_TRUE(next_overall(Overall::PASS, odd) == Overall::PASS);
  TEST_ASSERT_TRUE(next_overall(Overall::FAIL, odd) == Overall::FAIL);
}

TEST_CASE("POST-023 merge keeps a latched PASS when later facts would fail it", "[post][merge]") {
  Facts f = good_facts();
  f.window->button_idle = false; // keeps the POST pending so merge keeps folding
  const Report first = merge(evaluate(Facts{}), evaluate(f));
  TEST_ASSERT_TRUE(first.overall == Overall::PENDING);
  f.memory->dma_free_min_b = 0;
  const Report second = merge(first, evaluate(f));
  TEST_ASSERT_TRUE(result_of(second, Id::MEM_DMA_MIN).verdict == Verdict::PASS);
  TEST_ASSERT_EQUAL_INT(2400, result_of(second, Id::MEM_DMA_MIN).value);
  TEST_ASSERT_TRUE(second.overall == Overall::PENDING);
}

TEST_CASE("POST-024 merge keeps a latched FAIL when later facts pass, and the POST stays FAIL",
          "[post][merge]") {
  Facts f = good_facts();
  f.reset = ResetReason::TASK_WDT;
  const Report failed = merge(evaluate(Facts{}), evaluate(f));
  TEST_ASSERT_TRUE(failed.overall == Overall::FAIL);
  const Report later = merge(failed, evaluate(good_facts()));
  TEST_ASSERT_TRUE(later.overall == Overall::FAIL);
  expect(result_of(later, Id::RESET_CLEAN), Id::RESET_CLEAN, Verdict::FAIL, Reason::UNCLEAN_RESET,
         Where(Id::RESET_CLEAN, "kept", 0));
  TEST_ASSERT_TRUE(blocking_check(later) == Id::RESET_CLEAN);
}

TEST_CASE("POST-025 a stick deflected at power-on delays the POST and names the check; released, "
          "the POST passes",
          "[post][merge]") {
  Facts f = good_facts();
  TEST_ASSERT_TRUE(set_measured(f, Id::REST_Y, 400)); // about 25 % forward
  Report latched = merge(evaluate(Facts{}), evaluate(f));
  for (int window = 0; window < 5; ++window) { // held for five windows: still only waiting
    latched = merge(latched, evaluate(f));
    TEST_ASSERT_TRUE(latched.overall == Overall::PENDING);
    TEST_ASSERT_TRUE(blocking_check(latched) == Id::REST_Y);
  }
  latched = merge(latched, evaluate(good_facts()));
  TEST_ASSERT_TRUE(latched.overall == Overall::PASS);
}

TEST_CASE("POST-026 once PASS or FAIL the POST holds until reset, whatever later windows show",
          "[post][merge]") {
  const Report passed = merge(evaluate(Facts{}), evaluate(good_facts()));
  TEST_ASSERT_TRUE(passed.overall == Overall::PASS);
  Facts moving = good_facts();
  TEST_ASSERT_TRUE(set_measured(moving, Id::REST_X, 900)); // driving after the POST
  moving.memory->internal_free_min_b = 0;
  const Report still_passed = merge(passed, evaluate(moving));
  TEST_ASSERT_TRUE(still_passed.overall == Overall::PASS);
  TEST_ASSERT_TRUE(result_of(still_passed, Id::REST_X).verdict == Verdict::PASS);

  Facts bad = good_facts();
  bad.cal->saved = false;
  const Report failed = merge(evaluate(Facts{}), evaluate(bad));
  TEST_ASSERT_TRUE(failed.overall == Overall::FAIL);
  TEST_ASSERT_TRUE(merge(failed, evaluate(good_facts())).overall == Overall::FAIL);
  Report odd = evaluate(good_facts());
  odd.overall = static_cast<Overall>(3);
  TEST_ASSERT_TRUE(merge(odd, evaluate(good_facts())).overall == Overall::FAIL);
}

TEST_CASE("POST-027 facts may arrive in any order: a latched PENDING takes the newer result",
          "[post][merge]") {
  Facts f = good_facts();
  f.image.reset(); // the image hash is still running
  Report latched = merge(evaluate(Facts{}), evaluate(f));
  TEST_ASSERT_TRUE(latched.overall == Overall::PENDING);
  TEST_ASSERT_TRUE(blocking_check(latched) == Id::IMAGE_OK);
  f.image = ImageFacts{.verified = false, .state = OtaImageState::VALID};
  latched = merge(latched, evaluate(f));
  TEST_ASSERT_TRUE(latched.overall == Overall::FAIL);
  expect(result_of(latched, Id::IMAGE_OK), Id::IMAGE_OK, Verdict::FAIL, Reason::IMAGE_NOT_VERIFIED,
         Where(Id::IMAGE_OK, "late", 0));
}

TEST_CASE("POST-028 the blocking check is the first FAIL in table order, else the first "
          "PENDING, else none",
          "[post][blocking]") {
  Facts f = good_facts();
  f.stacks.reset();              // STK_* pending
  f.window->button_idle = false; // live FAIL, later in the table
  TEST_ASSERT_TRUE(blocking_check(evaluate(f)) == Id::BUTTON_IDLE);
  f.window->button_idle = true;
  TEST_ASSERT_TRUE(blocking_check(evaluate(f)) == Id::STK_ADC);
  f.window->button_idle = false;
  f.i2c = I2cSet{}; // latched FAIL, earlier in the table than the button
  TEST_ASSERT_TRUE(blocking_check(evaluate(f)) == Id::I2C_MISSING);
  TEST_ASSERT_FALSE(blocking_check(evaluate(good_facts())).has_value());
}

TEST_CASE("POST-029 evaluating an id that is not a check fails with BAD_FACTS", "[post][hostile]") {
  for (const uint8_t raw : {static_cast<uint8_t>(Id::COUNT), uint8_t{200}, uint8_t{255}}) {
    const Result r = evaluate_check(static_cast<Id>(raw), good_facts());
    TEST_ASSERT_TRUE(r.verdict == Verdict::FAIL);
    TEST_ASSERT_TRUE(r.reason == Reason::BAD_FACTS);
  }
}

TEST_CASE("POST-032 every verdict, overall state and reason has its own name; a value outside "
          "the enum reads '?'",
          "[post][names]") {
  constexpr std::array verdicts{Verdict::PENDING, Verdict::PASS, Verdict::FAIL};
  constexpr std::array overalls{Overall::PENDING, Overall::PASS, Overall::FAIL};
  TEST_ASSERT_TRUE(to_string(Verdict::PASS) == to_string(Overall::PASS));
  for (std::size_t i = 0; i < verdicts.size(); ++i) {
    TEST_ASSERT_TRUE(to_string(verdicts[i]) == to_string(overalls[i]));
    for (std::size_t j = i + 1; j < verdicts.size(); ++j) {
      TEST_ASSERT_TRUE(to_string(verdicts[i]) != to_string(verdicts[j]));
    }
  }
  constexpr int reason_count = static_cast<int>(Reason::BUTTON_PRESSED) + 1;
  for (int i = 0; i < reason_count; ++i) {
    const std::string_view a = to_string(static_cast<Reason>(i));
    TEST_ASSERT_TRUE(a != "?");
    for (int j = i + 1; j < reason_count; ++j) {
      TEST_ASSERT_TRUE(a != to_string(static_cast<Reason>(j)));
    }
  }
  TEST_ASSERT_TRUE(to_string(static_cast<Reason>(reason_count)) == "?");
  TEST_ASSERT_TRUE(to_string(static_cast<Verdict>(3)) == "?");
  TEST_ASSERT_TRUE(to_string(static_cast<Overall>(3)) == "?");
}

TEST_CASE("POST-033 merge does not keep a latched result whose id does not match its row",
          "[post][merge][hostile]") {
  Facts f = good_facts();
  f.window->button_idle = false; // keeps the POST pending
  Report previous = merge(evaluate(Facts{}), evaluate(f));
  previous.results[index_of(Id::RESET_CLEAN)].id = Id::IMAGE_OK; // corrupted
  previous.results[index_of(Id::RESET_CLEAN)].verdict = Verdict::FAIL;
  const Report merged = merge(previous, evaluate(f));
  expect(result_of(merged, Id::RESET_CLEAN), Id::RESET_CLEAN, Verdict::PASS, Reason::OK,
         Where(Id::RESET_CLEAN, "re-judged", 0));
  TEST_ASSERT_TRUE(merged.overall == Overall::PENDING);
}
