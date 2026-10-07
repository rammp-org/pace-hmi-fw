// A queue into a safety task never drops silently: RAISE_FAULT, not DROP_NEWEST_COUNT (CS-OWN-04).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::channel_drop_into_safety()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Full FULL = Full::DROP_NEWEST_COUNT; // control is a safety task
#else
inline constexpr Full FULL = Full::RAISE_FAULT;
#endif
inline constexpr std::array CHANNELS2{DRIVE_VIEW, MCB_TO_CONTROL, REMOTE_UI_REQ,
                                      ChannelRow{Ch::SEAT_REQUEST, "SEAT_REQUEST", Kind::QUEUE,
                                                 "SeatRequestMsg", Task::UI, Task::CONTROL, 8,
                                                 FULL}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
