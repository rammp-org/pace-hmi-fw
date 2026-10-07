// Every task is an island or an adapter (CS-OWN-01, CS-OWN-02).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::task_without_role()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Role ROLE = Role::NONE; // what a row that leaves the role out gets
#else
inline constexpr Role ROLE = Role::ISLAND;
#endif
inline constexpr std::array TASKS2{UI_ROW, CONTROL_ROW, REMOTE_ROW,
                                   TaskRow{Task::NET, "net", ROLE, 6144, 5, -1, true, Start::BOOT}};
static_assert(validate(TASKS2, FOREIGN, COMPONENTS, CHANNELS));
} // namespace mnc
