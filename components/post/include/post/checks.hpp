#pragma once
// The quick POST's check table: one row per check (TS-POST-03, CS-TYP-03). THIS IS THE SPEC;
// src/post.cpp measures each row and the README states the requirements.
//
// Two kinds of check (docs/plans/hazard-fixes.md section 4 C3):
//   LATCHED  hardware: judged once its facts are in, and a FAIL holds until the next reset.
//   LIVE     the stick at rest: re-judged on every rest window. A FAIL only delays the POST,
//            so a stick bumped at power-on never locks the user out.
//
// Limits marked "D4" are placeholders until the owner decides them (hazard-fixes section 1 D4).
// Each picks the stricter side of what is known today, and says where it comes from.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace hmi::post {

/// No upper limit.
inline constexpr int32_t ANY_HI = std::numeric_limits<int32_t>::max();

// --- limits --------------------------------------------------------------------------------

/// D4: shortest rest window judged, in ADC task cycles (and samples per axis). About 0.8 s at
/// today's 33 ms cycle; hazard-fixes B2 asks for about 1 s. Fewer is PENDING, never FAIL.
inline constexpr int32_t WINDOW_MIN_SAMPLES = 25;
/// D4: least share of ADC cycles that read all three axes, in 0.1 %. 990 = the self test's
/// joy.valid 99 %, which allows no failed read in a window under 100 cycles.
inline constexpr int32_t ADC_VALID_MIN_PERMILLE = 990;
/// Least travel either side of centre for a calibration to count (joystick_cal.cpp's
/// kFullTravelMv: the firmware already refuses a saved file with less).
inline constexpr int32_t CAL_MIN_HALF_SPAN_MV = 1000;
/// D4: the internal bus devices expected at boot: board 2's boot scan, every bench boot of
/// 2026-10-06 (C:/b/bench/results/*/boot-candidate.log). Not yet split into required and
/// board-optional (board 1 has no haptic answering, 0x5a).
inline constexpr std::array<uint8_t, 11> EXPECTED_I2C{0x10, 0x28, 0x32, 0x36, 0x40, 0x41,
                                                      0x43, 0x44, 0x55, 0x5a, 0x68};
/// D4: least internal RAM free since boot (the self test's mem.int_min, 12 KB).
inline constexpr int32_t MEM_INT_MIN_B = 12 * 1024;
/// D4: least largest free internal block (the self test's mem.int_block, 12 KB).
inline constexpr int32_t MEM_INT_BLOCK_B = 12 * 1024;
/// D4: least DMA-capable RAM free since boot (the self test's mem.dma_min, 1536 B).
inline constexpr int32_t MEM_DMA_MIN_B = 1536;
/// D4: least PSRAM free (the self test's mem.psram_free, 8 MiB).
inline constexpr int32_t MEM_PSRAM_FREE_B = 8 * 1024 * 1024;
/// D4: least stack never used by the ADC task (the self test's mem.stk_adc; 1496 B was
/// measured free at the end of a self test on board 2, 2026-10-06).
inline constexpr int32_t STK_ADC_MIN_B = 1024;
/// D4: least stack never used by the LVGL task (the self test's mem.stk_lvgl).
inline constexpr int32_t STK_UI_MIN_B = 2048;
/// D4: most X or Y rest offset from the calibrated centre, mV (the self test's
/// joy.*_cal_off; inside the stick's 0.10 radial dead zone, see the static_asserts).
inline constexpr int32_t REST_XY_MAX_MV = 40;
/// D4: most twist rest offset from the calibrated centre, mV (the self test's
/// joy.twist_cal_off; inside the 60 mV twist centre dead band).
inline constexpr int32_t REST_TWIST_MAX_MV = 50;
/// D4: most X or Y peak-to-peak over the window, mV (the self test's joy.*_noise, ~1 mV
/// measured): a hand moving the stick through centre is not at rest.
inline constexpr int32_t STILL_XY_MAX_MV = 30;
/// D4: most twist peak-to-peak over the window, mV (the self test's joy.twist_noise; the
/// twist pot reads ~60 mV peak-to-peak at rest on ADC2).
inline constexpr int32_t STILL_TWIST_MAX_MV = 120;

