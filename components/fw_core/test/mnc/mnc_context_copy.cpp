// A context token cannot be copied (CS-OWN-07).
// expect-error: use of deleted function
// expect-error: Context(const hmi::fw::Context<Island>&)
#include "fw_core/context.hpp"

void use(const hmi::fw::ContextBase &ctx);

class Island {
public:
  void cycle() {
    const hmi::fw::Context<Island> ctx{hmi::fw::Passkey<Island>{}};
#ifdef MNC_FAULT
    const hmi::fw::Context<Island> copy = ctx;
    use(copy);
#else
    const hmi::fw::Context<Island> &same = ctx;
    use(same);
#endif
  }
};
