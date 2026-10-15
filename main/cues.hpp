#pragma once

/**
 * @file cues.hpp
 * @brief The user's haptic and sound cues as the rest of main and the UiApp reach them: the one
 *        hmi::feedback::Feedback (app_main's), the click, the refusal, the haptic clicks, and
 *        hmi_ui's CuesPort over them. Composition: it joins components/feedback to the UI's
 *        port and the board's callbacks. Moved from main.cpp unchanged.
 */

#include <cstddef>
#include <cstdint>

#include "drv2605.hpp"
#include "feedback/feedback.hpp"
#include "hmi_ui/app_ports.hpp"
#include "m5stack-tab5.hpp"

/// @brief Points the cues at app_main's Feedback. app_main, once, before any task starts; the
/// Feedback lives as long as app_main, which never returns.
/// @param cues The DRV2605 haptics and the click sound.
void cues_attach(hmi::feedback::Feedback &cues);

/// @brief A DRV2605 waveform: the HAPTIC TEST settings row's buzz, the unlock and hold clicks,
/// a refusal's double click.
/// @param w The waveform.
/// @param slots How many sequence slots it fills (kHapticBuzzSlots for the test buzz).
/// @return false (having logged) if there is no motor or the I2C burst fails, so callers can
/// leave their own UI alone; also false before cues_attach.
bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots);

/// @brief The click: a touch landing, the stick button selecting, a hold completing. Silent with
/// Settings "Sounds" off. Every caller passes the one Tab5, which is the speaker the Feedback
/// plays on.
/// @param tab5 The board (unused: the Feedback holds the speaker).
void play_click(espp::M5StackTab5 &tab5);

/// @brief "Can't do that", heard. LVGL task, or under lvgl_mutex.
/// @param warning A banner coming up: it sounds even with Sounds off.
void play_refusal(bool warning = false);

/// @brief A refused press: the DRV2605's double click, which says the same by touch.
void refusal_feedback();

/// @brief Loads the click sound's samples (click.wav, embedded by main/CMakeLists.txt).
/// @param out_size Set to the samples' size, bytes.
/// @param out_sample_rate Set to their sample rate, Hz.
/// @return Whether they loaded; without them the click plays nothing.
bool load_audio(size_t &out_size, size_t &out_sample_rate);

/// The UiApp's cues (hmi_ui/app_ports.hpp): the click, the refusal, the haptic click and test.
/// Constant, and visible here, because the UiApp's constexpr constructor reads it (main.cpp's
/// constinit ui_app).
inline constexpr hmi::ui::CuesPort kCuesPort{
    .click = [] { play_click(espp::M5StackTab5::get()); },
    .refusal = [](bool warning) { play_refusal(warning); },
    .refused = refusal_feedback,
    .haptic_click = [] { (void)haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1); },
    .haptic_test =
        [] {
          (void)haptic_play(espp::Drv2605::Waveform::ALERT_1000MS, hmi::feedback::kHapticBuzzSlots);
        },
};
