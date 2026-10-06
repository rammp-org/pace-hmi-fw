#pragma once
// Row coverage for the drive goldens: which rows of TRANSITIONS the scenarios take.
//
// ProbedSession is a DriveSession whose step() first notes the row that (phase, input, guards)
// selects, with the table's own find_row, then steps exactly as DriveSession does. The code
// under test is compiled with `#define DriveSession ProbedSession` (main_unit.cpp, and the
// adapter's harness), after drive_session.hpp, so its session is a probed one. Nothing else
// changes: same phase, same hidden variables, same actions.

#include <array>
#include <cstddef>

#include "drive_session.hpp"

namespace golden {
// Hits per row (TRANSITIONS index); the last entry counts steps that matched no row.
std::array<unsigned, hmi::drive_session::kTransitionCount + 1> &row_hits();
void note_row(hmi::drive_session::Phase p, hmi::drive_session::Input in,
              hmi::drive_session::GuardMask guards);
} // namespace golden

namespace hmi::drive_session {
class ProbedSession : public DriveSession {
public:
  [[nodiscard]] bool step(Input in, const Env &env, Actions &out) noexcept {
    golden::note_row(phase(), in, env_guards(env) | hidden());
    return DriveSession::step(in, env, out);
  }
};
} // namespace hmi::drive_session
