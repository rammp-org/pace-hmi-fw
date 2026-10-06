// split_main.py fragment of main.cpp (frag_overdraw: original lines 4000-4105); one TU, see main.cpp.
// Direct-mode flush. LVGL renders into the DPI panel's own frame buffers, so
// px_map is already a frame buffer and esp_lcd_panel_draw_bitmap takes its
// no-copy branch: it writes the cache back, sets cur_fb_index, and returns,
// instead of copying 1.8 MB.
//
// That branch is not self-synchronising: it fires on_color_trans_done
// SYNCHRONOUSLY (esp_lcd_panel_dpi.c), which the BSP wires to
// lv_display_flush_ready, so disp->flushing clears before this function even
// returns. The actual buffer swap only lands when the DSI DMA re-arms at the
// end of the current frame. present_frame() blocks until it does. Blocking here
// is what gates the renderer: LVGL is single-threaded and cannot start the next
// refresh while flush_cb is on the stack, so by the time this returns the DMA
// is scanning the frame we just queued and the other buffer is free to draw
// into. Costs up to one panel refresh period (~17 ms) per frame, during which
// the LVGL task holds lvgl_mutex.
//
// Ignoring `area` and flushing full-screen IS correct here: call_flush_cb passes
// the buffer START (not an offset), and refr_sync_areas already copies the
// previous frame's invalid areas forward, so both buffers stay coherent.
// ---------------------------------------------------------------------------
// Overdraw
//
// What a full-screen redraw costs is mostly content, not the frame buffer: the
// render benchmark (kFpsInstrument) measured 86 ms for a busy screen against
// 19.5 ms for an empty one. The biggest share of that content was fills
// nobody could see, which is what strip_screen_overdraw takes out.
// ---------------------------------------------------------------------------
static espp::Logger logger_overdraw({.tag = "overdraw", .level = espp::Logger::Verbosity::INFO});

// Overdraw: SquareLine gives every container an opaque background, so a page
// nested three deep repaints the same theme colour three times before anything
// visible lands on top. Measured on the old home pager with a full-screen
// redraw, those redundant fills were ~35 ms of an 86 ms frame -- three nested
// panels each filling the exact colour the screen had already painted.
//
// A fill is redundant when the object is a plain opaque rectangle in exactly
// the colour already on the screen behind it: same colour, no corner radius, no
// gradient and no background image. Clearing bg_opa then changes nothing that
// can be seen and removes the fill. Anything else is left alone.
//
// `behind` is the colour actually painted behind `obj`, threaded down the
// recursion rather than read back off the parent -- a parent whose own fill was
// just cleared still has its colour in its style, and reading that would stop
// the cascade after one level.
static bool color_eq(lv_color_t a, lv_color_t b) {
  return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

static uint32_t strip_redundant_backgrounds(lv_obj_t *obj, lv_color_t behind) {
  uint32_t stripped = 0;
  lv_color_t painted = behind;
  // An overlay keeps its fill, subtree and all. The test below asks whether an
  // object paints what its ANCESTORS already painted, which is true of a
  // nested container and false of anything that covers a SIBLING: spec V2's
  // fault banner and burger menu are exactly the theme background, so their
  // fill reads as redundant -- and taking it away leaves them drawn on top of
  // the content they exist to hide, with the speed digits showing through the
  // fault text. Marked by kOverlayFlag where they are wired.
  if (lv_obj_has_flag(obj, kOverlayFlag)) {
    return 0;
  }
  if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) == LV_OPA_COVER) {
    const lv_color_t own = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
    if (color_eq(own, behind) && lv_obj_get_style_radius(obj, LV_PART_MAIN) == 0 &&
        lv_obj_get_style_bg_grad_dir(obj, LV_PART_MAIN) == LV_GRAD_DIR_NONE &&
        lv_obj_get_style_bg_image_src(obj, LV_PART_MAIN) == nullptr) {
      lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
      stripped = 1;
    } else {
      painted = own;
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    stripped += strip_redundant_backgrounds(lv_obj_get_child(obj, i), painted);
  }
  return stripped;
}

// The screen itself always keeps its fill: it is what the redundant children
// were duplicating, and something has to paint the background.
static uint32_t strip_screen_overdraw(const lv_obj_t *screen) {
  const lv_color_t base = lv_obj_get_style_bg_color(screen, LV_PART_MAIN);
  uint32_t stripped = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++) {
    stripped += strip_redundant_backgrounds(lv_obj_get_child(screen, i), base);
  }
  return stripped;
}

// Every screen ui_init built. Kept in one place so the boot pass and the
// theme-change pass cannot drift apart. The screens built on demand are
// nullptr while they do not exist, and skipped.
static void strip_all_overdraw() {
  const lv_obj_t *const screens[] = {ui_LockedScreen,    ui_DriveScreen,       ui_SeatScreen,
                                     ui_BenchGateScreen, ui_SettingsScreen,    ui_JoystickScreen,
                                     ui_LogScreen,       ui_SkunkWorksScreen,  ui_DiagnosticsScreen,
                                     ui_UpdateScreen,    ui_BenchMotorsScreen, ui_InternetScreen,
                                     ui_AboutScreen};
  const uint32_t stripped =
      std::accumulate(std::begin(screens), std::end(screens), uint32_t{0},
                      [](uint32_t sum, const lv_obj_t *screen) {
                        return screen != nullptr ? sum + strip_screen_overdraw(screen) : sum;
                      });
  logger_overdraw.info("cleared {} redundant background fills", stripped);
}

