// A queue declares a depth from 1 to MAX_QUEUE_DEPTH (CS-OWN-04, CS-FLW-02).
// expect-error: fw::Queue: DEPTH must be 1..MAX_QUEUE_DEPTH (CS-FLW-02)
#include "fw_core/queue.hpp"

struct KeyMsg {
  static constexpr bool IS_MESSAGE = true;
  int key;
};

#ifdef MNC_FAULT
inline constexpr std::size_t DEPTH = 0;
#else
inline constexpr std::size_t DEPTH = 1;
#endif

void build() { hmi::fw::Queue<KeyMsg, DEPTH, hmi::fw::FullPolicy::RAISE_FAULT> queue({}); }
