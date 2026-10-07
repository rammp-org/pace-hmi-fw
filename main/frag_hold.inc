// split_main.py fragment of main.cpp (frag_hold: original lines 954-1066); one TU, see main.cpp.
// Whether each input is currently released, and so free to start a new hold.
//
// This is the "let go first" rule, and it is per *input* rather than per
// gesture on purpose: completing one gesture usually navigates straight to a
// screen where another gesture watches the same input, and the user's thumb is
// still where it was. Without this, the button hold that enters driving would
// roll on into the Drive screen's exit hold, which watches the same button.
static bool joy_button_armed = true;

// The gestures and the engine that runs them (hmi::ui::HoldGesture, HoldEngine).
using hmi::ui::HoldGesture;

// Defined with the refusal banners, further down: the push check that shadows the unlock hold.
static void entry_refusal_poll();

// The one engine. Its confirmation is the same for all three gestures. Both calls are
// non-blocking - one short I2C burst, and one xStreamBufferSend with a zero timeout - so
// neither holds the LVGL task for the duration of the effect it starts.
static constinit hmi::ui::HoldEngine hold_engine{{
    .confirm =
        [] {
          haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
          play_click(espp::M5StackTab5::get());
        },
    // The self-test overlay owns the stick while it is up (see the keypad read in app_main).
    .overlay_up = [] { return selftest_ui_visible(); },
    .before_poll = [] { entry_refusal_poll(); },
}};

// Cancel anything mid-fill rather than let it complete - and navigate - behind the
// overlay.
void hmi::ui::HoldEngine::poll_all(std::span<HoldGesture *const> gestures) const {
  if (config_.overlay_up()) {
    for (HoldGesture *g : gestures) {
      if (g->holding) {
        g->holding = false;
        reset(g);
      }
    }
    return;
  }
  config_.before_poll();
  for (HoldGesture *g : gestures) {
    poll(g);
  }
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

