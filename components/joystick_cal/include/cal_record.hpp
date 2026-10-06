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
 * Safety (CS-SAF-01): the record sets the stick's travel. Its behaviour as it is today is
 * pinned by components/joystick_cal/test (CAL-xxx), hazards included: plausible() has no upper
 * bound (plan H3), and a word in the file has no length limit.
 */

#include <array>
#include <cstddef>
#include <istream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

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

/// Why decode() rejected a file. message() is the text the firmware logs after "<path>: ".
enum class DecodeError {
  NOT_VERSION_1 = 1, ///< "not a version 1 calibration file"
  NO_HORIZONTAL,     ///< "expected a 'horizontal min center max' line"
  NO_VERTICAL,       ///< "expected a 'vertical min center max' line"
  NO_TWIST,          ///< "expected a 'twist min center max' line"
};

const std::error_category &decode_category() noexcept;
std::error_code make_error_code(DecodeError e) noexcept;

/// "horizontal 11/1507/2971, vertical 6/1510/2962, twist 10/1477/2960 mV": each number
/// rounded to whole mV. The boot log line the bench checks is built from this.
[[nodiscard]] std::string describe(const Record &record);

/// Whether every axis has at least kFullTravelMv each way from rest. No upper bound: see the
/// file comment.
[[nodiscard]] bool plausible(const Record &record);

/// The file's text for `record`, numbers to 0.1 mV.
[[nodiscard]] std::string encode(const Record &record);

/// Reads one record from `in`. On success fills `out` and returns true; otherwise leaves
/// `out` as it was, sets `ec` to a DecodeError and returns false. Does not check plausible().
[[nodiscard]] bool decode(std::istream &in, Record &out, std::error_code &ec);

} // namespace hmi::cal

template <> struct std::is_error_code_enum<hmi::cal::DecodeError> : std::true_type {};
