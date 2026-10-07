#pragma once

/// @file config.hpp
/// @brief fw_core's configuration, as constexpr values (CS-TYP-05).

#include <cstddef>

#if defined(ESP_PLATFORM)
// sdkconfig.h only exists in an ESP-IDF build; the host build passes CONFIG_* on the command line.
#include <sdkconfig.h>
#endif

namespace hmi::fw {

/// @brief What a failed ownership check does by default (CS-OWN-11).
/// @details true (test builds): log the failure and abort. false (release builds): log it and
///          let the check return false, so the caller goes to its safe state.
///          The Kconfig option CONFIG_HMI_OWNERSHIP_CHECKS_ABORT does not exist yet; until it
///          does, a build sets it with -D (the host test Makefile does).
#ifdef CONFIG_HMI_OWNERSHIP_CHECKS_ABORT
inline constexpr bool OWNERSHIP_CHECKS_ABORT = true;
#else
inline constexpr bool OWNERSHIP_CHECKS_ABORT = false;
#endif

/// @brief The largest message a channel accepts, in bytes (CS-OWN-05).
inline constexpr std::size_t MAX_MESSAGE_SIZE = 128;

/// @brief The longest task name kept for an ownership report, including the terminating NUL.
/// @details FreeRTOS's configMAX_TASK_NAME_LEN is 16 in ESP-IDF; Linux thread names are 16 too.
inline constexpr std::size_t TASK_NAME_SIZE = 16;

} // namespace hmi::fw
