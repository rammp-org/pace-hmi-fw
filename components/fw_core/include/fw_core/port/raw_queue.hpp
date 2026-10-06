#pragma once

/// @file raw_queue.hpp
/// @brief Picks the bounded FIFO the channels are built on: FreeRTOS or the host shim.

// #if, not if constexpr: the FreeRTOS headers do not exist in the host build, and the host shim's
// std::mutex is not allowed in the target build (CS-TYP-05, CS-OWN-08).
#if defined(ESP_PLATFORM)
#include "fw_core/port/raw_queue_freertos.hpp"
#else
#include "fw_core/port/raw_queue_host.hpp"
#endif
