// A TaskId is built at compile time only (consteval constructor), so the TASKS row is checked
// before the firmware runs: an id known only at run time does not compile (REQ-TOP-07).
// expect-error: 'task' is not a constant expression
#include "topology_espp.hpp"

namespace mnc {
using namespace hmi::topo;
#ifdef MNC_FAULT
espp::Task::BaseConfig config(Task task) { return Topology::task_config(TaskId{task}); }
#else
espp::Task::BaseConfig config(Task /*task*/) {
  return Topology::task_config(TaskId{Task::CONTROL});
}
#endif
} // namespace mnc
