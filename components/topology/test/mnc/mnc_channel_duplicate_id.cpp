// A channel id has exactly one row (CS-OWN-12).
// expect-error: call to non-'constexpr' function
// expect-error: 'void hmi::topo::broken_rule::channel_duplicate_id()'
#include "mnc_tables.hpp"

namespace mnc {
#ifdef MNC_FAULT
inline constexpr Ch ID = Ch::DRIVE_VIEW; // DRIVE_VIEW's id twice
#else
inline constexpr Ch ID = Ch::STICK_VIEW;
#endif
inline constexpr std::array CHANNELS2{DRIVE_INTENT, DRIVE_VIEW, MCB_TO_CONTROL, REMOTE_UI_REQ,
                                      ChannelRow{ID, "STICK_VIEW", Kind::MAILBOX, "StickViewMsg",
                                                 Task::CONTROL, Task::UI, 30, Full::OVERWRITE}};
static_assert(validate(TASKS, FOREIGN, COMPONENTS, CHANNELS2));
} // namespace mnc