/// The stick's centre dead zone, in 0.1 % of the half-span (main: kStickCenterDeadzoneRadius
/// 0.10). Used only to prove REST_XY_MAX_MV sits inside it.
inline constexpr int32_t STICK_DEAD_ZONE_PERMILLE = 100;
/// The twist centre dead band, mV (main: kTwistCenterDeadbandMv). Used only to prove
/// REST_TWIST_MAX_MV sits inside it.
inline constexpr int32_t TWIST_DEAD_BAND_MV = 60;

// --- the table -----------------------------------------------------------------------------

/// One check, in CHECKS' order: the position is the report's index. Hand-written, and the
/// static_asserts below prove it lists exactly CHECKS' rows in CHECKS' order, then COUNT.
enum class Id : uint8_t {
  ADC_VALID,
  CAL_SAVED,
  CAL_SPAN,
  I2C_MISSING,
  RESET_CLEAN,
  IMAGE_OK,
  MEM_INT_MIN,
  MEM_INT_BLOCK,
  MEM_DMA_MIN,
  MEM_PSRAM_FREE,
  STK_ADC,
  STK_UI,
  REST_X,
  REST_Y,
  REST_TWIST,
  STILL_X,
  STILL_Y,
  STILL_TWIST,
  BUTTON_IDLE,
  COUNT, ///< not a check: how many there are
};

/// Latched hardware check, or live stick-at-rest guard (see the file comment).
enum class Kind : uint8_t {
  LATCHED, ///< a FAIL holds until the next reset
  LIVE,    ///< a FAIL only delays; re-judged on every window
};

/// What the check counts for in the overall verdict (TS-POST-04).
enum class Need : uint8_t {
  REQUIRED, ///< not PASS keeps the overall verdict from PASS
  OPTIONAL, ///< reported only
};

/// One row of the spec (TS-POST-03).
struct Check {
  Id id;
  std::string_view name; ///< "mem.int_min": the report's name
  std::string_view unit; ///< "B"; empty for a count or a yes/no check
  int32_t lo;            ///< inclusive
  int32_t hi;            ///< inclusive; ANY_HI = no upper limit
  Kind kind;
  Need need;
  std::string_view why; ///< what a PASS proves

  /// @brief PASS when lo <= value <= hi.
  /// @param value the measurement, in `unit`
  /// @return true for PASS
  [[nodiscard]] constexpr bool in_limits(int32_t value) const noexcept {
    return value >= lo && value <= hi;
  }
  /// @brief A yes/no check: lo == hi == 1 and no unit; it measures 1 (yes) or 0 (no).
  /// @return true for a yes/no check
  [[nodiscard]] constexpr bool is_yes_no() const noexcept {
    return lo == 1 && hi == 1 && unit.empty();
  }
};

