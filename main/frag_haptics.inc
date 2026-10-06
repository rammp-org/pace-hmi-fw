// split_main.py fragment of main.cpp (frag_haptics: original lines 264-321); one TU, see main.cpp.
// HAPTIC TEST settings row -> kHapticBuzzDuration of vibration.
//
// Set by init_haptic() once the DRV2605 answers, and left null if it does not,
// so the button is inert rather than fatal on a board with no motor fitted.
// The DRV2605 plays the armed waveform on its own once START is written, so
// this is a single I2C write and the LVGL task is never held for the duration.
// espp::I2c and BasePeripheral are both mutex-protected, so sharing the bus
// with the IMU/touch/expander traffic from other tasks is safe.
static espp::Drv2605 *haptic = nullptr;
// Raw register reads from the same DRV2605, for the self test: the espp driver
// covers playback but not the STATUS and GO registers a diagnostic is read back
// through. Set by init_haptic, alongside `haptic`.
static std::function<bool(uint8_t, uint8_t, uint8_t *, size_t)> drv2605_read_register;

// One ALERT_1000MS effect per sequencer slot, so the buzz lasts one second per
// slot. Keep kHapticBuzzDuration in step with kHapticBuzzSlots: it is what the
// label countdown runs on, and a mismatch shows up as a label that clears
// before the motor stops (or lingers after it).
static constexpr uint8_t kHapticBuzzSlots = 2;
static_assert(kHapticBuzzSlots < 8, "need a sequencer slot left for END");
static constexpr auto kHapticBuzzDuration = kHapticBuzzSlots * 1000ms;

// The VIBRATING label the old HAPTIC TEST row swapped to went with that row:
// "Haptic test" is a Skunk Works slot now (action_haptic_test), and the slot
// tiles carry no running state. kHapticBuzzDuration is still what the motor is
// armed for.

// Arms `slots` copies of `w` (plus the END marker) and fires the sequencer.
// The DRV2605 has exactly one waveform sequence, so every caller re-arms before
// starting rather than relying on whatever the previous one left in the slots —
// otherwise the unlock's single click and the HAPTIC TEST buzz would overwrite
// each other. Returns false (having logged) if there is no motor or the I2C
// burst fails, so callers can leave their own UI alone.
//
// Cheap enough for the LVGL task: slots+2 register writes at 400 kHz, and the
// DRV2605 plays the sequence on its own once START is written.
static bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots) {
  if (!haptic) {
    return false; // no motor on this board (init_haptic already logged why)
  }
  std::error_code ec;
  for (uint8_t slot = 0; slot < slots; slot++) {
    if (!haptic->set_waveform(slot, w, ec)) {
      fmt::print("DRV2605 set_waveform failed: {}\n", ec.message());
      return false;
    }
  }
  if (!haptic->set_waveform(slots, espp::Drv2605::Waveform::END, ec)) {
    fmt::print("DRV2605 set_waveform failed: {}\n", ec.message());
    return false;
  }
  if (!haptic->start(ec)) {
    fmt::print("DRV2605 start failed: {}\n", ec.message());
    return false;
  }
  return true;
}

