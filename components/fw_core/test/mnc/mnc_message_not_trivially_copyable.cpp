// A channel rejects a message that is not trivially copyable (CS-OWN-05).
// expect-error: fw::Message: a message must be trivially copyable (CS-OWN-05)
#include <array>
#include <string>

#include "fw_core/mailbox.hpp"

struct NameMsg {
  static constexpr bool IS_MESSAGE = true;
#ifdef MNC_FAULT
  std::string name;
#else
  std::array<char, 32> name;
#endif
};

void build() { hmi::fw::Mailbox<NameMsg> mailbox({}); }
