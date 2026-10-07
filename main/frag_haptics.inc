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

} // namespace hmi::feedback

// The DRV2605 (hmi::feedback::Haptics). app_main builds it (app-main-shrink V6)
// and points this at it before any task starts; the rest of the unit plays it
// through haptic_play below. Read from the LVGL task and the self test's.
static hmi::feedback::Haptics *haptics = nullptr;
using hmi::feedback::kHapticBuzzSlots;

// HAPTIC TEST settings row -> kHapticBuzzDuration of vibration; the unlock and
// hold clicks; a refusal's double click. Returns false (having logged) if there
// is no motor or the I2C burst fails, so callers can leave their own UI alone.
static bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots) {
  return haptics != nullptr && haptics->play(w, slots);
}
