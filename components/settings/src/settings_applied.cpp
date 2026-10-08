#include "settings_applied.hpp"

namespace hmi::settings {

bool AppliedSettings::apply(int param, int value) noexcept {
  switch (param) {
  case SETTINGS_PARAM_STICK_SENSITIVITY:
    stick_sensitivity_.store(value);
    return true;
  case SETTINGS_PARAM_DRIVE_SPEED:
    drive_speed_.store(value);
    return true;
  case SETTINGS_PARAM_STICK_INVERT_X:
    stick_invert_x_.store(value != 0);
    return true;
  case SETTINGS_PARAM_STICK_INVERT_Y:
    stick_invert_y_.store(value != 0);
    return true;
  case SETTINGS_PARAM_STICK_SWAP:
    stick_swap_.store(value != 0);
    return true;
  case SETTINGS_PARAM_SOUNDS:
    sounds_on_.store(value != 0);
    return true;
  default:
    return false;
  }
}

} // namespace hmi::settings
