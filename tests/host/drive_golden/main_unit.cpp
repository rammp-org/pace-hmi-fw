// The drive code of the firmware, compiled on the host: drive_ui's DrivePort (drive_port.hpp,
// moved from main/frag_drive.inc's MainDriveView), verbatim, over the recording shims
// (main_unit_shims.hpp) through MainUnitUi, with its session probed for row coverage
// (probe.hpp), and one DriveAdapter over it (components/drive_adapter), made as main.cpp
// makes it. main_unit_target() is the way in the firmware uses: the same functions, on the
// same inputs.

#include <memory>

#include "main_unit_shims.hpp"
#include "probe.hpp"
#include "scripts.hpp"

// The fragment's session is a probed one (probe.hpp). Test-only, after drive_session.hpp.
#define DriveSession ProbedSession
#include "drive_adapter.hpp"
#include "drive_ui/drive_port.hpp"
#undef DriveSession

#include "adapter_peer.hpp"

namespace {
// As main.cpp: the port, stateless over the Ui, and the one adapter over it.
MainUnitUi main_unit_ui;
constexpr hmi::ui::DrivePort<MainUnitUi> drive_port{&main_unit_ui};
static_assert(hmi::drive_adapter::DrivePort<hmi::ui::DrivePort<MainUnitUi>>);
hmi::drive_adapter::DriveAdapter<hmi::ui::DrivePort<MainUnitUi>> drive_adapter{
    {.view = drive_port}};
// main.cpp's forwarders.
bool drive_session_input(hmi::drive_session::Input input) { return drive_adapter.input(input); }
void drive_wait_poll() { drive_adapter.tick(); }
void drive_unlock_hold_done() { drive_adapter.unlock_hold_done(); }
} // namespace

namespace golden {

namespace {
void reset() {
  std::destroy_at(&drive_adapter);
  std::construct_at(&drive_adapter, decltype(drive_adapter)::Config{.view = drive_port});
  drive_profile_published.store(MIB::DriveProfile::NORMAL);
}
bool input(hmi::drive_session::Input in) { return drive_session_input(in); }
void tick() { drive_wait_poll(); }
void unlock_hold_done() { drive_unlock_hold_done(); }
// main.cpp's drive_exit_gesture completes with exactly this call.
void exit_hold_done() { drive_adapter.exit_hold_done(); }
void set_profile(Profile p) { drive_profile_published.store(static_cast<MIB::DriveProfile>(p)); }
Deadlines deadlines() { return hmi::drive_adapter::DriveAdapterTestPeer::deadlines(drive_adapter); }
} // namespace

// GLD-116, GLD-125: the port's sample as the adapter would take it.
bool port_sample_link_connected() { return drive_port.sample().link_connected; }
bool port_sample_post_ok() { return drive_port.sample().post_ok; }

Target main_unit_target() {
  return Target{&reset,          &input,       &tick,     &unlock_hold_done,
                &exit_hold_done, &set_profile, &deadlines};
}

} // namespace golden
