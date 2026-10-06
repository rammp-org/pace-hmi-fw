#pragma once

/// @file topology.hpp
/// @brief Every task, component and channel of the HMI firmware, as constexpr tables
///        (CS-OWN-12). DRAFT for review: not wired into the firmware yet.
/// @details People own this file (CS-OWN-13). A PR that changes a row states the change in
///          words. An AI never edits a row to make a check pass.
///          Pure C++: no FreeRTOS, ESP-IDF or espp includes. Storage and handles come from
///          topology_espp.hpp; task settings from `topo.task_config(Task::X)` (CS-CON-02).
///          Source: docs/plans/refactor.md §2.1 (islands and adapters) and §2.2 (this draft).
///          Where this file differs from §2.2, the comment on the row says why.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "messages.hpp"        // every Message type below
#include "rates_constants.hpp" // UPPER_CASE rates and depths, no dependencies
#include "topology_types.hpp"  // Task, Ch, Role, Start, Kind, Full and the row types
#include "topology_validate.hpp"

namespace hmi::topo {

// clang-format off

// Stacks and priorities are today's values (main/main.cpp, file:line in README) or
// placeholders until the stress test measures them (CS-MEM-04).
// Priorities: control above ui, so an LVGL stall cannot starve it (H4, H11).
inline constexpr std::array TASKS{
  TaskRow{Task::UI,           "ui",           Role::ISLAND,  16384, 20,  1, false, Start::BOOT},
  TaskRow{Task::CONTROL,      "control",      Role::ISLAND,   6144, 22,  0, true,  Start::BOOT},
  TaskRow{Task::NET,          "net",          Role::ISLAND,   6144,  5, -1, true,  Start::BOOT},
  TaskRow{Task::SELFTEST,     "selftest",     Role::ISLAND,   8192,  4, -1, true,  Start::ON_DEMAND},
  TaskRow{Task::OTA,          "ota",          Role::ISLAND,   8192,  3, -1, false, Start::ON_DEMAND},
  TaskRow{Task::WIFI_SCAN,    "wifi_scan",    Role::ISLAND,   6144,  3, -1, false, Start::ON_DEMAND},
  TaskRow{Task::FW_HASH,      "fw_hash",      Role::ISLAND,   4096,  1, -1, false, Start::ON_DEMAND},
  // Not in the §2.2 draft; from §2.1 ("replaces the Data Display Task": 6 KB, prio 10, core 1).
  TaskRow{Task::HOUSEKEEPING, "housekeeping", Role::ISLAND,   6144, 10,  1, false, Start::BOOT},
  TaskRow{Task::STICK_BUTTON, "Button",       Role::ADAPTER,  4096,  5, -1, true,  Start::BOOT},
  TaskRow{Task::REMOTE_UI,    "remote_ui",    Role::ADAPTER, 16384,  3, -1, false, Start::BOOT},
  TaskRow{Task::SIDE_BUTTON,  "side_button",  Role::ADAPTER,  4096,  5, -1, false, Start::BOOT},
  TaskRow{Task::TOUCH,        "touch",        Role::ADAPTER,  4096,  5, -1, false, Start::BOOT},
};

// Tasks ESP-IDF, espp and drivers create (TS-DET-09). RTPS_RX and LOG_HOOK are adapters that
// run on a foreign task: `hosts` names them, so CHANNELS can name them.
inline constexpr std::array FOREIGN_TASKS{
  ForeignTaskRow{"rtps_worker", Start::BOOT, Task::RTPS_RX, true},  // espp RTPS receive
  ForeignTaskRow{ANY_TASK,      Start::BOOT, Task::LOG_HOOK},       // vprintf hook: any task
  ForeignTaskRow{"main",        Start::BOOT},
  ForeignTaskRow{"IDLE0",       Start::BOOT},
  ForeignTaskRow{"IDLE1",       Start::BOOT},
  ForeignTaskRow{"ipc0",        Start::BOOT},
  ForeignTaskRow{"ipc1",        Start::BOOT},
  ForeignTaskRow{"esp_timer",   Start::BOOT},
  ForeignTaskRow{"sys_evt",     Start::BOOT},
  ForeignTaskRow{"tiT",         Start::BOOT},                       // lwIP
  // esp_hosted, Wi-Fi, W5500 and espp socket tasks: filled from a real boot (TS-DET-09).
};

inline constexpr std::array COMPONENTS{
  ComponentRow{"hmi_ui",        Task::UI},
  ComponentRow{"stick",         Task::CONTROL},
  ComponentRow{"drive_session", Task::CONTROL},
  ComponentRow{"net_link",      Task::NET},
  ComponentRow{"selftest",      Task::SELFTEST},
  ComponentRow{"github_ota",    Task::OTA},
  ComponentRow{"settings",      Task::UI},
  ComponentRow{"log_capture",   Task::UI},
};

inline constexpr std::array CHANNELS{
  // MCB -> HMI. Fan-out = one mailbox per consumer.
  ChannelRow{Ch::MCB_TO_CONTROL,  "MCB_TO_CONTROL",  Kind::MAILBOX, "McbStatusMsg",     Task::RTPS_RX,      Task::CONTROL,   MIB_STATUS_HZ,      Full::OVERWRITE},
  ChannelRow{Ch::MCB_TO_UI,       "MCB_TO_UI",       Kind::MAILBOX, "McbStatusMsg",     Task::RTPS_RX,      Task::UI,        MIB_STATUS_HZ,      Full::OVERWRITE},
  ChannelRow{Ch::DIAG,            "DIAG",            Kind::MAILBOX, "DiagMsg",          Task::RTPS_RX,      Task::UI,        DIAG_HZ,            Full::OVERWRITE},
  ChannelRow{Ch::HMI_COMMAND,     "HMI_COMMAND",     Kind::QUEUE,   "HmiCommandMsg",    Task::RTPS_RX,      Task::UI,        HMI_COMMAND_DEPTH,  Full::DROP_NEWEST_COUNT},
  ChannelRow{Ch::BRIGHTNESS,      "BRIGHTNESS",      Kind::MAILBOX, "BrightnessMsg",    Task::RTPS_RX,      Task::UI,        ON_CHANGE,          Full::OVERWRITE},
  ChannelRow{Ch::LINK_TO_CONTROL, "LINK_TO_CONTROL", Kind::MAILBOX, "LinkMsg",          Task::NET,          Task::CONTROL,   NET_TICK_HZ,        Full::OVERWRITE},
  ChannelRow{Ch::LINK_TO_UI,      "LINK_TO_UI",      Kind::MAILBOX, "LinkMsg",          Task::NET,          Task::UI,        NET_TICK_HZ,        Full::OVERWRITE},
  // UI <-> control. Intents that must not be lost are queued (CS-OWN-04).
  ChannelRow{Ch::DRIVE_INTENT,    "DRIVE_INTENT",    Kind::QUEUE,   "DriveIntentMsg",   Task::UI,           Task::CONTROL,   DRIVE_INTENT_DEPTH, Full::RAISE_FAULT},
  // §2.2 had DROP_NEWEST_COUNT, which validate() rejects: control is a safety task.
  ChannelRow{Ch::SEAT_REQUEST,    "SEAT_REQUEST",    Kind::QUEUE,   "SeatRequestMsg",   Task::UI,           Task::CONTROL,   SEAT_REQUEST_DEPTH, Full::RAISE_FAULT},
  ChannelRow{Ch::UI_CONTEXT,      "UI_CONTEXT",      Kind::MAILBOX, "UiContextMsg",     Task::UI,           Task::CONTROL,   UI_FRAME_HZ,        Full::OVERWRITE},  // screen, menu, overlay, heartbeat
  ChannelRow{Ch::STICK_SETTINGS,  "STICK_SETTINGS",  Kind::MAILBOX, "StickSettingsMsg", Task::UI,           Task::CONTROL,   ON_CHANGE,          Full::OVERWRITE},
  ChannelRow{Ch::DRIVE_VIEW,      "DRIVE_VIEW",      Kind::MAILBOX, "DriveViewMsg",     Task::CONTROL,      Task::UI,        CONTROL_HZ,         Full::OVERWRITE},
  ChannelRow{Ch::STICK_VIEW,      "STICK_VIEW",      Kind::MAILBOX, "StickViewMsg",     Task::CONTROL,      Task::UI,        CONTROL_HZ,         Full::OVERWRITE},
  ChannelRow{Ch::NAV_KEY,         "NAV_KEY",         Kind::QUEUE,   "NavKeyMsg",        Task::CONTROL,      Task::UI,        NAV_KEY_DEPTH,      Full::DROP_NEWEST_COUNT},
  ChannelRow{Ch::STICK_BUTTON,    "STICK_BUTTON",    Kind::ATOMIC,  "bool",             Task::STICK_BUTTON, Task::CONTROL,   ON_CHANGE,          Full::OVERWRITE},
  ChannelRow{Ch::BUTTON_EVENT,    "BUTTON_EVENT",    Kind::QUEUE,   "ButtonEdgeMsg",    Task::STICK_BUTTON, Task::UI,        BUTTON_DEPTH,       Full::DROP_NEWEST_COUNT},
  // Tools, workers and logs -> UI.
  ChannelRow{Ch::UI_INPUT,        "UI_INPUT",        Kind::QUEUE,   "UiInputMsg",       Task::SIDE_BUTTON,  Task::UI,        UI_INPUT_DEPTH,     Full::DROP_NEWEST_COUNT},
  ChannelRow{Ch::REMOTE_UI_REQ,   "REMOTE_UI_REQ",   Kind::QUEUE,   "RemoteUiReqMsg",   Task::REMOTE_UI,    Task::UI,        REMOTE_DEPTH,       Full::BLOCK_TIMEOUT},
  ChannelRow{Ch::REMOTE_UI_REP,   "REMOTE_UI_REP",   Kind::QUEUE,   "RemoteUiRepMsg",   Task::UI,           Task::REMOTE_UI, REMOTE_DEPTH,       Full::BLOCK_TIMEOUT},
  // §2.2 had DROP_NEWEST_COUNT, which validate() rejects: selftest is a safety task.
  ChannelRow{Ch::SELFTEST_REQ,    "SELFTEST_REQ",    Kind::QUEUE,   "SelfTestReqMsg",   Task::UI,           Task::SELFTEST,  SELFTEST_REQ_DEPTH, Full::RAISE_FAULT},
  ChannelRow{Ch::SELFTEST_EVENT,  "SELFTEST_EVENT",  Kind::QUEUE,   "SelfTestEventMsg", Task::SELFTEST,     Task::UI,        SELFTEST_DEPTH,     Full::DROP_NEWEST_COUNT},
  ChannelRow{Ch::POST_RESULT,     "POST_RESULT",     Kind::ATOMIC,  "PostVerdict",      Task::SELFTEST,     Task::CONTROL,   ON_CHANGE,          Full::OVERWRITE},
  ChannelRow{Ch::OTA_EVENT,       "OTA_EVENT",       Kind::QUEUE,   "OtaEventMsg",      Task::OTA,          Task::UI,        OTA_DEPTH,          Full::DROP_NEWEST_COUNT},
  ChannelRow{Ch::WIFI_EVENT,      "WIFI_EVENT",      Kind::QUEUE,   "WifiEventMsg",     Task::WIFI_SCAN,    Task::UI,        WIFI_DEPTH,         Full::DROP_NEWEST_COUNT},
  ChannelRow{Ch::LOG_RING,        "LOG_RING",        Kind::QUEUE,   "LogChunkMsg",      Task::LOG_HOOK,     Task::UI,        LOG_DEPTH,          Full::DROP_NEWEST_COUNT},
};

// clang-format on

static_assert(validate(TASKS, FOREIGN_TASKS, COMPONENTS, CHANNELS));

/// @brief The number of adapters that run on a foreign task.
[[nodiscard]] consteval std::size_t hosted_count() noexcept {
  std::size_t n = 0;
  for (const ForeignTaskRow &row : FOREIGN_TASKS) {
    n += row.hosts.has_value() ? 1U : 0U;
  }
  return n;
}
// With validate()'s unique ids, these make every id have exactly one row.
static_assert(TASKS.size() + hosted_count() == static_cast<std::size_t>(Task::COUNT_),
              "topology: every Task id needs a TASKS row or a FOREIGN_TASKS hosts (CS-OWN-12)");
static_assert(CHANNELS.size() == static_cast<std::size_t>(Ch::COUNT_),
              "topology: every Ch id needs a CHANNELS row (CS-OWN-12)");

/// @brief The index of @p id's TASKS row, or TASKS.size() for an adapter on a foreign task.
/// @param id The task.
/// @return The index.
[[nodiscard]] constexpr std::size_t task_index(Task id) noexcept {
  for (std::size_t i = 0; i < TASKS.size(); ++i) {
    if (TASKS[i].id == id) {
      return i;
    }
  }
  return TASKS.size();
}

/// @brief The TASKS row of @p id, or nullptr for an adapter on a foreign task (run time).
/// @param id The task.
/// @return The row, or nullptr.
[[nodiscard]] constexpr const TaskRow *find_task(Task id) noexcept {
  const std::size_t i = task_index(id);
  return i < TASKS.size() ? &TASKS[i] : nullptr;
}

/// @brief The index of @p id's CHANNELS row, or CHANNELS.size() if it has none.
/// @param id The channel.
/// @return The index.
[[nodiscard]] constexpr std::size_t channel_index(Ch id) noexcept {
  for (std::size_t i = 0; i < CHANNELS.size(); ++i) {
    if (CHANNELS[i].id == id) {
      return i;
    }
  }
  return CHANNELS.size();
}

/// @brief A channel's type: its id, its row and its message (CS-OWN-12).
/// @details Pairs a CHANNELS row with a C++ type. A `using` line whose message type does not
///          match the row's `message` column does not compile. Storage and handles for it come
///          from topology_espp.hpp.
/// @tparam ID The channel's id.
/// @tparam M The message type (MessageName<M>::VALUE must equal the row's `message`).
template <Ch ID, class M> struct Channel {
  static_assert(channel_index(ID) < CHANNELS.size(),
                "topology::Channel: the id has no CHANNELS row (CS-OWN-12)");
  static constexpr Ch CH_ID = ID;                                                  ///< The id.
  static constexpr ChannelRow ROW = CHANNELS[channel_index(ID) % CHANNELS.size()]; ///< The row.
  static_assert(MessageName<M>::VALUE == ROW.message,
                "topology::Channel: the message type must match its CHANNELS row (CS-OWN-12)");
  using message_type = M; ///< The message.
};

// One `using` per CHANNELS row, in the same order.
using McbToControlCh = Channel<Ch::MCB_TO_CONTROL, McbStatusMsg>;
using McbToUiCh = Channel<Ch::MCB_TO_UI, McbStatusMsg>;
using DiagCh = Channel<Ch::DIAG, DiagMsg>;
using HmiCommandCh = Channel<Ch::HMI_COMMAND, HmiCommandMsg>;
using BrightnessCh = Channel<Ch::BRIGHTNESS, BrightnessMsg>;
using LinkToControlCh = Channel<Ch::LINK_TO_CONTROL, LinkMsg>;
using LinkToUiCh = Channel<Ch::LINK_TO_UI, LinkMsg>;
using DriveIntentCh = Channel<Ch::DRIVE_INTENT, DriveIntentMsg>;
using SeatRequestCh = Channel<Ch::SEAT_REQUEST, SeatRequestMsg>;
using UiContextCh = Channel<Ch::UI_CONTEXT, UiContextMsg>;
using StickSettingsCh = Channel<Ch::STICK_SETTINGS, StickSettingsMsg>;
using DriveViewCh = Channel<Ch::DRIVE_VIEW, DriveViewMsg>;
using StickViewCh = Channel<Ch::STICK_VIEW, StickViewMsg>;
using NavKeyCh = Channel<Ch::NAV_KEY, NavKeyMsg>;
using StickButtonCh = Channel<Ch::STICK_BUTTON, bool>;
using ButtonEventCh = Channel<Ch::BUTTON_EVENT, ButtonEdgeMsg>;
using UiInputCh = Channel<Ch::UI_INPUT, UiInputMsg>;
using RemoteUiReqCh = Channel<Ch::REMOTE_UI_REQ, RemoteUiReqMsg>;
using RemoteUiRepCh = Channel<Ch::REMOTE_UI_REP, RemoteUiRepMsg>;
using SelfTestReqCh = Channel<Ch::SELFTEST_REQ, SelfTestReqMsg>;
using SelfTestEventCh = Channel<Ch::SELFTEST_EVENT, SelfTestEventMsg>;
using PostResultCh = Channel<Ch::POST_RESULT, PostVerdict>;
using OtaEventCh = Channel<Ch::OTA_EVENT, OtaEventMsg>;
using WifiEventCh = Channel<Ch::WIFI_EVENT, WifiEventMsg>;
using LogRingCh = Channel<Ch::LOG_RING, LogChunkMsg>;

/// @brief A list of channel types.
template <class... Chs> struct ChannelList {};

/// @brief Every channel, in CHANNELS row order. topology_espp.hpp creates storage for each.
using AllChannels =
    ChannelList<McbToControlCh, McbToUiCh, DiagCh, HmiCommandCh, BrightnessCh, LinkToControlCh,
                LinkToUiCh, DriveIntentCh, SeatRequestCh, UiContextCh, StickSettingsCh, DriveViewCh,
                StickViewCh, NavKeyCh, StickButtonCh, ButtonEventCh, UiInputCh, RemoteUiReqCh,
                RemoteUiRepCh, SelfTestReqCh, SelfTestEventCh, PostResultCh, OtaEventCh,
                WifiEventCh, LogRingCh>;

/// @brief True when the list names CHANNELS rows 0, 1, 2, ... in order, each once.
/// @details Naming a channel's ROW instantiates it, so every `using` line above is checked.
template <class... Chs> [[nodiscard]] consteval bool in_row_order(ChannelList<Chs...>) noexcept {
  std::size_t i = 0;
  return ((channel_index(Chs::ROW.id) == i++) && ...) && i == CHANNELS.size();
}
static_assert(
    in_row_order(AllChannels{}),
    "topology: one `using XCh = Channel<...>` per CHANNELS row, in row order (CS-OWN-12)");

} // namespace hmi::topo
