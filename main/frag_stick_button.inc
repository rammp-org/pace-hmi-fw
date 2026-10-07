// split_main.py fragment of main.cpp (frag_stick_button: original lines 853-953); one TU, see main.cpp.
static void stick_button_edge(bool active) {
  // lv_subject_set_int runs the observers synchronously on this task, and they
  // touch widgets, so this needs the LVGL lock
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);

  // the panel follows every edge, so contact bounce can't strand it blue: the
  // last edge always wins
  lv_subject_set_int(&button_pressed_subject, active);
  // undebounced on purpose: the MCB wants the raw line state, and this is the
  // same every-edge signal the panel follows
  joy_button_pressed.store(active);

  const int64_t now = esp_timer_get_time();
  static int64_t pressed_at_us = 0;
  if (active) {
    pressed_at_us = now;
  } else if (now - pressed_at_us < kSelectMaxUs) {
    select_key.store(true);
  }

  // only the counter is debounced. espp's GPIO glitch filters are ns-scale and
  // do nothing against millisecond-scale mechanical bounce.
  static int64_t last_press_us = 0;
  if (active && (now - last_press_us) > 30000) {
    last_press_us = now;
    lv_subject_set_int(&button_count_subject, lv_subject_get_int(&button_count_subject) + 1);
  }
}

// Runs on the Button's interrupt task (not in ISR context).
static void gpio48_button_callback(const espp::Interrupt::Event &event) {
  stick_button_edge(event.active);
}

static bool load_audio(size_t &out_size, size_t &out_sample_rate);
static void play_click(espp::M5StackTab5 &tab5);
// "Can't do that", heard (play_refusal) or heard and felt (refusal_feedback);
// defined with play_click. A warning or error sounds even with Sounds off.
static void play_refusal(bool warning = false);
static void refusal_feedback();

// DRV2605 haptic driver: brings the motor on the PCB's JST connector up and
// selects its effect library. The waveform slots are armed per play, by
// haptic_play().
static void init_haptic(espp::Logger &logger, espp::I2c &i2c);

/////////////////////////////////////////////////////////////////////////////
// Push-and-hold gestures
//
// Three places in the HMI ask the user to hold an input for kHoldMs before
// something happens:
//
//   LockedScreen   stick button    enters driving mode  -> DriveScreen
//   DriveScreen    stick button    leaves driving mode  -> LockedScreen
//   JoystickScreen stick button, or a finger on CALIBRATE: starts calibration
//
// Both on the button, never the stick (issue #11): an enable held on the stick
// left the user holding it forward the moment the chair would take it, and
// the chair drove off at full speed.
//
// Everything else that used to be a hold -- enter seat, enter actions, and the
// "pull the stick back to leave" exits on seat, bench, settings, actions,
// diagnostics and log -- is a burger-menu row now, one tap away from anywhere,
// so those gestures and the bars that showed their fill are gone.
//
// The DriveScreen is the odd one out because pulling the stick back is how you
// reverse: an exit on LV_KEY_DOWN there fired every time the user drove
// backwards, so it exits on the stick button instead.
//
// Each gesture's fill runs through a subject: the ring round the padlock and
// Calibrate's meter are bound to theirs, and the exit hold's has no widget.
//
// The lock state itself (locked_subject) is declared with the other subjects at
// the top of the file. Locked still leaves the menu reachable -- Log, UI
// Settings and the bench tools are useful with the chair not driving -- but
// nothing moves: the stick only drives from Drive (stick_drives).
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kHoldMs = 1000; // hold time to fill a gesture widget
// Dead time before a hold starts filling. The stick button doubles as select,
// and a select is a press shorter than this (kSelectMaxUs), so a tap never
// ticks a fill up and snaps it back.
static constexpr uint32_t kBarGraceMs = 500;
static constexpr int32_t kHoldMax = 100; // arc/bar range (LVGL's default)
// Beat between the unlock landing and the Drive screen dissolving in, so the
// unlocked padlock is legible rather than a flash.
static constexpr uint32_t kUnlockAdvanceMs = 1000;
// Poll cadence for the inputs. Matched to the ADC task's 33 ms period: joy_key
// cannot change faster than that, so a shorter period would only burn LVGL task
// time re-reading the same value.
static constexpr uint32_t kHoldPollMs = 33;

