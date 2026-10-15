#pragma once
// hmi::stick: the boot self check of the stick's mapping math (J2 of the vendored-joystick
// review, moved out of the espp fork's espp::joystick_selftest()): 14 of its 17 checks, on the
// same values, against espp's 2-axis Joystick for the X/Y gimbal and the twist's own range
// mapper, the way StickPipeline wires them (J1). The other 3 asked whether a joystick has a z
// axis; espp's Joystick has none, so they have no counterpart. No logging.

namespace hmi::stick {

/// Runs every check of the mapping math: rectangular and circular X/Y (centre, rails, half
/// deflection, the circular clamp, the centre dead zone and the rescale outside it), the
/// twist's dead bands, and that the twist stays independent of the X/Y gimbal.
/// @return true when every check passed
[[nodiscard]] bool mapping_selftest();

} // namespace hmi::stick
