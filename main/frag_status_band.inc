// split_main.py fragment of main.cpp (frag_status_band: original lines 322-479); one TU, see
// main.cpp.
#include "hmi_ui/status_band_view.hpp"
// The "FPS counter" Skunk Works slot -> LVGL's built-in perf overlay
// (lv_sysmon), which already renders FPS/CPU on the sys layer above every
// screen. Wired here rather than through a SquareLine CALL FUNCTION event so it
// needs no round trip through the design tool; either way import_ui.ps1's
// mirror never touches main.cpp.
static void fps_toggle() {
  static bool shown = false;
  shown = !shown;
  if (shown) {
    lv_sysmon_show_performance(lv_display_get_default());
  } else {
    lv_sysmon_hide_performance(lv_display_get_default());
  }
}

// Observer for the button counter's colour. There's no built-in binding for a
// style property, so the widget work happens here; the observer is bound to the
// object, so it dies with it.
static void gpio48_panel_observer(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  // "not pressed" is the *current theme's* text colour, not a hardcoded white —
  // SquareLine registered this property as themeable, so reading the theme keeps
  // the count correct after a dark/day switch.
  lv_obj_set_style_text_color(label,
                              lv_subject_get_int(subject)
                                  ? lv_palette_main(LV_PALETTE_BLUE)
                                  : lv_color_hex(ui_get_theme_value(_ui_theme_color_text)),
                              LV_PART_MAIN);
}

/////////////////////////////////////////////////////////////////////////////
// MCB status panel
//
// One view (hmi::ui::StatusBandView) serves both labels on every DriveBand
// instance. It sets the text (from the shared spec, so the MCB's logs and
// these labels use the same words) and the colour — a style property, which
// has no built-in binding, hence an observer rather than lv_label_bind_text.
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kStatusOrange = 0xFF8C00; // network up, nothing peering

void hmi::ui::StatusBandView::drive_observer_cb(lv_observer_t *observer, lv_subject_t *) {
  static_cast<const StatusBandView *>(lv_observer_get_user_data(observer))
      ->show(lv_observer_get_target_obj(observer), Kind::DRIVE_STATUS);
}

void hmi::ui::StatusBandView::state_observer_cb(lv_observer_t *observer, lv_subject_t *) {
  static_cast<const StatusBandView *>(lv_observer_get_user_data(observer))
      ->show(lv_observer_get_target_obj(observer), Kind::STATE);
}

// Re-run either when the MCB says something new or when the link or the lock
// changes: whichever subject fired, the answer depends on all of them.
void hmi::ui::StatusBandView::show(lv_obj_t *label, Kind kind) const {
  // Theme colours, not literals: the day theme's green and red are darker, so
  // they still read on white. Re-run on a theme switch (rtps_poll_cb notifies).
  const lv_color_t ok = lv_color_hex(static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_ok)));
  const lv_color_t alert =
      lv_color_hex(static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_alert)));
  const lv_color_t muted =
      lv_color_hex(static_cast<uint32_t>(ui_get_theme_value(_ui_theme_color_text_muted)));
  auto show = [label](const char *text, lv_color_t colour) {
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, colour, LV_PART_MAIN);
  };

  const bool connected =
      static_cast<LinkState>(lv_subject_get_int(config_.shared->rtps_link)) == LinkState::CONNECTED;
  const auto state =
      static_cast<MIB::MibSystemState>(lv_subject_get_int(config_.shared->mib_state));

  // DRIVE is two words, per spec: ACTIVE in green while the chair is driving,
  // LOCKED in red whenever it is not -- locked here, a fault, the link down, or
  // the MIB simply not enabled. It is the one thing the user has to act on
  // before anything moves, and the spec gives it the alert colour for that.
  // Locked outranks everything: the chair does not drive until it is asked to,
  // whatever the MIB says.
  if (kind == Kind::DRIVE_STATUS) {
    const bool active = lv_subject_get_int(config_.shared->locked) == 0 && connected &&
                        state == MIB::MibSystemState::ENABLED;
    show(active ? "ACTIVE" : "LOCKED", active ? ok : alert);
    return;
  }

  // STATE: no live link means no current answer, whatever arrived last.
  if (!connected) {
    show(UNKNOWN_TEXT, muted);
    return;
  }
  // The MIB's own wording wins when it sends one ("M2 ERROR"); the colour still
  // comes from the state, so it can say anything and still read as a fault.
  const char *override_text = lv_subject_get_string(config_.shared->state_text);
  const bool overridden = override_text != nullptr && override_text[0] != '\0';
  switch (state) {
  case MIB::MibSystemState::IDLE:
  case MIB::MibSystemState::ENABLED:
    show(overridden ? override_text : "OK", ok);
    return;
  case MIB::MibSystemState::INITIALIZING:
    // Not a fault and not yet OK. "STARTING" rather than the enum's name, which
    // does not fit the cell.
    show(overridden ? override_text : "STARTING", muted);
    return;
  default:
    // ERROR, and any value this firmware does not know -- what a MIB running
    // ahead of it sends.
    show(overridden ? override_text : "ERROR", alert);
    return;
  }
}

void hmi::ui::StatusBandView::bind(lv_obj_t *panel) {
  if (panel == nullptr) {
    return;
  }
  const SharedSubjects &s = *config_.shared;
  lv_obj_t *drive_label = ui_comp_get_child(panel, UI_COMP_DRIVEBAND_DRIVECELL_DRIVEVALUE);
  lv_obj_t *state_label = ui_comp_get_child(panel, UI_COMP_DRIVEBAND_STATECELL_STATEVALUE);
  lv_subject_add_observer_obj(s.mib_state, drive_observer_cb, drive_label, this);
  lv_subject_add_observer_obj(s.mib_state, state_observer_cb, state_label, this);
  // Further observers, so losing the link or receiving a new override repaints
  // them even though the enum said nothing new. All are object-bound and die
  // with the label.
  lv_subject_add_observer_obj(s.rtps_link, drive_observer_cb, drive_label, this);
  lv_subject_add_observer_obj(s.rtps_link, state_observer_cb, state_label, this);
  lv_subject_add_observer_obj(s.drive_text, drive_observer_cb, drive_label, this);
  lv_subject_add_observer_obj(s.state_text, state_observer_cb, state_label, this);
  // The drive cell reads LOCKED while locked, so it re-runs on that too.
  lv_subject_add_observer_obj(s.locked, drive_observer_cb, drive_label, this);
}

// The one instance, for every DriveBand; bound from app_main and the screens built on demand.
static constinit hmi::ui::StatusBandView status_band_view{{.shared = &ui_shared_subjects}};
static void bind_status_panel(lv_obj_t *panel) { status_band_view.bind(panel); }

/////////////////////////////////////////////////////////////////////////////
// Joystick mapping
//
// How far each axis travels is measured per unit (joystick_cal.cpp, the
// CALIBRATE button on the JoystickTest screen). What is done with that travel
// is fixed here.
/////////////////////////////////////////////////////////////////////////////

// X/Y are one circular gimbal, so their per-axis deadbands are 0 and the
// deadzone lives on the vector radius instead. The APEM HF44S10 has a square
// gate, but the output stays CIRCULAR: a corner reads ~(0.71, 0.71) rather
// than (1, 1), so the MCB never sees a speed above 1.
static constexpr float kStickCenterDeadzoneRadius = 0.10f;
static constexpr float kStickRangeDeadzone = 0.05f;
// Twist is a separate pot on its own axis, so it carries its own deadbands.
static constexpr float kTwistCenterDeadbandMv = 60.0f;
static constexpr float kTwistRangeDeadbandMv = 40.0f;
