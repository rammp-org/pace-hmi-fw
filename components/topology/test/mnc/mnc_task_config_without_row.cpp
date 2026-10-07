// Task settings come only from a TASKS row (CS-CON-02): an adapter that runs on a foreign
// task has none, and asking for them does not compile.
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::task_config_without_task_row()'
#include "topology_espp.hpp"

namespace mnc {
using namespace hmi::topo;
#ifdef MNC_FAULT
inline constexpr Task TASK = Task::RTPS_RX; // runs on espp's rtps_worker
#else
inline constexpr Task TASK = Task::CONTROL;
#endif
espp::Task::BaseConfig config() { return Topology::task_config(TaskId{TASK}); }
} // namespace mnc
