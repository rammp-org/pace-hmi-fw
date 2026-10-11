#pragma once

/**
 * @file cal_record.hpp
 * @brief The joystick calibration record: three raw-mV numbers per axis, the text file it is
 *        kept in, and the check a record must pass before the stick uses it.
 *
 * Plain C++ (no LVGL, no storage, no logging): the caller opens the file, logs, and decides
 * what to fall back to. The file format, version 1, one record per file:
 *
 *     # joystick calibration, raw ADC mV: min center max
 *     version 1
 *     horizontal 11.0 1507.0 2971.0
 *     vertical 6.0 1510.0 2962.0
 *     twist 10.0 1477.0 2960.0
 *
 * Words are separated by any whitespace; a word starting with `#` starts a comment to the end
 * of its line, where a word is expected (before `version` and before each axis name). Numbers
 * are read with `std::istream >> float`. Anything after the twist line is ignored.
 *
 * Safety (CS-SAF-01): the record sets the stick's travel. Its behaviour is pinned by
 * components/joystick_cal/test (CAL-xxx). check() is the one test a record passes before the
 * stick uses it, at boot and at the end of a run (hazard fix C2, REQ-CAL-09: it gained the upper
 * bound and finiteness plausible() lacked, plan H3). A word in the file has no length limit.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>

namespace hmi::cal {

/// One axis's travel in raw ADC millivolts; min < center < max. Which physical direction
/// reads which end is the caller's business.
struct AxisCal {
  float min_mv;
  float center_mv;
  float max_mv;
};

inline constexpr std::size_t kAxisCount = 3;

/// Indexed horizontal, vertical, twist (main's JoystickAxis).
using Record = std::array<AxisCal, kAxisCount>;

/// The file's format version: the number after the word `version`.
inline constexpr int kFileVersion = 1;
/// The file's name in the storage root.
inline constexpr std::string_view kFileName = "joystick_cal.txt";
/// Each axis's word in the file and in describe(), in Record order.
inline constexpr std::array<std::string_view, kAxisCount> kAxisNames{"horizontal", "vertical",
                                                                     "twist"};
/// The least travel, each way from rest, a record may claim (inclusive). Also how far past
/// rest the calibration run counts as pushed "fully".
inline constexpr float kFullTravelMv = 1000.0f;

/// Why decode() rejected a file; NONE after a successful decode. message() is the text the
/// firmware logs after "<path>: ". A plain enum, not a std::error_code: no error_category
/// singleton, so no lazy static (app-main-shrink V6).
enum class DecodeError {
  NONE = 0,          ///< decoded
  NOT_VERSION_1 = 1, ///< "not a version 1 calibration file"
  NO_HORIZONTAL,     ///< "expected a 'horizontal min center max' line"
  NO_VERTICAL,       ///< "expected a 'vertical min center max' line"
  NO_TWIST,          ///< "expected a 'twist min center max' line"
};

/// The log text for `error`; "unknown joystick_cal error" for NONE or a value outside the
/// enum.
[[nodiscard]] std::string message(DecodeError error);

/// "horizontal 11/1507/2971, vertical 6/1510/2962, twist 10/1477/2960 mV": each number
/// rounded to whole mV. The boot log line the bench checks is built from this.
[[nodiscard]] std::string describe(const Record &record);

/// What check() found wrong with a record, in the order it looks (per axis, Record order).
enum class RecordFault : std::uint8_t {
  NONE,        ///< valid
  NOT_FINITE,  ///< a NaN or an infinity among the axis's three numbers
  BELOW_ZERO,  ///< min below 0 mV (the ADC cannot read it)
  TRAVEL,      ///< less than kFullTravelMv from rest to an end (or an end on the wrong side)
  ABOVE_LIMIT, ///< max + CAL_OVERSHOOT_MV above HIGH_RAIL_MV (max above 3050 mV)
};

/// check()'s answer: the first fault and the axis it is on (index into Record; 0 when NONE).
struct RecordCheck {
  RecordFault fault = RecordFault::NONE;
  std::size_t axis = 0;
};

/// Hazard fix C2 (docs/plans/hazard-c2-spec.md §6, REQ-CAL-09): the first reason a record may
/// not be used, axis by axis in Record order and, on one axis, in RecordFault's order. A record
/// passes exactly when all nine numbers are finite and, per axis, min >= 0,
/// center - min >= kFullTravelMv, max - center >= kFullTravelMv and
/// max + CAL_OVERSHOOT_MV <= HIGH_RAIL_MV (that is, max <= 3050 mV; stick/stick_monitor.hpp).
[[nodiscard]] RecordCheck check(const Record &record);

/// Whether a record may be used: check() found nothing. Boot uses only a valid saved record,
/// and a run hands over only a valid one (REQ-CAL-09).
[[nodiscard]] bool valid(const Record &record);

/// The file's text for `record`, numbers to 0.1 mV.
[[nodiscard]] std::string encode(const Record &record);

/// Reads one record from `in`. On success fills `out`, sets `error` to NONE and returns true;
/// otherwise leaves `out` as it was, sets `error` to the reason and returns false. Does not
/// check() the record.
[[nodiscard]] bool decode(std::istream &in, Record &out, DecodeError &error);

} // namespace hmi::cal
