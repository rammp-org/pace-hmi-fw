// A BLOCK_TIMEOUT queue never has a safety task at either end (CS-OWN-04).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::channel_block_on_safety_path()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Task PRODUCER = Task::CONTROL; // a safety task would block
#else
inline constexpr Task PRODUCER = Task::REMOTE_UI;
#endif
inline constexpr std::array CHANNELS2{DRIVE_INTENT, DRIVE_VIEW, MCB_TO_CONTROL,
                                      ChannelRow{Ch::REMOTE_UI_REQ, "REMOTE_UI_REQ", Kind::QUEUE,
                                                 "RemoteUiReqMsg", PRODUCER, Task::UI, 4,
                                                 Full::BLOCK_TIMEOUT}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
