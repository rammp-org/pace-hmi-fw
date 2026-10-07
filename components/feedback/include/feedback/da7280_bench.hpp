#pragma once

#include <cstdint>
#include <vector>

#include "i2c.hpp"
#include "logger.hpp"

namespace hmi::feedback {

/// DA7280 haptic driver bring-up test: read-only register probe, no driver
/// class yet. Datasheet 7-bit slave address is 0x4A (its 0x94/0x95 values are
/// the pre-shifted 8-bit write/read forms) — espp::I2c shifts internally, so
/// the raw 7-bit address is passed here. The self test also reports whether
/// the boot scan found it.
inline constexpr uint8_t kDa7280Address = 0x4A;

/// @brief The DA7280 bench run at boot: a register probe, then the driver's
/// functional test. Only with CONFIG_HMI_BENCH_DA7280_TEST (kBenchDa7280Test);
/// otherwise nothing.
/// @param logger The boot log the run reports to.
/// @param i2c The Tab5 internal bus.
/// @param found_addresses What the boot scan found on that bus.
void run_da7280_bench(espp::Logger &logger, espp::I2c &i2c,
                      const std::vector<uint8_t> &found_addresses);

} // namespace hmi::feedback
