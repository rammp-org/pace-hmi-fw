#pragma once

/// @file config.hpp
/// @brief alloc_guard's configuration, as constexpr values (CS-TYP-05).

#include <cstddef>

#include "sdkconfig.h"

namespace hmi::alloc_guard {

/// @brief Whether this build has the on-chip allocation guard (CONFIG_HMI_BENCH_ALLOC_GUARD): a
///        bench feature, off unless a bench overlay turns it on (ci/sdkconfig.allocguard;
///        CS-LAY-09). Without it the component compiles no source, and callers reach its
///        functions only inside `if constexpr (ENABLED)`.
inline constexpr bool ENABLED = CONFIG_HMI_BENCH_ALLOC_GUARD_AS_INT != 0;

/// @brief How many tasks the guard can watch at once.
inline constexpr std::size_t MAX_TASKS = 4;

} // namespace hmi::alloc_guard
