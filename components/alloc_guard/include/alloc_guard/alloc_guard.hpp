#pragma once

/// @file alloc_guard.hpp
/// @brief The on-chip allocation guard (TS-DET-08): counts the heap allocations chosen tasks
///        make once start-up is declared complete. Bench builds only (CONFIG_HMI_BENCH_ALLOC_GUARD,
///        see config.hpp); the self test reports the counts (mem.alloc_* in selftest_spec.hpp).
/// @details The counting is ESP-IDF's heap hook, esp_heap_trace_alloc_hook
///          (CONFIG_HEAP_USE_HOOKS, which the Kconfig option selects), defined in
///          src/alloc_guard.cpp. It runs after every successful allocation, in IRAM, and only
///          counts (alloc_counter.hpp says how a task is told apart and why that is safe in an
///          interrupt or with the cache off). The functions below run on ordinary tasks.
///
///          These functions exist only in a build with the guard: call them inside
///          `if constexpr (ENABLED)` (config.hpp), so a release build has no reference to them.
///
///          A task is watched by name: its stack, found with xTaskGetStackStart and the heap
///          block that holds it. A task that does not exist yet when start() runs (rtps_pub
///          waits for the network) is looked up again by count(), and counts from then on.

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "alloc_guard/alloc_counter.hpp"
#include "alloc_guard/config.hpp"

namespace hmi::alloc_guard {

/// @brief Declares start-up complete and watches @p tasks: task i in slot i. From now on every
///        heap allocation one of them makes is counted. Call once, at app_main's park.
/// @param tasks Task names (FreeRTOS's, at most 15 characters), at most MAX_TASKS; more are
///        ignored. A name with no task yet is looked up again by count().
void start(std::span<const std::string_view> tasks);

/// @brief What slot @p slot counted since start(). Looks @p task up again first: a task made
///        after start(), or made again, is watched from now on and keeps the slot's count.
/// @param slot The slot start() gave the task.
/// @param task Its name, as given to start().
/// @return The count; nullopt when the slot has never watched a task (no task by that name
///         since start()), or before start().
[[nodiscard]] std::optional<Count> count(std::size_t slot, std::string_view task);

} // namespace hmi::alloc_guard
