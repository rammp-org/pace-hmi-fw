// split_main.py fragment of main.cpp (frag_overdraw: original lines 4000-4105); one TU, see main.cpp.
#include "hmi_ui/overdraw.hpp"
// ---------------------------------------------------------------------------
// Overdraw
//
// What a full-screen redraw costs is mostly content, not the frame buffer: the
// render benchmark (kFpsInstrument) measured 86 ms for a busy screen against
// 19.5 ms for an empty one. The biggest share of that content was fills
// nobody could see, which is what strip_screen_overdraw takes out.
// ---------------------------------------------------------------------------
static espp::Logger logger_overdraw({.tag = "overdraw", .level = espp::Logger::Verbosity::INFO});

// The unit's names for the pass (screens_on_demand, rtps_poll's theme switch,
// app_main's boot pass), with the overlay flag frag_state marks overlays with.
static uint32_t strip_screen_overdraw(const lv_obj_t *screen) {
  return hmi::ui::strip_screen_overdraw(screen, kOverlayFlag);
}

static void strip_all_overdraw() {
  logger_overdraw.info("cleared {} redundant background fills",
                       hmi::ui::strip_all_overdraw(kOverlayFlag));
}

