// Task names are unique across TASKS and FOREIGN_TASKS: the census matches by name (TS-DET-09).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::task_duplicate_name()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr std::string_view NAME = "main"; // also a FOREIGN_TASKS name
#else
inline constexpr std::string_view NAME = "net";
#endif
inline constexpr std::array TASKS2{
    UI_ROW, CONTROL_ROW, REMOTE_ROW,
    TaskRow{Task::NET, NAME, Role::ISLAND, 6144, 5, -1, true, Start::BOOT}};
static_assert(validate(TASKS2, FOREIGN, COMPONENTS, CHANNELS));
} // namespace mnc
