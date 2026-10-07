// A component has exactly one row (CS-OWN-12).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::component_duplicate_name()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr std::string_view NAME = "stick"; // also STICK's name
#else
inline constexpr std::string_view NAME = "drive_session";
#endif
inline constexpr std::array COMPONENTS2{HMI_UI, STICK, ComponentRow{NAME, Task::CONTROL}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS2, CHANNELS));
} // namespace mnc
