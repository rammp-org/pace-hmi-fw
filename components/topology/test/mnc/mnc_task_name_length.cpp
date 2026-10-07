// A task name fits FreeRTOS's 15 characters, or the census cannot find it (TS-DET-09).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::task_name_length()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr std::string_view NAME = "Data Display Task"; // 17 characters
#else
inline constexpr std::string_view NAME = "housekeeping";
#endif
inline constexpr std::array TASKS2{
    UI_ROW, CONTROL_ROW, REMOTE_ROW,
    TaskRow{Task::HOUSEKEEPING, NAME, Role::ISLAND, 6144, 10, 1, false, Start::BOOT}};
static_assert(validate(TASKS2, FOREIGN, COMPONENTS, CHANNELS));
} // namespace mnc
