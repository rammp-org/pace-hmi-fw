// The DRV2605 haptic motor. Moved from main.cpp's frag_haptics.inc (and frag_da7280.inc
// before it).
#include "feedback/haptics.hpp"

#include <system_error>
#include <thread>

#include "esp_timer.h"
#include "format.hpp"

using namespace std::chrono_literals;

namespace hmi::feedback {

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

Haptics::Haptics(const Config &config)
    : BaseComponent("Haptics", config.log_level) {
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
      logger_.error("DRV2605 set_waveform failed: {}", ec.message());
      return false;
    }
  }
  if (!haptic_->set_waveform(slots, espp::Drv2605::Waveform::END, ec)) {
    logger_.error("DRV2605 set_waveform failed: {}", ec.message());
    return false;
  }
  if (!haptic_->start(ec)) {
    logger_.error("DRV2605 start failed: {}", ec.message());
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
