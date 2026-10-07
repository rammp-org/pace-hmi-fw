// Only the island can create its context token (CS-OWN-07, passkey).
// expect-error: is private within this context
#include "fw_core/context.hpp"

void use(const hmi::fw::ContextBase &ctx);

class Island {
public:
  static void cycle() {
    const hmi::fw::Context<Island> ctx{hmi::fw::Passkey<Island>{}};
    use(ctx);
  }
};

void outsider() {
#ifdef MNC_FAULT
  const hmi::fw::Context<Island> ctx{hmi::fw::Passkey<Island>{}};
  use(ctx);
#else
  Island::cycle();
#endif
}
