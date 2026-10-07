// The click sound. Moved from main.cpp's frag_audio.inc (by way of frag_haptics.inc).
#include "feedback/click_sound.hpp"

namespace hmi::feedback {

ClickSound::ClickSound(const Config &config)
    : config_(config) {}

bool ClickSound::load(std::span<const uint8_t> wav, size_t &out_size, size_t &out_sample_rate) {
  // if the audio_bytes vector is already populated, return the size
  if (audio_bytes_.size() > 0) {
    return true;
  }

  // load the audio data (the caller has it from the image: see load_audio)
  audio_bytes_ = std::vector<uint8_t>(wav.begin(), wav.end());
  // ensure we have at least a wav header
  if (audio_bytes_.size() < 44) {
    audio_bytes_.clear();
    return false;
  }
  // get the sample rate from the wav header (bytes 24-27)
  uint32_t sample_rate = *(reinterpret_cast<const uint32_t *>(&audio_bytes_[24]));
  // set the audio sample rate accordingly
  // decode the wav file header (first 44 bytes) and remove it
  if (audio_bytes_.size() > 44) {
    audio_bytes_.erase(audio_bytes_.begin(), audio_bytes_.begin() + 44);
  }
  out_size = audio_bytes_.size();
  out_sample_rate = sample_rate;
  return true;
}

void ClickSound::click() {
  if (audio_bytes_.size() > 0) {
    config_.play(audio_bytes_);
  }
}

void ClickSound::play_click() {
  if (config_.sounds_on.load()) {
    click();
  }
}

// The audio queue holds about one click's worth, so the second is queued by a
// one-shot timer rather than appended now. LVGL task, or under lvgl_mutex.
//
// `warning` is a banner coming up. Sounds off silences the rest -- a greyed
// press is still felt -- but never a warning or an error.
void ClickSound::play_refusal(bool warning) {
  if (!warning && !config_.sounds_on.load()) {
    return;
  }
  if (played_ && config_.now_ms() - last_ms_ < kRefusalQuietMs) {
    return;
  }
  played_ = true;
  last_ms_ = config_.now_ms();
  click();
  config_.click_later(kRefusalGapMs, *this);
}

} // namespace hmi::feedback
