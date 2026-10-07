// The user's haptic and sound cues, together. Moved from main.cpp's frag_haptics.inc.
#include "feedback/feedback.hpp"

namespace hmi::feedback {

Feedback::Feedback(const Config &config)
    : haptics_({.i2c = config.i2c, .boot_log = config.boot_log})
    , sound_(config.sound) {}

void Feedback::refusal() {
  haptics_.play(espp::Drv2605::Waveform::DOUBLE_CLICK, 1);
  sound_.play_refusal();
}

} // namespace hmi::feedback
