#pragma once

/// @file topology_types.hpp
/// @brief The topology's ids, enums and row types (CS-OWN-12). DRAFT for review.
/// @details Pure C++: no FreeRTOS, ESP-IDF or espp includes. People own this file and
///          topology.hpp (CS-OWN-13): adding a task or a channel adds its id here and its row
///          there, in the same PR, stated in words.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hmi::topo {

/// @brief Every task that produces or consumes a channel: the TASKS rows, plus the adapters
///        that run on a foreign task (FOREIGN_TASKS rows with `hosts`).
enum class Task : std::uint8_t {
  UI,
  CONTROL,
  NET,
  SELFTEST,
  OTA,
  WIFI_SCAN,
  FW_HASH,
  HOUSEKEEPING,
  STICK_BUTTON,
  RTPS_RX,
  REMOTE_UI,
  LOG_HOOK,
  SIDE_BUTTON,
  TOUCH,
  COUNT_, ///< Not a task: the number of ids.
};

/// @brief Every channel: one CHANNELS row and one `using XCh = Channel<...>` line each.
enum class Ch : std::uint8_t {
  MCB_TO_CONTROL,
  MCB_TO_UI,
  DIAG,
  HMI_COMMAND,
  BRIGHTNESS,
  LINK_TO_CONTROL,
  LINK_TO_UI,
  DRIVE_INTENT,
  SEAT_REQUEST,
  UI_CONTEXT,
  STICK_SETTINGS,
  DRIVE_VIEW,
  STICK_VIEW,
  NAV_KEY,
  STICK_BUTTON,
  BUTTON_EVENT,
  UI_INPUT,
  REMOTE_UI_REQ,
  REMOTE_UI_REP,
  SELFTEST_REQ,
  SELFTEST_EVENT,
  POST_RESULT,
  OTA_EVENT,
  WIFI_EVENT,
  LOG_RING,
  COUNT_, ///< Not a channel: the number of ids.
};

/// @brief What a task is (CS-OWN-01, CS-OWN-02).
/// @details NONE is the value-initialised default, so a row that leaves its role out is caught
///          by validate() instead of silently becoming an island.
enum class Role : std::uint8_t {
  NONE,    ///< Not set: validate() rejects it.
  ISLAND,  ///< Owns state; one task runs all of it.
  ADAPTER, ///< Holds no application state; turns input into messages.
};

/// @brief When a task starts (TS-DET-09: the census checks boot tasks after start-up).
enum class Start : std::uint8_t {
  BOOT,      ///< Running once start-up is complete.
  ON_DEMAND, ///< Started for one job, then ends.
};

/// @brief The channel kinds (CS-OWN-03).
enum class Kind : std::uint8_t {
  MAILBOX, ///< State: one slot, the writer overwrites (fw::Mailbox).
  QUEUE,   ///< Events: bounded FIFO, none lost or merged (fw::Queue).
  ATOMIC,  ///< One lock-free value (fw::AtomicValue).
  CONST,   ///< Data fixed after start-up.
};

/// @brief What a full channel does (CS-OWN-04). A mailbox or atomic always overwrites.
enum class Full : std::uint8_t {
  OVERWRITE,         ///< Mailbox and atomic only.
  DROP_NEWEST_COUNT, ///< Queue: drop the new message and count it. Never into a safety task.
  BLOCK_TIMEOUT,     ///< Queue: wait up to a timeout. Never on a safety path.
  RAISE_FAULT,       ///< Queue: drop, raise a fault, log once.
};

/// @brief One task this firmware creates (CS-OWN-12, CS-CON-02).
struct TaskRow {
  Task id;               ///< The id channels name.
  std::string_view name; ///< The FreeRTOS task name the census matches (TS-DET-09).
  Role role;             ///< Island or adapter.
  std::uint32_t stack;   ///< Stack size, bytes.
  std::uint8_t prio;     ///< FreeRTOS priority.
  std::int8_t core;      ///< Core 0 or 1; -1 for no affinity.
  bool safety;           ///< Safety-relevant (CS-SAF-01).
  Start start;           ///< Boot or on demand.
};

/// @brief A task ESP-IDF, espp or a driver creates (TS-DET-09).
/// @details `hosts` names the adapter that runs on it, if any, so CHANNELS can name that
///          adapter as a producer or consumer. ANY_TASK as the name: the adapter runs on
///          whichever task calls it (the log hook); the census skips that row.
struct ForeignTaskRow {
  std::string_view name;          ///< The FreeRTOS task name.
  Start start;                    ///< Boot or on demand.
  std::optional<Task> hosts = {}; ///< The adapter that runs on this task, if any.
  bool safety = false;            ///< The hosted adapter is safety-relevant (CS-SAF-01).
};

/// @brief The name of a ForeignTaskRow whose adapter runs on any calling task.
inline constexpr std::string_view ANY_TASK = "*";

/// @brief A component and the task that runs it.
struct ComponentRow {
  std::string_view name; ///< Component name (folder under components/).
  Task task;             ///< The island or adapter that runs it.
};

/// @brief One channel: the only way data crosses tasks (CS-OWN-03).
struct ChannelRow {
  Ch id;                       ///< The id `Channel<Ch::X, M>` names.
  std::string_view name;       ///< UPPER_CASE, for logs and diagrams.
  Kind kind;                   ///< Mailbox, queue, atomic or const.
  std::string_view message;    ///< The message type's name (MessageName<M>::VALUE).
  Task producer;               ///< The one task that writes.
  Task consumer;               ///< The one task that reads.
  std::uint32_t rate_or_depth; ///< Mailbox/atomic: rate in Hz (ON_CHANGE = 0). Queue: depth.
  Full full;                   ///< What a full channel does.
};

} // namespace hmi::topo
