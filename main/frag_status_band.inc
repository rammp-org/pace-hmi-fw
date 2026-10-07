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

