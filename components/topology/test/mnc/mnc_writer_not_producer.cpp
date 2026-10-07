// Only a channel's producer gets its write end (CS-OWN-06): the Topology checks the caller's
// context token against the row.
// expect-error: topology: only the channel's producer task gets its write end (CS-OWN-06)
#include "topology_espp.hpp"

namespace mnc {
using namespace hmi::topo;
namespace fw = hmi::fw;

class ControlIsland {
public:
  static constexpr Task TASK = Task::CONTROL;
  void wire(Topology &topo) {
    const fw::Context<ControlIsland> ctx{fw::Passkey<ControlIsland>{}};
#ifdef MNC_FAULT
    auto end = topo.writer<DriveIntentCh>(ctx); // UI produces DRIVE_INTENT
#else
    auto end = topo.writer<DriveViewCh>(ctx);
#endif
    static_cast<void>(end);
  }
};
} // namespace mnc
