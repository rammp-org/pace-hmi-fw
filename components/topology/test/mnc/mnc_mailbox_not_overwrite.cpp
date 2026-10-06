// A mailbox (or atomic) always overwrites (CS-OWN-03).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::mailbox_not_overwrite()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Full FULL = Full::DROP_NEWEST_COUNT;
#else
inline constexpr Full FULL = Full::OVERWRITE;
#endif
inline constexpr std::array CHANNELS2{DRIVE_INTENT, MCB_TO_CONTROL, REMOTE_UI_REQ,
                                      ChannelRow{Ch::DRIVE_VIEW, "DRIVE_VIEW", Kind::MAILBOX,
                                                 "DriveViewMsg", Task::CONTROL, Task::UI, 30,
                                                 FULL}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
