#pragma once
// The code under test, behind one seam so the golden cases never change: the hmi_format
// component. (Before the move this seam called the verbatim pre-move copy, legacy_format.cpp,
// which stays as the oracle of the differential cases.)

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <span>

#include "hmi_format/diag.hpp"
#include "hmi_format/speed.hpp"
#include "hmi_format/stepper.hpp"
#include "hmi_format/topbar.hpp"
#include "specs.hpp"

namespace sut {

inline int32_t speed_display_tenths(float mps) { return hmi::format::speed_display_tenths(mps); }

inline void speed_text(int32_t tenths, char *out, std::size_t out_size) {
  hmi::format::speed_text(tenths, std::span<char>(out, out_size));
}

inline void stepper_format(const fmt_test::Row &row, int32_t raw, char *out, std::size_t size) {
  hmi::format::stepper_format(fmt_test::make_spec<hmi::format::StepperSpec>(row), raw,
                              std::span<char>(out, size));
}

inline void seat_format(const fmt_test::Row &row, int32_t raw, char *out, std::size_t size) {
  hmi::format::seat_format(fmt_test::make_spec<hmi::format::StepperSpec>(row), raw,
                           std::span<char>(out, size));
}

inline void seat_angle_texts(const fmt_test::Row &row, int32_t raw, char (&text)[32],
                             char (&footer)[40]) {
  const auto spec = fmt_test::make_spec<hmi::format::StepperSpec>(row);
  hmi::format::seat_reading_text(spec, raw, text);
  hmi::format::seat_range_text(spec, footer);
}

inline bool clock_plausible(const std::tm &t) { return hmi::format::clock_plausible(t); }

inline void clock_text(const std::tm &t, char (&text)[8]) { hmi::format::clock_text(t, text); }

inline const char *link_text(uint8_t link) {
  return hmi::format::link_text(static_cast<hmi::format::Link>(link));
}

inline void diag_rate_text(int32_t rate, char *out, std::size_t out_size) {
  hmi::format::diag_rate_text(rate, std::span<char>(out, out_size));
}

} // namespace sut
