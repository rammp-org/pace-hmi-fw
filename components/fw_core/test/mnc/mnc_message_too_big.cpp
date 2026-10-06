// A channel rejects a message over 128 bytes (CS-OWN-05).
// expect-error: fw::Message: a message must be at most 128 bytes (CS-OWN-05)
#include <array>

#include "fw_core/queue.hpp"

struct LogChunkMsg {
  static constexpr bool IS_MESSAGE = true;
#ifdef MNC_FAULT
  std::array<char, 129> text;
#else
  std::array<char, 128> text;
#endif
};

void build() { hmi::fw::Queue<LogChunkMsg, 4, hmi::fw::FullPolicy::DROP_NEWEST_COUNT> queue({}); }
