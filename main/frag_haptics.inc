// split_main.py fragment of main.cpp (frag_haptics: original lines 264-321); one TU, see main.cpp.
namespace hmi::feedback {

/// @brief The click sound: a touch landing, a select, a hold completing, and the
/// double click that says "can't do that".
///
/// Every sound leaves through Config::play, the one place this class starts the
/// speaker. Today several tasks reach it (the touch task, the LVGL task, an RTPS
/// handler through a banner): hazard H16. Moving that to one writer means giving
/// `play` a queue to the audio owner, and nothing here changes.
class ClickSound {
public:
  /// @brief Where the sound goes, and the UI-task services the refusal needs.
  struct Config {
    const std::atomic<bool> &sounds_on; ///< Settings "Sounds"; read on every play.
    std::function<void(std::span<const uint8_t>)> play; ///< Starts the speaker on these samples.
    std::function<uint32_t()> now_ms;                   ///< The LVGL tick, in ms.
    /// Runs sound.click() once, delay_ms from now, on the LVGL task.
    std::function<void(uint32_t delay_ms, ClickSound &sound)> click_later;
  };

  /// @brief Keeps the config; the samples come with load().
  /// @param config The speaker, the Sounds setting and the UI-task services.
  explicit ClickSound(const Config &config);

  ClickSound(const ClickSound &) = delete;
  ClickSound &operator=(const ClickSound &) = delete;

  /// @brief Takes the click from a WAV file (44-byte header, then the samples).
  /// @param wav The whole file.
  /// @param out_size The number of sample bytes kept.
  /// @param out_sample_rate The sample rate from the header, in Hz.
  /// @return false if the file is shorter than a WAV header. A second call
  ///         returns true and changes nothing.
  bool load(std::span<const uint8_t> wav, size_t &out_size, size_t &out_sample_rate);

  /// @brief The click, whatever the Sounds setting; nothing before load().
  void click();

  /// @brief The click: a touch landing, the stick button selecting, a hold
  /// completing. Silent with Settings "Sounds" off.
  void play_click();

  /// @brief "Can't do that": the click twice, kRefusalGapMs apart.
  /// @param warning A banner coming up: sounds even with Sounds off.
  void play_refusal(bool warning = false);

private:
  // "Can't do that": the click twice, kRefusalGapMs apart. One click is a touch
  // landing, so two in quick succession reads as a no. Played when a greyed
  // control is pressed and when a warning or error banner comes up. A greyed
  // press that also raises a banner is one refusal, not two, so a second within
  // kRefusalQuietMs is dropped.
  static constexpr uint32_t kRefusalGapMs = 110;
  static constexpr uint32_t kRefusalQuietMs = 400;

  Config config_;
  std::vector<uint8_t> audio_bytes_;
  bool played_ = false;
  uint32_t last_ms_ = 0;
};

ClickSound::ClickSound(const Config &config) : config_(config) {}

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

/// @brief The user's haptic and sound cues: the DRV2605 and the click sound.
///
/// Built once, in app_main (app-main-shrink V6), before any task that cues starts.
class Feedback {
public:
  /// @brief The motor's bus and boot log, and the sound's config.
  struct Config {
    espp::I2c &i2c;          ///< The Tab5 internal bus the DRV2605 is on.
    espp::Logger &boot_log;  ///< Where the DRV2605 bring-up reports.
    ClickSound::Config sound; ///< The click sound's speaker and services.
  };

  /// @brief Brings the DRV2605 up and readies the click sound.
  /// @param config The motor's bus, the boot log and the sound's config.
  explicit Feedback(const Config &config);

  /// @brief The DRV2605.
  /// @return The motor driver.
  Haptics &haptics() { return haptics_; }

  /// @brief The click sound.
  /// @return The sound.
  ClickSound &sound() { return sound_; }

  /// @brief A refused press: the DRV2605's double click, which says the same by
  /// touch, and the refusal sound.
  void refusal();

private:
  Haptics haptics_;
  ClickSound sound_;
};

Feedback::Feedback(const Config &config)
    : haptics_({.i2c = config.i2c, .boot_log = config.boot_log}), sound_(config.sound) {}

void Feedback::refusal() {
  haptics_.play(espp::Drv2605::Waveform::DOUBLE_CLICK, 1);
  sound_.play_refusal();
}

} // namespace hmi::feedback

// The user's haptic and sound cues (hmi::feedback::Feedback). app_main builds
// them (app-main-shrink V6) and points this at them before any task starts; the
// rest of the unit cues through haptic_play below, and through play_click,
// play_refusal and refusal_feedback (after app_main, with load_audio).
static hmi::feedback::Feedback *feedback = nullptr;
using hmi::feedback::kHapticBuzzSlots;
// The self test reports whether the DA7280 answered the boot scan.
using hmi::feedback::kDa7280Address;

// HAPTIC TEST settings row -> kHapticBuzzDuration of vibration; the unlock and
// hold clicks; a refusal's double click. Returns false (having logged) if there
// is no motor or the I2C burst fails, so callers can leave their own UI alone.
static bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots) {
  return feedback != nullptr && feedback->haptics().play(w, slots);
}

// How the cues reach the board and the LVGL task, for app_main's Feedback.
static hmi::feedback::Feedback::Config feedback_config(espp::Logger &logger, espp::I2c &i2c) {
  return {
      .i2c = i2c,
      .boot_log = logger,
      .sound =
          {
              .sounds_on = sounds_on,
              .play =
                  [](std::span<const uint8_t> samples) {
                    espp::M5StackTab5::get().play_audio(samples);
                  },
              .now_ms = [] { return lv_tick_get(); },
              // The second click of a refusal: a one-shot LVGL timer, so it runs
              // on the LVGL task like the first.
              .click_later =
                  [](uint32_t delay_ms, hmi::feedback::ClickSound &sound) {
                    lv_timer_t *second = lv_timer_create(
                        [](lv_timer_t *timer) {
                          static_cast<hmi::feedback::ClickSound *>(lv_timer_get_user_data(timer))
                              ->click();
                        },
                        delay_ms, &sound);
                    lv_timer_set_repeat_count(second, 1);
                  },
          },
  };
}
