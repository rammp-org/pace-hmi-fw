// split_main.py fragment of main.cpp (frag_audio: original lines 6328-6405); one TU, see main.cpp.
static bool load_audio(size_t &out_size, size_t &out_sample_rate) {
  // if the audio_bytes vector is already populated, return the size
  if (audio_bytes.size() > 0) {
    return true;
  }

  // load the audio data. these are configured in the CMakeLists.txt file

  extern const uint8_t click_wav_start[] asm("_binary_click_wav_start");
  extern const uint8_t click_wav_end[] asm("_binary_click_wav_end");
  audio_bytes = std::vector<uint8_t>(click_wav_start, click_wav_end);
  // ensure we have at least a wav header
  if (audio_bytes.size() < 44) {
    audio_bytes.clear();
    return false;
  }
  // get the sample rate from the wav header (bytes 24-27)
  uint32_t sample_rate = *(reinterpret_cast<const uint32_t *>(&audio_bytes[24]));
  // set the audio sample rate accordingly
  // decode the wav file header (first 44 bytes) and remove it
  if (audio_bytes.size() > 44) {
    audio_bytes.erase(audio_bytes.begin(), audio_bytes.begin() + 44);
  }
  out_size = audio_bytes.size();
  out_sample_rate = sample_rate;
  return true;
}

static void click_now(espp::M5StackTab5 &tab5) {
  if (audio_bytes.size() > 0) {
    tab5.play_audio(audio_bytes);
  }
}

// The click: a touch landing, the stick button selecting, a hold completing.
// Silent with Settings "Sounds" off.
static void play_click(espp::M5StackTab5 &tab5) {
  if (sounds_on.load()) {
    click_now(tab5);
  }
}

// "Can't do that": the click twice, kRefusalGapMs apart. One click is a touch
// landing, so two in quick succession reads as a no. Played when a greyed
// control is pressed and when a warning or error banner comes up. A greyed
// press that also raises a banner is one refusal, not two, so a second within
// kRefusalQuietMs is dropped.
//
// The audio queue holds about one click's worth, so the second is queued by a
// one-shot timer rather than appended now. LVGL task, or under lvgl_mutex.
static constexpr uint32_t kRefusalGapMs = 110;
static constexpr uint32_t kRefusalQuietMs = 400;

//
// `warning` is a banner coming up. Sounds off silences the rest -- a greyed
// press is still felt -- but never a warning or an error.
static void play_refusal(bool warning) {
  if (!warning && !sounds_on.load()) {
    return;
  }
  static bool played = false;
  static uint32_t last_ms = 0;
  if (played && lv_tick_elaps(last_ms) < kRefusalQuietMs) {
    return;
  }
  played = true;
  last_ms = lv_tick_get();
  click_now(espp::M5StackTab5::get());
  lv_timer_t *second = lv_timer_create([](lv_timer_t *) { click_now(espp::M5StackTab5::get()); },
                                       kRefusalGapMs, nullptr);
  lv_timer_set_repeat_count(second, 1);
}

// A refused press: the DRV2605's double click, which says the same by touch.
static void refusal_feedback() {
  haptic_play(espp::Drv2605::Waveform::DOUBLE_CLICK, 1);
  play_refusal();
}
