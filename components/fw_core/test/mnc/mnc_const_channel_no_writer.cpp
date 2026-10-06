// Only a mutable owner hands out a channel's ends: a const Mailbox has no write end (CS-OWN-06).
// expect-error: no matching function for call to 'writer(
#include "fw_core/mailbox.hpp"

struct SpeedMsg {
  static constexpr bool IS_MESSAGE = true;
  int speed_mm_s;
};

hmi::fw::Reader<hmi::fw::Mailbox<SpeedMsg>> consumer_end(hmi::fw::Mailbox<SpeedMsg> &owner,
                                                         const hmi::fw::Mailbox<SpeedMsg> &view) {
#ifdef MNC_FAULT
  (void)hmi::fw::writer(view);
#else
  (void)view;
#endif
  return hmi::fw::reader(owner);
}
