#pragma once

/// @file rates_constants.hpp
/// @brief The rates and depths the topology's CHANNELS table names (CS-OWN-12).
/// @details DRAFT for review, like topology.hpp. Dependency-free: no includes but <cstdint>.
///          Each value says where it comes from:
///          - measured: read on the bench (board 2) or in a boot log;
///          - doc: today's code or a document states it (file:line);
///          - estimate: a placeholder until the stress test measures it (CS-MEM-04).
///          A rate is in Hz, the most often a producer writes. A depth is a queue's capacity.

#include <cstdint>

namespace hmi::topo {

// --- rates (Hz) ----------------------------------------------------------------------------

/// @brief A state channel written on change only, with no fixed rate.
inline constexpr std::uint32_t ON_CHANGE = 0;

/// @brief MibStatus from the MCB. doc: plan D1 ("MibStatus 2 Hz"), the MCB's publish period.
inline constexpr std::uint32_t MIB_STATUS_HZ = 2;

/// @brief Diagnostics from the MCB. doc: plan D1 ("Diagnostics 2 Hz").
inline constexpr std::uint32_t DIAG_HZ = 2;

/// @brief The net island's link-state tick. doc: plan §2.1 ("event-driven, 250 ms tick").
inline constexpr std::uint32_t NET_TICK_HZ = 4;

/// @brief The control island's cycle. doc: `kAdcUpdatePeriod = 33ms` (main/main.cpp:1452),
///        today's ADC task period ("30 Hz").
inline constexpr std::uint32_t CONTROL_HZ = 30;

/// @brief The UI island's drain, once per lv_timer cycle. doc: the lv_task period of 8 ms
///        (main/main.cpp:1220); an upper bound. measured: the flipped panel renders ~17 fps.
inline constexpr std::uint32_t UI_FRAME_HZ = 125;

// --- queue depths --------------------------------------------------------------------------

/// @brief HmiCommand events from the MCB (self-test start, etc.). estimate.
inline constexpr std::uint32_t HMI_COMMAND_DEPTH = 4;

/// @brief Drive intents from the UI (enable, disable, profile). doc: plan D2 ("Queue · 8").
inline constexpr std::uint32_t DRIVE_INTENT_DEPTH = 8;

/// @brief Seat requests from the UI (one per press or hold step). estimate.
inline constexpr std::uint32_t SEAT_REQUEST_DEPTH = 8;

/// @brief Navigation keys from the stick to the UI. doc: plan D2 ("Queue · 16").
inline constexpr std::uint32_t NAV_KEY_DEPTH = 16;

/// @brief Stick-button edges to the UI. estimate.
inline constexpr std::uint32_t BUTTON_DEPTH = 8;

/// @brief Side-button and touch events to the UI. estimate.
inline constexpr std::uint32_t UI_INPUT_DEPTH = 16;

/// @brief Remote-UI requests and replies (bench only). doc: plan D2 ("Queue · 4").
inline constexpr std::uint32_t REMOTE_DEPTH = 4;

/// @brief Self-test requests: one run at a time. doc: plan §2.2 (depth 1).
inline constexpr std::uint32_t SELFTEST_REQ_DEPTH = 1;

/// @brief Self-test progress and result events. estimate (today's table has ~57 rows; the UI
///        drains every frame, so 16 covers a burst of 2 frames).
inline constexpr std::uint32_t SELFTEST_DEPTH = 16;

/// @brief OTA progress events. estimate.
inline constexpr std::uint32_t OTA_DEPTH = 8;

/// @brief Wi-Fi scan and join events. estimate.
inline constexpr std::uint32_t WIFI_DEPTH = 8;

/// @brief Log chunks to the UI's log ring. estimate (boot logs burst; the drop is counted).
inline constexpr std::uint32_t LOG_DEPTH = 32;

/// @brief The deepest queue the topology accepts. doc: fw_core's MAX_QUEUE_DEPTH (queue.hpp);
///        topology_espp.hpp checks the two are equal.
inline constexpr std::uint32_t MAX_QUEUE_DEPTH = 64;

// --- timeouts ------------------------------------------------------------------------------

/// @brief The longest a BLOCK_TIMEOUT queue's send waits, in ms (today only the remote UI's
///        queues block). estimate.
inline constexpr std::uint32_t BLOCK_SEND_TIMEOUT_MS = 100;

} // namespace hmi::topo
