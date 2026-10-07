#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

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
    const std::atomic<bool> &sounds_on;                 ///< Settings "Sounds"; read on every play.
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

} // namespace hmi::feedback
