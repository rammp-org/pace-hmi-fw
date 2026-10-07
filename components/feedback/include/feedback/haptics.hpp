#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "base_component.hpp"
#include "drv2605.hpp"
#include "i2c.hpp"
#include "logger.hpp"

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

} // namespace hmi::feedback
