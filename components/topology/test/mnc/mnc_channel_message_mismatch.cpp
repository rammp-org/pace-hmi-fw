// A `using XCh = Channel<Ch::X, M>` line names the message type its CHANNELS row names
// (CS-OWN-12): a channel cannot carry a type the table did not declare.
// expect-error: topology::Channel: the message type must match its CHANNELS row (CS-OWN-12)
#include "topology.hpp"

namespace mnc {
using namespace hmi::topo;
#ifdef MNC_FAULT
using Message = McbStatusMsg; // DIAG's row says DiagMsg
#else
using Message = DiagMsg;
#endif
using WrongDiagCh = Channel<Ch::DIAG, Message>;
static_assert(WrongDiagCh::ROW.id == Ch::DIAG);
} // namespace mnc
