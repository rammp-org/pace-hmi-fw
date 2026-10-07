// A Task id has exactly one row (CS-OWN-12).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::task_duplicate_id()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Task SECOND = Task::UI; // the UI id twice
#else
inline constexpr Task SECOND = Task::NET;
#endif
inline constexpr std::array TASKS2{
    UI_ROW, CONTROL_ROW, REMOTE_ROW,
    TaskRow{SECOND, "net", Role::ISLAND, 6144, 5, -1, true, Start::BOOT}};
static_assert(validate(TASKS2, FOREIGN, COMPONENTS, CHANNELS));
} // namespace mnc
