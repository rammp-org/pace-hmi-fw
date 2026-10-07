// A queue declares a depth from 1 to MAX_QUEUE_DEPTH (CS-OWN-04, CS-FLW-02).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::queue_depth_out_of_range()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr std::uint32_t DEPTH = 0;
#else
inline constexpr std::uint32_t DEPTH = 1;
#endif
inline constexpr std::array CHANNELS2{DRIVE_INTENT, DRIVE_VIEW, MCB_TO_CONTROL, REMOTE_UI_REQ,
                                      ChannelRow{Ch::NAV_KEY, "NAV_KEY", Kind::QUEUE, "NavKeyMsg",
                                                 Task::CONTROL, Task::UI, DEPTH,
                                                 Full::DROP_NEWEST_COUNT}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
