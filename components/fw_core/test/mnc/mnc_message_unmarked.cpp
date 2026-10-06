// A channel rejects a type that is not marked as a message (CS-OWN-05).
// expect-error: fw::Message: a message must be marked with IS_MESSAGE = true (CS-OWN-05)
#include "fw_core/mailbox.hpp"

struct LinkMsg {
#ifndef MNC_FAULT
  static constexpr bool IS_MESSAGE = true;
#endif
  bool up;
};

void build() { hmi::fw::Mailbox<LinkMsg> mailbox({}); }
