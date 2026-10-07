// split_main.py fragment of main.cpp (frag_haptics: original lines 264-321); one TU, see main.cpp.
namespace hmi::feedback {

// One ALERT_1000MS effect per sequencer slot, so the buzz lasts one second per
// slot. Keep kHapticBuzzDuration in step with kHapticBuzzSlots: it is what the
// label countdown runs on, and a mismatch shows up as a label that clears
// before the motor stops (or lingers after it).
inline constexpr uint8_t kHapticBuzzSlots = 2;
static_assert(kHapticBuzzSlots < 8, "need a sequencer slot left for END");
inline constexpr auto kHapticBuzzDuration = kHapticBuzzSlots * std::chrono::milliseconds(1000);

// The VIBRATING label the old HAPTIC TEST row swapped to went with that row:
// "Haptic test" is a Skunk Works slot now (action_haptic_test), and the slot
// tiles carry no running state. kHapticBuzzDuration is still what the motor is
// armed for.

/// @brief The DRV2605 haptic motor driver: the motor on the PCB's JST connector.
///
/// Built once, in app_main (app-main-shrink V6): the constructor brings the chip
/// up on the I2C bus. If it does not answer, the object stays inert rather than
/// fatal, so play() returns false on a board with no motor fitted.
///
/// The DRV2605 plays the armed waveform on its own once START is written, so a
/// play is a few I2C writes and the LVGL task is never held for the duration.
/// espp::I2c and BasePeripheral are both mutex-protected, so sharing the bus
/// with the IMU/touch/expander traffic from other tasks is safe.
class Haptics : public espp::BaseComponent {
public:
  /// @brief What the DRV2605 hangs off.
  struct Config {
    espp::I2c &i2c;         ///< The Tab5 internal bus the DRV2605 is on.
    espp::Logger &boot_log; ///< Where the bring-up reports (app_main's logger).
    espp::Logger::Verbosity log_level{espp::Logger::Verbosity::WARN}; ///< This class's own log.
  };

  /// @brief Brings the DRV2605 up and selects its effect library.
  /// @param config The bus and the boot log.
  explicit Haptics(const Config &config);

  Haptics(const Haptics &) = delete;
  Haptics &operator=(const Haptics &) = delete;

  /// @brief Arms `slots` copies of `w` (plus the END marker) and fires the sequencer.
  /// @param w The waveform each slot plays.
  /// @param slots How many slots play it, back to back.
  /// @return false (having logged) if there is no motor or the I2C burst fails.
  bool play(espp::Drv2605::Waveform w, uint8_t slots);

  /// @brief The DRV2605 STATUS register, for the self test.
  /// @return The register, or nullopt if the chip is not up or the read fails.
  std::optional<uint8_t> status() const;

