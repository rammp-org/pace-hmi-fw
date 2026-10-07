// A channel names only declared tasks: TASKS, or an adapter a FOREIGN_TASKS row hosts (CS-OWN-12).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::channel_unknown_task()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Task PRODUCER = Task::NET; // no row
#else
inline constexpr Task PRODUCER = Task::RTPS_RX; // hosted by rtps_worker
#endif
inline constexpr std::array CHANNELS2{DRIVE_INTENT, DRIVE_VIEW, MCB_TO_CONTROL, REMOTE_UI_REQ,
                                      ChannelRow{Ch::LINK_TO_UI, "LINK_TO_UI", Kind::MAILBOX,
                                                 "LinkMsg", PRODUCER, Task::UI, 4,
                                                 Full::OVERWRITE}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
