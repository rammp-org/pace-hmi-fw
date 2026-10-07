#pragma once

#include "sdkconfig.h"

namespace hmi::feedback {

/// Whether app_main runs the DA7280 bench tests at boot (run_da7280_bench): a bench
/// feature, off unless CONFIG_HMI_BENCH_DA7280_TEST (CS-LAY-09, CS-TYP-05).
inline constexpr bool kBenchDa7280Test = CONFIG_HMI_BENCH_DA7280_TEST_AS_INT != 0;

} // namespace hmi::feedback
