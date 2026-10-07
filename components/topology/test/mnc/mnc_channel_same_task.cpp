// A channel connects two different tasks (CS-OWN-12).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::channel_same_task()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Task CONSUMER = Task::UI; // the producer
#else
inline constexpr Task CONSUMER = Task::CONTROL;
#endif
inline constexpr std::array CHANNELS2{DRIVE_VIEW, MCB_TO_CONTROL, REMOTE_UI_REQ,
                                      ChannelRow{Ch::DRIVE_INTENT, "DRIVE_INTENT", Kind::QUEUE,
                                                 "DriveIntentMsg", Task::UI, CONSUMER, 8,
                                                 Full::RAISE_FAULT}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
