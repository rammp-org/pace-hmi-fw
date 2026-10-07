// Only a channel's consumer gets its read end (CS-OWN-06): the Topology checks the caller's
// context token against the row.
// expect-error: topology: only the channel's consumer task gets its read end (CS-OWN-06)
#include "topology_espp.hpp"

namespace mnc {
using namespace hmi::topo;
namespace fw = hmi::fw;

class UiIsland {
public:
  static constexpr Task TASK = Task::UI;
  void wire(Topology &topo) {
    const fw::Context<UiIsland> ctx{fw::Passkey<UiIsland>{}};
#ifdef MNC_FAULT
    auto end = topo.reader<DriveIntentCh>(ctx); // control consumes DRIVE_INTENT
#else
    auto end = topo.reader<DriveViewCh>(ctx);
#endif
    static_cast<void>(end);
  }
};
} // namespace mnc
