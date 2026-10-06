// The drive code of the main.cpp unit, compiled on the host: main/frag_drive.inc, verbatim,
// over the recording shims (main_unit_shims.hpp), with its session probed for row coverage
// (probe.hpp). main_unit_target() is the way in the firmware uses: the same functions, on
// the same inputs.

#include <memory>

#include "main_unit_shims.hpp"
#include "probe.hpp"
#include "scripts.hpp"

// The fragment's session is a probed one (probe.hpp). Test-only, after drive_session.hpp.
#define DriveSession ProbedSession
#include "frag_drive.inc"
#undef DriveSession

namespace golden {

namespace {
void reset() {
  std::destroy_at(&drive_state);
  std::construct_at(&drive_state);
  drive_profile_published.store(MIB::DriveProfile::NORMAL);
}
bool input(hmi::drive_session::Input in) { return drive_session_input(in); }
void tick() { drive_wait_poll(); }
void unlock_hold_done() { drive_unlock_hold_done(); }
void exit_hold_done() { drive_exit_gesture.completed(); }
void set_profile(Profile p) { drive_profile_published.store(static_cast<MIB::DriveProfile>(p)); }
Deadlines deadlines() {
  return {drive_state.wait_warn_us, drive_state.wait_until_us, drive_state.exit_until_us};
}
} // namespace

Target main_unit_target() {
  return Target{&reset,          &input,       &tick,     &unlock_hold_done,
                &exit_hold_done, &set_profile, &deadlines};
}

} // namespace golden
