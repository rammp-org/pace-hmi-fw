#pragma once
// The code under test, behind one seam so the golden cases never change: before the move it is
// the verbatim pre-move copy (legacy_format.cpp).

#include <cstddef>
#include <cstdint>
#include <ctime>

#include "legacy_format.hpp"
#include "specs.hpp"

namespace sut {

inline int32_t speed_display_tenths(float mps) { return legacy::speed_display_tenths(mps); }

inline void speed_text(int32_t tenths, char *out, std::size_t out_size) {
  legacy::speed_label_text(tenths, out, out_size);
}

inline void stepper_format(const fmt_test::Row &row, int32_t raw, char *out, std::size_t size) {
  legacy::stepper_format(fmt_test::make_spec<legacy::StepperSpec>(row), raw, out, size);
}

inline void seat_format(const fmt_test::Row &row, int32_t raw, char *out, std::size_t size) {
  legacy::seat_format(fmt_test::make_spec<legacy::StepperSpec>(row), raw, out, size);
}

inline void seat_angle_texts(const fmt_test::Row &row, int32_t raw, char (&text)[32],
                             char (&footer)[40]) {
  legacy::seat_angle_texts(fmt_test::make_spec<legacy::StepperSpec>(row), raw, text, footer);
}

inline bool clock_plausible(const std::tm &t) { return legacy::clock_plausible(t); }

inline void clock_text(const std::tm &t, char (&text)[8]) { legacy::clock_text(t, text); }

inline const char *link_text(uint8_t link) {
  return legacy::link_text(static_cast<legacy::NetLink>(link));
}

inline void diag_rate_text(int32_t rate, char *out, std::size_t out_size) {
  legacy::diag_rate_text(rate, out, out_size);
}

} // namespace sut