  /// @brief Plays one click and waits for the chip to report it done.
  /// @param detail What happened, for the self test's report.
  /// @return true if it finished, false if it did not, nullopt if it could not be tried.
  std::optional<bool> play_click(std::string &detail);

private:
  std::shared_ptr<espp::I2c::Device<uint8_t>> device_;
  std::optional<espp::Drv2605> drv2605_;
  // Set once the library is selected, and left null if the chip does not answer,
  // so a half-configured driver can never be reached from the button.
  espp::Drv2605 *haptic_ = nullptr;
  // Raw register reads from the same DRV2605, for the self test: the espp driver
  // covers playback but not the STATUS and GO registers a diagnostic is read back
  // through. Set alongside `haptic_`.
  std::function<bool(uint8_t, uint8_t, uint8_t *, size_t)> read_register_;
};

// DRV2605 haptic motor driver (the motor on the PCB's JST connector).
//
// No waveform is armed at start-up: the sequencer slots are shared between the
// HAPTIC TEST buzz and the unlock click, so play() writes them on every play
// instead. The sequencer has 8 slots, and the last one used must hold the
// terminator.
//
// The bundled motor is an ERM; for an LRA swap MotorType::ERM -> LRA and
// Library::ERM_1 -> Library::LRA. ERM_1 is LIBRARY register value 2, i.e. TI's
// "Library B" (3 V rated ERM) — the enum names are offset by one from the
// datasheet's library numbering.
//
// The Tab5 internal bus runs at 1 MHz; the DRV2605 is fast-mode only (400 kHz
// max), so this device gets its own per-device clock rather than the bus rate.
static constexpr uint32_t kDrv2605SclSpeedHz = 400 * 1000;

// DRV2605 registers the self test reads back (datasheet, register map).
static constexpr uint8_t kDrv2605RegStatus = 0x00; // DEVICE_ID[7:5] DIAG_RESULT[3] OT[1] OC[0]
static constexpr uint8_t kDrv2605RegGo = 0x0C;

Haptics::Haptics(const Config &config) : BaseComponent("Haptics", config.log_level) {
  espp::Logger &logger = config.boot_log;
  espp::I2c &i2c = config.i2c;
  std::error_code ec;
  // Members: both outlive the constructor, because play() reaches the driver
  // (and the driver the device) for the rest of the run.
  device_ = i2c.add_device<uint8_t>(
      {
          .device_address = espp::Drv2605::DEFAULT_ADDRESS,
          .timeout_ms = static_cast<int>(i2c.config().timeout_ms),
          .scl_speed_hz = kDrv2605SclSpeedHz,
          .log_level = espp::Logger::Verbosity::WARN,
      },
      ec);
  if (!device_) {
    logger.error("Could not create DRV2605 I2C device: {}", ec.message());
    return;
  }

  drv2605_.emplace(espp::Drv2605::Config{
      .device_address = espp::Drv2605::DEFAULT_ADDRESS,
      .write = espp::make_i2c_addressed_write(device_),
      .read_register = espp::make_i2c_addressed_read_register(device_),
      .motor_type = espp::Drv2605::MotorType::ERM,
      .auto_init = false,
      .log_level = espp::Logger::Verbosity::WARN,
  });

  if (!drv2605_->initialize(ec)) {
    logger.error("DRV2605 init failed at {:#02x} ({}) — check wiring",
                 espp::Drv2605::DEFAULT_ADDRESS, ec.message());
    return;
  }
  if (!drv2605_->select_library(espp::Drv2605::Library::ERM_1, ec)) {
    logger.error("DRV2605 select_library failed: {}", ec.message());
    return;
  }

  // Only published once the library is selected, so a half-configured driver
  // can never be reached from the button.
  haptic_ = &*drv2605_;
  read_register_ = espp::make_i2c_addressed_read_register(device_);
  logger.info("DRV2605 ready: HAPTIC TEST plays a {} ms buzz", kHapticBuzzDuration.count());
}

// The DRV2605 has exactly one waveform sequence, so every caller re-arms before
// starting rather than relying on whatever the previous one left in the slots —
// otherwise the unlock's single click and the HAPTIC TEST buzz would overwrite
// each other.
//
// Cheap enough for the LVGL task: slots+2 register writes at 400 kHz, and the
// DRV2605 plays the sequence on its own once START is written.
bool Haptics::play(espp::Drv2605::Waveform w, uint8_t slots) {
  if (!haptic_) {
    return false; // no motor on this board (the constructor already logged why)
  }
  std::error_code ec;
  for (uint8_t slot = 0; slot < slots; slot++) {
    if (!haptic_->set_waveform(slot, w, ec)) {
      fmt::print("DRV2605 set_waveform failed: {}\n", ec.message());
      return false;
    }
  }
  if (!haptic_->set_waveform(slots, espp::Drv2605::Waveform::END, ec)) {
    fmt::print("DRV2605 set_waveform failed: {}\n", ec.message());
    return false;
  }
  if (!haptic_->start(ec)) {
    fmt::print("DRV2605 start failed: {}\n", ec.message());
    return false;
  }
  return true;
}

std::optional<uint8_t> Haptics::status() const {
  uint8_t status = 0;
  if (!read_register_ ||
      !read_register_(espp::Drv2605::DEFAULT_ADDRESS, kDrv2605RegStatus, &status, 1)) {
    return std::nullopt;
  }
  return status;
}

// GO self-clears when the sequence has finished. That proves the I2C path, the
// sequencer and the output stage run end to end - not that a motor is attached.
// The chip's own actuator diagnostic would, but on the bench unit it reported
// DIAG_RESULT in open- and closed-loop mode alike, so it cannot be told apart
// from a missing motor and is not used (see hap.drv_play in selftest_spec.hpp).
std::optional<bool> Haptics::play_click(std::string &detail) {
  if (!haptic_ || !read_register_) {
    detail = "DRV2605 not initialised";
    return std::nullopt;
  }
  if (!play(espp::Drv2605::Waveform::STRONG_CLICK, 1)) {
    detail = "could not start the click";
    return false;
  }
  const int64_t started = esp_timer_get_time();
  uint8_t go = 1;
  while ((go & 1) != 0 && esp_timer_get_time() - started < 1000 * 1000) {
    std::this_thread::sleep_for(10ms);
    if (!read_register_(espp::Drv2605::DEFAULT_ADDRESS, kDrv2605RegGo, &go, 1)) {
      detail = "GO read failed";
      return std::nullopt;
    }
  }
  if ((go & 1) != 0) {
    detail = "click never finished (GO stuck)";
    return false;
  }
  detail = fmt::format("finished in ~{} ms", (esp_timer_get_time() - started) / 1000);
  return true;
}

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
