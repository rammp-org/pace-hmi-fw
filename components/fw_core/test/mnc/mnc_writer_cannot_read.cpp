// A narrow handle exposes one end only: a Writer cannot read (CS-OWN-06).
// expect-error: has no member named 'read'
#include "fw_core/mailbox.hpp"

struct SpeedMsg {
  static constexpr bool IS_MESSAGE = true;
  int speed_mm_s;
};

void producer(const hmi::fw::Writer<hmi::fw::Mailbox<SpeedMsg>> &out) {
  SpeedMsg msg{};
#ifdef MNC_FAULT
  (void)out.read(msg);
#else
  out.write(msg);
#endif
}
