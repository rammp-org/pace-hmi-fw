/**
 * @file cues.cpp
 * @brief The user's haptic and sound cues, wired (cues.hpp). Moved from main.cpp unchanged.
 */

#include "cues.hpp"

// The user's haptic and sound cues (hmi::feedback::Feedback). app_main builds
// them (app-main-shrink V6) and points this at them (cues_attach) before any task starts;
// main cues through haptic_play, play_click, play_refusal and refusal_feedback.
static hmi::feedback::Feedback *feedback = nullptr;

// HAPTIC TEST settings row -> kHapticBuzzDuration of vibration; the unlock and
// hold clicks; a refusal's double click. Returns false (having logged) if there
// is no motor or the I2C burst fails, so callers can leave their own UI alone.
bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots) {
  return feedback != nullptr && feedback->haptics().play(w, slots);
}

void cues_attach(hmi::feedback::Feedback &cues) { feedback = &cues; }

// The click sound's samples: click.wav, embedded by main/CMakeLists.txt
// (EMBED_TXTFILES). The sound itself is hmi::feedback::ClickSound.
bool load_audio(size_t &out_size, size_t &out_sample_rate) {
  extern const uint8_t click_wav_start[] asm("_binary_click_wav_start");
  extern const uint8_t click_wav_end[] asm("_binary_click_wav_end");
  return feedback->sound().load({click_wav_start, click_wav_end}, out_size, out_sample_rate);
}

// The click: a touch landing, the stick button selecting, a hold completing.
// Silent with Settings "Sounds" off. Every caller passes the one Tab5, which is
// the speaker the Feedback plays on.
void play_click(espp::M5StackTab5 & /*tab5*/) { feedback->sound().play_click(); }

// "Can't do that", heard. `warning` is a banner coming up: it sounds even with
// Sounds off. LVGL task, or under lvgl_mutex.
void play_refusal(bool warning) { feedback->sound().play_refusal(warning); }

// A refused press: the DRV2605's double click, which says the same by touch.
void refusal_feedback() { feedback->refusal(); }
