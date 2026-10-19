#pragma once

/**
 * @file estop.hpp
 * @brief The e-stop switch and its side of the rammp::EStop protocol
 *        (messages/estop_message.hpp; the design is docs/plans/estop.md).
 *
 * A press engages the robot-wide stop at once. From then on this HMI sends
 * engaged=true every kEStopPolicy.period, released switch or not, until it accepts
 * a clear from the MIB (rammp::kEStopClearer); otherwise it sends engaged=false as
 * the heartbeat the MIB's deadman watches.
 *
 * One task owns all of it. The switch reaches it through an edge interrupt (no
 * polling: a press shorter than any poll period is still counted), received
 * messages through a static queue. Nothing here allocates after estop_start().
 */

#include <cstdint>
#include <span>

/// The ESTOP_PIN setting's values: which header pin the switch is wired to.
enum class EStopPin : uint8_t { GPIO2 = 0, GPIO47 = 1 };
/// The ESTOP_TYPE setting's values. Both contacts switch to GND against the pull-up.
enum class EStopContact : uint8_t {
  NO = 0, ///< normally open: pressed pulls the pin LOW
  NC = 1, ///< normally closed: pressed opens it, the pull-up takes it HIGH
};

/// What the rest of the HMI acts on. A copy, so any task can hold it.
struct EStopView {
  bool local = false;     ///< this HMI's switch engaged it; held until the MIB clears it
  bool remote = false;    ///< the MIB says the stop is latched
  bool mib_fresh = false; ///< an EStop from the MIB within kEStopPolicy.timeout
  bool armed = false;     ///< the interrupt is set up on the configured pin
  EStopPin pin = EStopPin::GPIO2;
  EStopContact contact = EStopContact::NO;
  uint32_t presses = 0;        ///< press edges counted since boot
  uint32_t clears = 0;         ///< clears accepted
  uint32_t clears_refused = 0; ///< clears turned down (stale, not ours, switch held, old ack)
  uint32_t rx_errors = 0;      ///< received messages that did not decode, or did not fit the queue
  uint32_t tx_errors = 0;      ///< sends refused once the RTPS endpoint existed
  uint32_t seq = 0;            ///< the last seq sent
  char remote_source[16] = {}; ///< the MIB's source field, when remote
  char remote_reason[48] = {}; ///< and its reason
};

/// Arms the switch and starts the e-stop task. Once, from app_main, after settings_load()
/// and before RTPS starts. False = it could not start, which estop_active() reports as
/// engaged.
bool estop_start(EStopPin pin, EStopContact contact);

/// The Settings rows changed: re-arm on this pin and contact. Any task.
void estop_configure(EStopPin pin, EStopContact contact);

/// True while driving must not happen: this HMI engaged it, the MIB says the stop is
/// latched, the task is not running, or (with rammp::kEStopNeedsMib) the MIB's e-stop
/// messages are not arriving. Any task; one atomic load.
bool estop_active();

/// A consistent copy of the whole state, for the UI and the log. Any task.
EStopView estop_view();

/// rtps_comms: a sample from rammp::kEStop's reader. Runs on the RTPS receive task;
/// decodes in place and queues the result without allocating.
void estop_on_sample(std::span<const uint8_t> cdr_payload);
