// pace-hmi-fw: RtpsParticipant::matched_readers, our addition to the vendored espp rtps
// (owner decision T1; components/espp_rtps/README.md, offered upstream). The facade keeps its
// engine writers private; this reads the count the engine keeps for each (Writer's m_proxies,
// the remote readers SEDP matched, under the writer's own lock in getProxiesCount).
//
// Lock-free on the facade's side: our code takes no lock (the L0 ratchet, CS-OWN-08), so it is
// not synchronised with add_writer(), remove_writer() or stop(), the only writers of writers_.
// The caller keeps them from running: main/rtps_comms.cpp asks under its endpoints lock, which
// also covers its add_writer() calls and its teardown flag. Upstream would take mutex_ here.

#include "rtps_participant.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "rtps/entities/Writer.hpp"

std::uint32_t espp::RtpsParticipant::matched_readers(std::string_view topic) const {
  // A linear search: a handful of writers, and no std::string built for the lookup.
  const auto it =
      std::ranges::find_if(writers_, [topic](const auto &w) { return w.first == topic; });
  if (it == writers_.end() || it->second == nullptr) {
    return 0;
  }
  return it->second->getProxiesCount();
}
