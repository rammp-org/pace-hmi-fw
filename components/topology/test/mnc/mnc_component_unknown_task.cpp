// A component runs on a declared task (CS-OWN-12).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::component_unknown_task()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Task TASK = Task::NET; // no row
#else
inline constexpr Task TASK = Task::CONTROL;
#endif
inline constexpr std::array COMPONENTS2{HMI_UI, STICK, ComponentRow{"drive_session", TASK}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS2, CHANNELS));
} // namespace mnc
