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
void hmi::ui::JoystickView::pressed_observer(lv_observer_t *observer, lv_subject_t *subject) {
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

// Bind the Joystick screen's axis bars to the ADC subjects (observer pattern).
// Bars show the calibrated joystick position as a percentage: -100..+100,
// centered at 0; RTPS carries the same values as -1..+1 (see the ADC task).
void hmi::ui::JoystickView::init_bars() {
  lv_subject_init_int(&x_, 0);
  lv_subject_init_int(&y_, 0);
  lv_subject_init_int(&twist_, 0);
  lv_bar_set_range(ui_XAxisBar, -100, 100);
  lv_bar_set_range(ui_YAxisBar, -100, 100);
  lv_bar_set_range(ui_TwistBar, -100, 100);
  lv_bar_bind_value(ui_XAxisBar, &x_);
  lv_bar_bind_value(ui_YAxisBar, &y_);
  lv_bar_bind_value(ui_TwistBar, &twist_);
}

// GPIO48 test button. The count has a built-in binding; the tint the press
// used to show went with the panel behind it, so the observer paints the
// count's own text colour instead (torn down with the object).
void hmi::ui::JoystickView::init_button() {
  lv_subject_init_int(&count_, 0);
  lv_subject_init_int(&pressed_, 0);
  lv_label_bind_text(ui_ButtonCounter, &count_, "%d");
  lv_subject_add_observer_obj(&pressed_, pressed_observer, ui_ButtonCounter, nullptr);
}

/////////////////////////////////////////////////////////////////////////////
// MCB status panel
//
// One view (hmi::ui::StatusBandView) serves both labels on every DriveBand
// instance. It sets the text (from the shared spec, so the MCB's logs and
// these labels use the same words) and the colour — a style property, which
// has no built-in binding, hence an observer rather than lv_label_bind_text.
/////////////////////////////////////////////////////////////////////////////

// The one instance, for every DriveBand; bound from app_main and the screens built on demand.
static constinit hmi::ui::StatusBandView status_band_view{{.shared = &ui_shared_subjects}};
static_assert(hmi::ui::StatusBandView::TEXT_SIZE == rammp::kMcbTextLen,
              "the band's text buffers are the shared spec's");
static void bind_status_panel(lv_obj_t *panel) { status_band_view.bind(panel); }

