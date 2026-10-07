// A queue never overwrites: events are not merged (CS-OWN-03).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::queue_overwrite()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Full FULL = Full::OVERWRITE;
#else
inline constexpr Full FULL = Full::DROP_NEWEST_COUNT;
#endif
inline constexpr std::array CHANNELS2{DRIVE_INTENT, DRIVE_VIEW, MCB_TO_CONTROL, REMOTE_UI_REQ,
                                      ChannelRow{Ch::NAV_KEY, "NAV_KEY", Kind::QUEUE, "NavKeyMsg",
                                                 Task::CONTROL, Task::UI, 16, FULL}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
