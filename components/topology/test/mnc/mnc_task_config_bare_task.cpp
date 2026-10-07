// task_config() takes a TaskId, built explicitly from a Task (REQ-TOP-07): a bare Task does
// not convert, so every call site names the TaskId whose consteval constructor checks the row.
// expect-error: cannot convert 'hmi::topo::Task' to 'hmi::topo::TaskId'
#include "topology_espp.hpp"

namespace mnc {
using namespace hmi::topo;
#ifdef MNC_FAULT
espp::Task::BaseConfig config() { return Topology::task_config(Task::CONTROL); }
#else
espp::Task::BaseConfig config() { return Topology::task_config(TaskId{Task::CONTROL}); }
#endif
} // namespace mnc
