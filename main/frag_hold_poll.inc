// split_main.py fragment of main.cpp (frag_hold_poll: original lines 1727-1832); one TU, see main.cpp.
// Which page of the seat screen is showing: 0 = the function buttons, 1 = the
// adjustment panel over them (spec 04b).
static int seat_page = 0;

// Defined with the rest of the seat navigation below, which is where the button
// grid it restores focus into is declared.
static void seat_show_buttons_page();

// Calibrate is press-and-HOLD, never a tap: a run takes the stick over for the
// best part of a minute, so it should not start from a brush of the screen or
// a stray click of the stick button. Holding the Calibrate button, or the stick
// button anywhere on the joystick screen, fills the meter along the bottom of
// the button (bound in app_main) and starts a run when it is full; the same
// hold during a run cancels it.
//
// The touch half is polled -- "is a pointer down, and on Calibrate?" at the
// gesture's own cadence -- rather than tracked from the button's press events,
// so there is no event to miss and nothing to keep in step. Both inputs feed
// the one gesture and share its grace and fill time.
static bool calibrate_touch_held() {
  if (ui_CalibrateButton == nullptr || lv_screen_active() != ui_JoystickScreen) {
    return false;
  }
  for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr;
       indev = lv_indev_get_next(indev)) {
    if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER ||
        lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
      continue;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (lv_indev_search_obj(lv_screen_active(), &point) == ui_CalibrateButton) {
      return true;
    }
  }
  return false;
}
static bool calibrate_held() { return joy_button_held() || calibrate_touch_held(); }

// Its own "let go first" flag, not joy_button_armed: that one is shared with
// drive_exit_gesture, whose poll sees the stick button up while a FINGER holds
// Calibrate and re-arms it every 33 ms -- so a completed touch hold was armed
// again at once, and one long press started a run and then cancelled it.
static bool calibrate_armed = true;

static HoldGesture calibrate_gesture{
    .armed = &calibrate_armed,
    .is_held = calibrate_held,
    .applies = [] { return lv_screen_active() == ui_JoystickScreen && nav_menu_open == nullptr; },
    .completed = [] { joystick_cal_toggle(); },
    // The button doubles as select, so a tap must not tick the fill.
    .grace_ms = kBarGraceMs,
};

// Three gestures. Everything else that used to be one -- enter the drive, seat
// and actions screens, leave the seat, bench, settings, actions, diagnostics
// and log screens -- is a menu row or the DRIVE cell now.
static HoldGesture *const kHoldGestures[] = {
    &unlock_gesture,
    &drive_exit_gesture,
    &calibrate_gesture,
};

static void hold_poll_cb(lv_timer_t *) {
  // The self-test overlay owns the stick while it is up (see the keypad read in
  // app_main). Cancel anything mid-fill rather than let it complete - and
  // navigate - behind the overlay.
  if (selftest_ui_visible()) {
    for (HoldGesture *g : kHoldGestures) {
      if (g->holding) {
        g->holding = false;
        hold_reset(g);
      }
    }
    return;
  }
  entry_refusal_poll();
  for (HoldGesture *g : kHoldGestures) {
    hold_poll(g);
  }
}

/////////////////////////////////////////////////////////////////////////////
// SeatScreen: joystick navigation of both pages
//
// The joystick walks each page as the grid the user sees rather than as the
// flat list LVGL's own focus_next would give. Each page has its own group and
// its own cursor:
//
//   function buttons                adjustment page (spec 04b)
//   [ FB Tilt   ] [ Side Tilt   ]   [ < ]
//   [ Elevation ] [ Translation ]   [     -     ] [     +     ]
//   [ Static    ] [ Dynamic     ]   [ 0deg ] [ 15deg ] [ 25deg ]
//
// The rows are different lengths, which is why a grid carries a per-row count
// rather than one column total. The ButtonGrid also serves the bench gate's
// PIN pad.
//
// Picking one of the four function buttons names its motion on the adjustment
// page and shows it over the buttons; "<", or left from the first column,
// hides it again. The page's buttons step the motion or send it to a preset.
/////////////////////////////////////////////////////////////////////////////

static constexpr int kGridMaxRows = 4; // the PIN pad's bottom row is the fourth
static constexpr int kGridMaxCols = 3;

