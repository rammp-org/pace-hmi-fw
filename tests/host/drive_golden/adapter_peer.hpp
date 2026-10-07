#pragma once
// Test-only access to a DriveAdapter's deadlines (declared a friend in drive_adapter.hpp), for
// the goldens' CLOCK_TO steps.

#include "drive_adapter.hpp"
#include "scripts.hpp"

namespace hmi::drive_adapter {
struct DriveAdapterTestPeer {
  template <class View> static golden::Deadlines deadlines(const DriveAdapter<View> &a) {
    return {a.wait_warn_us_, a.wait_until_us_, a.exit_until_us_};
  }
};
} // namespace hmi::drive_adapter
