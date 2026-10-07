#pragma once
// The stick math of the ADC task as it was before the extraction, copied verbatim into a
// host harness (legacy_adc_cycle.cpp). It is the characterisation oracle: it produced
// golden_stick.inc, and the golden test replays the table through it to prove the table
// still describes that code.

#include "stick_vectors.hpp"

namespace stick_test::legacy {

/// Power-on state for a scenario: the joystick built on `cal` (main.cpp:1441-1447), the key
/// trigger released, joy_key and remote_key 0.
void reset(CalId cal);

/// One ADC-task cycle with the vector's inputs; returns what it did. Inputs only: the
/// output half of `v` is ignored.
StickOutputs cycle(const StickVector &v);

} // namespace stick_test::legacy
