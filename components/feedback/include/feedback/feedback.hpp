#pragma once

#include "feedback/click_sound.hpp"
#include "feedback/haptics.hpp"
#include "i2c.hpp"
#include "logger.hpp"

namespace hmi::feedback {

/// @brief The user's haptic and sound cues: the DRV2605 and the click sound.
///
/// Built once, in app_main (app-main-shrink V6), before any task that cues starts.
class Feedback {
public:
  /// @brief The motor's bus and boot log, and the sound's config.
  struct Config {
    espp::I2c &i2c;           ///< The Tab5 internal bus the DRV2605 is on.
    espp::Logger &boot_log;   ///< Where the DRV2605 bring-up reports.
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

} // namespace hmi::feedback
