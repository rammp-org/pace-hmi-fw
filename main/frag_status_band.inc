// split_main.py fragment of main.cpp (frag_status_band: original lines 322-479); one TU, see main.cpp.
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

