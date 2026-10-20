#pragma once

/**
 * @file hmi_estop.hpp
 * @brief The HMI's e-stop: one estop::Node (estop-core) named rammp::kEStopSelf, on the
 *        switch the Settings rows choose.
 *
 * A press engages the robot-wide stop at once; the HMI then sends state=ENGAGED every
 * period until it accepts the MIB's clear attempt. estop-core's README and
 * docs/plans/estop.md have the protocol.
 */

#include <cstdint>

#include "estop/estop.hpp"
#include "hmi_rtps_spec.hpp"

/// The ESTOP_PIN setting's values: which header pin the switch is wired to.
enum class EStopPin : uint8_t { GPIO2 = 0, GPIO47 = 1 };

/// The GPIO number behind an ESTOP_PIN value.
constexpr int estop_gpio(EStopPin pin) { return pin == EStopPin::GPIO47 ? 47 : 2; }

/// The e-stop node. app_main sets its input from the settings and starts it before RTPS;
/// rtps_comms attaches it; anything can read active() and status().
inline estop::Node hmi_estop({.name = rammp::kEStopSelf, .require_latch = rammp::kEStopNeedsMib});
