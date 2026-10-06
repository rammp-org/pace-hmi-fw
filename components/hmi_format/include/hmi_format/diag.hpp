#pragma once
// The DIAGNOSTICS screen's arrival-rate label. Plain C++, no LVGL (CS-UI-03).

#include <cstdint>
#include <span>

namespace hmi::format {

/// @brief The live rate text, "N.N Hz - Live", from tenths of a Hz (the integer quotient and
/// remainder by 10, so a negative rate keeps its sign on both: -15 reads "-1.-5").
/// @param rate_tenths_hz the arrival rate, tenths of a Hz
/// @param out the buffer the text goes into, always terminated (unless it is empty)
void diag_rate_text(int32_t rate_tenths_hz, std::span<char> out) noexcept;

} // namespace hmi::format
