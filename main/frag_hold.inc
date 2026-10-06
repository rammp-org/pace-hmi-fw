// split_main.py fragment of main.cpp (frag_hold: original lines 954-1066); one TU, see main.cpp.
// Whether each input is currently released, and so free to start a new hold.
//
// This is the "let go first" rule, and it is per *input* rather than per
// gesture on purpose: completing one gesture usually navigates straight to a
// screen where another gesture watches the same input, and the user's thumb is
// still where it was. Without this, the button hold that enters driving would
// roll on into the Drive screen's exit hold, which watches the same button.
static bool joy_button_armed = true;

// progress and holding carry default member initializers rather than being
// spelled out at each definition below: the three gestures differ only in the
// four fields that follow, and -Wmissing-field-initializers is happy with a
// defaulted member but not with an omitted one.
struct HoldGesture {
  lv_subject_t progress{}; // 0..kHoldMax; the arc/bar is bound to this
  bool *armed;             // the input's "released since the last completion"
  bool (*is_held)();       // is the input held right now (sampled on LVGL task)
  bool (*applies)();       // is this gesture live on the current screen/page
  void (*completed)();     // what a full hold does
  uint32_t grace_ms = 0;   // dead time before the widget starts filling
  bool holding = false;    // edge detector for is_held && applies
};

static void hold_anim_exec_cb(void *var, int32_t value) {
  lv_subject_set_int(&static_cast<HoldGesture *>(var)->progress, value);
}

// Cancels any in-flight fill and empties the widget. The gesture doubles as the
// animation's `var`, so it is also the handle lv_anim_delete matches on.
static void hold_reset(HoldGesture *g) {
  lv_anim_delete(g, hold_anim_exec_cb);
  lv_subject_set_int(&g->progress, 0);
}

static void hold_anim_completed_cb(lv_anim_t *a) {
  auto *g = static_cast<HoldGesture *>(lv_anim_get_user_data(a));
  // Disarm before running the action: `completed` navigates, and the input is
  // still held at this instant, so whatever gesture watches that input on the
  // far side must not read the same unbroken hold as a fresh one.
  *g->armed = false;
  g->holding = false;
  // Deleting the animation from inside its own completed_cb (via hold_reset) is
  // safe and expected: lv_anim.c removes it from the list before calling us,
  // and anim_timer explicitly re-reads the list afterwards.
  hold_reset(g);
  // Confirmation feedback, the same for all three. Both calls are non-blocking
  // - one short I2C burst, and one xStreamBufferSend with a zero timeout - so
  // neither holds the LVGL task for the duration of the effect it starts.
  haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
  play_click(espp::M5StackTab5::get());
  g->completed();
}

// Runs on the LVGL task rather than the ADC/button tasks that produce the
// inputs: everything here is LVGL state, and this way each animation is created
// and destroyed on the task that runs it.
static void hold_poll(HoldGesture *g) {
  const bool held = g->is_held();
  if (!held) {
    *g->armed = true;
  }
  const bool hold = held && *g->armed && g->applies();
  if (hold == g->holding) {
    return;
  }
  g->holding = hold;
  if (!hold) {
    hold_reset(g);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g);
  lv_anim_set_user_data(&a, g);
  lv_anim_set_exec_cb(&a, hold_anim_exec_cb);
  lv_anim_set_values(&a, 0, kHoldMax);
  lv_anim_set_duration(&a, kHoldMs);
  // The delay holds act_time negative, so the exec callback does not run and the
  // widget stays empty until it elapses. Completion lands at grace + duration,
  // which is what a gesture with a grace period costs in total.
  lv_anim_set_delay(&a, g->grace_ms);
  lv_anim_set_completed_cb(&a, hold_anim_completed_cb);
  lv_anim_start(&a);
}

// The stick's own button. Reads the level the button callback mirrors out, not
// the edge-latched select_key: a hold gesture needs to know the button is still
// down, and select_key is consumed by the first indev read after the press.
static bool joy_button_held() { return joy_button_pressed.load(); }

/////////////////////////////////////////////////////////////////////////////
// Lock / unlock, and the LockedScreen gesture
/////////////////////////////////////////////////////////////////////////////

// Locked means "not driving", and the Locked screen is where that changes.
// Holding the stick button -- the legend under the padlock says so; the spec's
// ACTIVATE DRIVE button is gone -- asks the MIB to start driving, and nothing
// unlocks until the MIB says it has (activate_drive, lock_open). The ring round
// the padlock shows where that stands: it fills while the button is held, a
// quarter of it goes round while the MIB is asked, and it closes when the MIB
// answers. Then the spec's 01b frame -- the shackle rises, the band flips to
// ACTIVE -- held one second, and Drive dissolves in (Motion timing,
// "ACTIVATE DRIVE").
//
// Locking again is the MIB's call too: the chair stops, asked (the drive-exit
// hold) or not (a fault, the link), and drive_screen_follow_state brings the
// Locked screen back with the reason on its banner.

static constexpr int32_t kShackleRisePx = 30; // spec 01b: 103 px -> 133 px, bottom fixed
static constexpr uint32_t kRingSpinMs = 900;  // one turn of the waiting arc
static constexpr int32_t kRingWaitArc = 25;   // how much of the ring goes round
static constexpr uint32_t kUnlockDissolveMs = 280;

