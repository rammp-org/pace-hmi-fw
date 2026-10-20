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

/// The ESTOP_TYPE setting's values. Both switch to GND against the internal pull-up.
enum class EStopType : uint8_t {
  NO = 0, ///< normally open: pressed pulls the pin LOW
  NC = 1, ///< normally closed: pressed opens it, the pull-up takes it HIGH
};

/// The switch input for the two Settings rows.
constexpr estop::Input estop_input(EStopPin pin, EStopType type) {
  return {.gpio_num = pin == EStopPin::GPIO47 ? GPIO_NUM_47 : GPIO_NUM_2,
          .active_level =
              type == EStopType::NC ? estop::ActiveLevel::HIGH : estop::ActiveLevel::LOW,
          .pull = estop::Pull::UP};
}

/// The e-stop node. app_main sets its input from the settings and starts it before RTPS;
/// rtps_comms attaches it; anything can read active() and status().
inline estop::Node hmi_estop({.name = rammp::kEStopSelf, .require_latch = rammp::kEStopNeedsMib});
