#pragma once

/// @file port.hpp
/// @brief fw_core's platform port: task identity, ISR context and logging.
/// @details Internal to fw_core. The same declarations are implemented twice:
///          src/port_freertos.cpp (ESP-IDF build) and src/port_host.cpp (host L1 build).
///          The bounded queue the channels sit on is in port/raw_queue.hpp.

#include <cstdint>
#include <span>
#include <string_view>

namespace hmi::fw::port {

/// @brief Identifies a task: the FreeRTOS task handle, or the pthread on the host.
using TaskId = std::uintptr_t;

/// @brief The TaskId no task has.
inline constexpr TaskId NO_TASK = 0;

/// @brief The task that is running now.
/// @return Its id; never NO_TASK.
[[nodiscard]] TaskId current_task() noexcept;

/// @brief Whether the caller runs in an interrupt handler.
/// @details Host: a thread whose name starts with "isr" counts as an interrupt handler. That is
///          the hook the L1 tests use to check the ISR rules (TS-UNIT-05).
/// @return true in an ISR.
[[nodiscard]] bool in_isr() noexcept;

/// @brief Copies the current task's name into @p out, NUL-terminated and truncated to fit.
/// @param out Where the name goes; an empty span is left untouched.
void current_task_name(std::span<char> out) noexcept;

/// @brief Logs one line at ERROR level, tagged "fw_core".
/// @param text The line, already formatted.
void log_error(std::string_view text) noexcept;

} // namespace hmi::fw::port
