#pragma once
// The link state as the rtps_link subject carries it.

#include <cstdint>

namespace hmi::ui {

/// RTPS link health, in the values of main's RtpsLinkState (main/rtps_comms.hpp), which is
/// what the rtps_link subject holds. main static_asserts that each value matches; the network
/// code is not a dependency of the UI island.
enum class LinkState : int32_t {
  NET_FAILED, ///< the link's hardware failed to start
  LINK_DOWN,  ///< no Ethernet link, or not associated with the WiFi network
  NO_IP,      ///< link up, no DHCP lease
  NO_PEER,    ///< no MibStatus within its timeout (or never)
  CONNECTED,  ///< MibStatus arriving
};

} // namespace hmi::ui