// clang-format off
inline constexpr std::array CHECKS{
  // latched: hardware
  Check{Id::ADC_VALID,      "adc.valid",         "0.1%", ADC_VALID_MIN_PERMILLE, 1000, Kind::LATCHED, Need::REQUIRED, "ADC cycles that read all three axes"},
  Check{Id::CAL_SAVED,      "joy.cal_saved",     "",     1,                    1,      Kind::LATCHED, Need::REQUIRED, "Stick calibration loaded from flash"},
  Check{Id::CAL_SPAN,       "joy.cal_span",      "mV",   CAL_MIN_HALF_SPAN_MV, ANY_HI, Kind::LATCHED, Need::REQUIRED, "Least travel either side of a calibrated centre"},
  Check{Id::I2C_MISSING,    "i2c.missing",       "",     0,                    0,      Kind::LATCHED, Need::REQUIRED, "Every expected internal bus device answered"},
  Check{Id::RESET_CLEAN,    "sys.clean_reset",   "",     1,                    1,      Kind::LATCHED, Need::REQUIRED, "Last reset not a panic, watchdog, brownout or unknown"},
  Check{Id::IMAGE_OK,       "img.ok",            "",     1,                    1,      Kind::LATCHED, Need::REQUIRED, "Running image verified and in a bootable OTA state"},
  Check{Id::MEM_INT_MIN,    "mem.int_min",       "B",    MEM_INT_MIN_B,        ANY_HI, Kind::LATCHED, Need::REQUIRED, "Least internal RAM free since boot"},
  Check{Id::MEM_INT_BLOCK,  "mem.int_block",     "B",    MEM_INT_BLOCK_B,      ANY_HI, Kind::LATCHED, Need::REQUIRED, "Largest free internal block"},
  Check{Id::MEM_DMA_MIN,    "mem.dma_min",       "B",    MEM_DMA_MIN_B,        ANY_HI, Kind::LATCHED, Need::REQUIRED, "Least DMA-capable RAM free since boot"},
  Check{Id::MEM_PSRAM_FREE, "mem.psram_free",    "B",    MEM_PSRAM_FREE_B,     ANY_HI, Kind::LATCHED, Need::REQUIRED, "PSRAM free"},
  Check{Id::STK_ADC,        "stk.adc",           "B",    STK_ADC_MIN_B,        ANY_HI, Kind::LATCHED, Need::REQUIRED, "ADC task stack never used"},
  Check{Id::STK_UI,         "stk.ui",            "B",    STK_UI_MIN_B,         ANY_HI, Kind::LATCHED, Need::REQUIRED, "LVGL task stack never used"},
  // live: the stick at rest
  Check{Id::REST_X,         "joy.x_cal_off",     "mV",   0,                    REST_XY_MAX_MV,     Kind::LIVE, Need::REQUIRED, "X rests at its calibrated centre"},
  Check{Id::REST_Y,         "joy.y_cal_off",     "mV",   0,                    REST_XY_MAX_MV,     Kind::LIVE, Need::REQUIRED, "Y rests at its calibrated centre"},
  Check{Id::REST_TWIST,     "joy.twist_cal_off", "mV",   0,                    REST_TWIST_MAX_MV,  Kind::LIVE, Need::REQUIRED, "Twist rests at its calibrated centre"},
  Check{Id::STILL_X,        "joy.x_noise",       "mV",   0,                    STILL_XY_MAX_MV,    Kind::LIVE, Need::REQUIRED, "X still over the window"},
  Check{Id::STILL_Y,        "joy.y_noise",       "mV",   0,                    STILL_XY_MAX_MV,    Kind::LIVE, Need::REQUIRED, "Y still over the window"},
  Check{Id::STILL_TWIST,    "joy.twist_noise",   "mV",   0,                    STILL_TWIST_MAX_MV, Kind::LIVE, Need::REQUIRED, "Twist still over the window"},
  Check{Id::BUTTON_IDLE,    "joy.button_idle",   "",     1,                    1,                  Kind::LIVE, Need::REQUIRED, "Stick button released over the window"},
};
// clang-format on

/// How many checks there are.
inline constexpr std::size_t CHECK_COUNT = CHECKS.size();

/// @brief A check's position in CHECKS, which is also its report index.
/// @param id the check
/// @return its index
[[nodiscard]] constexpr std::size_t index_of(Id id) noexcept {
  return static_cast<std::size_t>(id);
}

/// @brief A check's row.
/// @param id the check; COUNT is not one
/// @return its row of CHECKS
[[nodiscard]] constexpr const Check &check(Id id) noexcept { return CHECKS[index_of(id)]; }

// --- invariants (POST-001 tests them again at run time) ------------------------------------

/// @brief Each row's id is its position, and Id has one enumerator per row.
/// @return true when Id and CHECKS agree, in count and order
consteval bool ids_match_rows() {
  for (std::size_t i = 0; i < CHECKS.size(); ++i) {
    if (index_of(CHECKS[i].id) != i) {
      return false;
    }
  }
  return index_of(Id::COUNT) == CHECKS.size();
}
static_assert(ids_match_rows(), "Id and CHECKS disagree: same checks, same order");

