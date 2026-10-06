// The atomic channel accepts only always-lock-free types (CS-OWN-03).
// expect-error: fw::AtomicValue: std::atomic<T> must be always lock-free (CS-OWN-03)
#include <cstdint>

#include "fw_core/atomic_value.hpp"

struct Pose {
#ifdef MNC_FAULT
  std::int32_t x, y, z, heading; // 16 bytes: not lock-free on the target or the host
#else
  std::int16_t x, y; // 4 bytes
#endif
};

void build() { hmi::fw::AtomicValue<Pose> pose({}); }
