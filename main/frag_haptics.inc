// split_main.py fragment of main.cpp (frag_haptics: original lines 264-321); one TU, see main.cpp.
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

// How the click sound reaches the speaker and the LVGL task, for app_main's Feedback.
static hmi::feedback::ClickSound::Config click_sound_config() {
  return {
      .sounds_on = applied_settings.sounds_on(),
      .play = [](std::span<const uint8_t> samples) { espp::M5StackTab5::get().play_audio(samples); },
      .now_ms = [] { return lv_tick_get(); },
      // The second click of a refusal: a one-shot LVGL timer, so it runs on the
      // LVGL task like the first.
      .click_later =
          [](uint32_t delay_ms, hmi::feedback::ClickSound &sound) {
            lv_timer_t *second = lv_timer_create(
                [](lv_timer_t *timer) {
                  static_cast<hmi::feedback::ClickSound *>(lv_timer_get_user_data(timer))->click();
                },
                delay_ms, &sound);
            lv_timer_set_repeat_count(second, 1);
          },
  };
}