/// @brief Every row has lo <= hi, a name and a why; a yes/no row is exactly lo == hi == 1.
/// @return true when every row is well formed
consteval bool rows_well_formed() {
  return std::all_of(CHECKS.begin(), CHECKS.end(), [](const Check &c) {
    const bool kind_ok = c.kind == Kind::LATCHED || c.kind == Kind::LIVE;
    const bool need_ok = c.need == Need::REQUIRED || c.need == Need::OPTIONAL;
    return c.lo <= c.hi && !c.name.empty() && !c.why.empty() && kind_ok && need_ok;
  });
}
static_assert(rows_well_formed(), "a CHECKS row has lo > hi, no name or why, or a bad kind/need");

/// @brief No two rows share a name: tools key the report on it.
/// @return true when the names are unique
consteval bool names_unique() {
  for (std::size_t i = 0; i < CHECKS.size(); ++i) {
    for (std::size_t j = i + 1; j < CHECKS.size(); ++j) {
      if (CHECKS[i].name == CHECKS[j].name) {
        return false;
      }
    }
  }
  return true;
}
static_assert(names_unique(), "two CHECKS rows share a name");

/// @brief The C3 split: a check is LIVE exactly when it is part of the stick-at-rest guard.
/// @param id the check
/// @return true for the rest offsets, the stillness and the button
constexpr bool is_stick_at_rest(Id id) {
  return id == Id::REST_X || id == Id::REST_Y || id == Id::REST_TWIST || id == Id::STILL_X ||
         id == Id::STILL_Y || id == Id::STILL_TWIST || id == Id::BUTTON_IDLE;
}

/// @brief Every hardware check latches and every stick-at-rest check is live (C3), and every
/// live check is REQUIRED (an optional delay would delay nothing).
/// @return true when the kinds follow the split
consteval bool kinds_follow_c3() {
  return std::all_of(CHECKS.begin(), CHECKS.end(), [](const Check &c) {
    const bool live = c.kind == Kind::LIVE;
    return live == is_stick_at_rest(c.id) && (!live || c.need == Need::REQUIRED);
  });
}
static_assert(kinds_follow_c3(), "a hardware check is LIVE, or a stick-at-rest check LATCHED");

/// @brief Every check counts toward the overall verdict today.
/// @return true when no row is OPTIONAL
consteval bool all_required() {
  return std::all_of(CHECKS.begin(), CHECKS.end(),
                     [](const Check &c) { return c.need == Need::REQUIRED; });
}
static_assert(all_required(), "an OPTIONAL row needs an owner decision (README, D4)");

/// @brief Every LATCHED row comes before every LIVE row, so a hardware FAIL is named before a
/// stick that is not at rest (blocking_check takes the first FAIL in table order).
/// @return true when no LATCHED row follows a LIVE one
consteval bool latched_rows_first() {
  bool seen_live = false;
  for (const Check &c : CHECKS) {
    if (c.kind == Kind::LIVE) {
      seen_live = true;
    } else if (seen_live) {
      return false;
    }
  }
  return true;
}
static_assert(latched_rows_first(), "list the LATCHED rows before the LIVE ones");

static_assert(REST_XY_MAX_MV < STICK_DEAD_ZONE_PERMILLE * CAL_MIN_HALF_SPAN_MV / 1000,
              "the X/Y rest limit must sit inside the dead zone of the shortest accepted travel");
static_assert(REST_TWIST_MAX_MV < TWIST_DEAD_BAND_MV,
              "the twist rest limit must sit inside the twist dead band");
static_assert(WINDOW_MIN_SAMPLES > 0, "an empty window must never be judged");
static_assert(ADC_VALID_MIN_PERMILLE > 0 && ADC_VALID_MIN_PERMILLE <= 1000,
              "adc.valid is a share in 0.1 %");

} // namespace hmi::post
