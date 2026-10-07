#pragma once
// Base rows for the must-not-compile cases (TS-UNIT-06). Every case builds its tables from
// these, changes one value under MNC_FAULT, and checks them with validate(). The base rows
// pass every rule, so each case's twin (no MNC_FAULT) compiles.

#include <array>

#include "topology_validate.hpp"

namespace mnc {
using namespace hmi::topo;

// clang-format off
inline constexpr TaskRow UI_ROW{Task::UI, "ui", Role::ISLAND, 16384, 20, 1, false, Start::BOOT};
inline constexpr TaskRow CONTROL_ROW{Task::CONTROL, "control", Role::ISLAND, 6144, 22, 0, true, Start::BOOT};
inline constexpr TaskRow REMOTE_ROW{Task::REMOTE_UI, "remote_ui", Role::ADAPTER, 16384, 3, -1, false, Start::BOOT};

inline constexpr ForeignTaskRow RTPS_HOST{"rtps_worker", Start::BOOT, Task::RTPS_RX, true};
inline constexpr ForeignTaskRow MAIN_ROW{"main", Start::BOOT};

inline constexpr ComponentRow HMI_UI{"hmi_ui", Task::UI};
inline constexpr ComponentRow STICK{"stick", Task::CONTROL};

inline constexpr ChannelRow DRIVE_INTENT{Ch::DRIVE_INTENT, "DRIVE_INTENT", Kind::QUEUE, "DriveIntentMsg", Task::UI, Task::CONTROL, 8, Full::RAISE_FAULT};
inline constexpr ChannelRow DRIVE_VIEW{Ch::DRIVE_VIEW, "DRIVE_VIEW", Kind::MAILBOX, "DriveViewMsg", Task::CONTROL, Task::UI, 30, Full::OVERWRITE};
inline constexpr ChannelRow MCB_TO_CONTROL{Ch::MCB_TO_CONTROL, "MCB_TO_CONTROL", Kind::MAILBOX, "McbStatusMsg", Task::RTPS_RX, Task::CONTROL, 2, Full::OVERWRITE};
inline constexpr ChannelRow REMOTE_UI_REQ{Ch::REMOTE_UI_REQ, "REMOTE_UI_REQ", Kind::QUEUE, "RemoteUiReqMsg", Task::REMOTE_UI, Task::UI, 4, Full::BLOCK_TIMEOUT};

inline constexpr std::array TASKS{UI_ROW, CONTROL_ROW, REMOTE_ROW};
inline constexpr std::array FOREIGN{RTPS_HOST, MAIN_ROW};
inline constexpr std::array COMPONENTS{HMI_UI, STICK};
inline constexpr std::array CHANNELS{DRIVE_INTENT, DRIVE_VIEW, MCB_TO_CONTROL, REMOTE_UI_REQ};
// clang-format on

} // namespace mnc
