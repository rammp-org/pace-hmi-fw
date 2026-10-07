// split_main.py fragment of main.cpp (frag_display_flip: original lines 4237-4418); one TU, see main.cpp.
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
static void direct_flush_cb(lv_display_t *disp, const lv_area_t * /*area*/, uint8_t *px_map) {
  if (!lv_display_flush_is_last(disp)) {
    lv_display_flush_ready(disp);
    return;
  }
  auto &tab5 = espp::M5StackTab5::get();
  // flush_ready itself arrives via the panel's on_color_trans_done; this call
  // additionally waits for the swap before letting LVGL render again.
  tab5.present_frame(px_map);
}

/////////////////////////////////////////////////////////////////////////////
// Screen flip (Settings "Flip screen")
//
// Turns the picture 180 degrees for a unit mounted upside down. LVGL keeps
// drawing upright; the P4's pixel-processing accelerator (PPA) turns the
// finished frame over on its way to the panel.
//
// Normally LVGL renders straight into the DSI panel's two frame buffers and a
// flush is just a swap (direct_flush_cb). Flipped, LVGL renders -- still in
// DIRECT mode, so it keeps redrawing only what changed -- into one PSRAM frame
// of its own, and each flush has the PPA rotate the whole frame into the panel
// buffer that is not on screen, then swaps to it at vsync exactly as the
// normal path does. So it stays tear-free, the frame buffer LVGL owns stays
// upright (screenshots, the remote UI), and flip off is the same zero-copy
// pipeline as before.
//
// Everything here is public API -- LVGL's buffer and read-callback calls,
// ESP-IDF's PPA driver and DPI frame buffers, the board's present_frame() --
// so neither LVGL nor the vendored board support is changed. The PPA driver
// writes the source back from the CPU cache and invalidates the target itself.
// Not the panel's own scan direction (MADCTL): on this DSI video-mode panel
// that garbles the picture.
//
// Touch turns with the picture: the touch input's read callback is wrapped to
// mirror the point while flipped.
/////////////////////////////////////////////////////////////////////////////

static uint8_t *panel_fb[2] = {nullptr, nullptr}; // the DSI panel's frame buffers
static size_t panel_fb_bytes = 0;
static int32_t panel_w = 0;
static int32_t panel_h = 0;
static uint8_t *flip_frame = nullptr; // LVGL's upright frame while flipped (PSRAM)
static ppa_client_handle_t flip_ppa = nullptr;
static int flip_back = 0; // which panel_fb the next rotated frame goes into
static std::atomic<bool> display_flipped{false};
static espp::Logger logger_nav({.tag = "nav", .level = espp::Logger::Verbosity::INFO});
static espp::Logger logger_flip({.tag = "flip", .level = espp::Logger::Verbosity::INFO});

static void flip_flush_cb(lv_display_t *disp, const lv_area_t *, uint8_t *px_map) {
  if (!lv_display_flush_is_last(disp)) {
    lv_display_flush_ready(disp);
    return;
  }
  uint8_t *target = panel_fb[flip_back];
  bool rotated = false;
  if (flip_ppa != nullptr) {
    ppa_srm_oper_config_t srm = {};
    srm.in.buffer = px_map;
    srm.in.pic_w = panel_w;
    srm.in.pic_h = panel_h;
    srm.in.block_w = panel_w;
    srm.in.block_h = panel_h;
    srm.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    srm.out.buffer = target;
    srm.out.buffer_size = panel_fb_bytes;
    srm.out.pic_w = panel_w;
    srm.out.pic_h = panel_h;
    srm.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_180;
    srm.scale_x = 1.0f;
    srm.scale_y = 1.0f;
    srm.mode = PPA_TRANS_MODE_BLOCKING;
    rotated = ppa_do_scale_rotate_mirror(flip_ppa, &srm) == ESP_OK;
  }
  if (!rotated) {
    // No PPA (or it refused): the same turn on the CPU, then out of the cache
    // so the DSI DMA reads what was written.
    const uint32_t stride = lv_draw_buf_width_to_stride(panel_w, LV_COLOR_FORMAT_RGB565);
    lv_draw_sw_rotate(px_map, target, panel_w, panel_h, stride, stride, LV_DISPLAY_ROTATION_180,
                      LV_COLOR_FORMAT_RGB565);
    esp_cache_msync(target, panel_fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
  // The same vsync-gated swap as direct_flush_cb; flush_ready arrives through
  // the panel's on_color_trans_done.
  espp::M5StackTab5::get().present_frame(target);
  flip_back ^= 1;
}

// Turns the picture over, or back. LVGL task, between frames (a settings step
// or the boot-time apply), so no flush is in flight.
static void set_display_flipped(bool on) {
  if (on == display_flipped.load() || panel_fb[0] == nullptr) {
    return; // nothing to change, or DIRECT mode never came up
  }
  lv_display_t *disp = lv_display_get_default();
  // In DIRECT double buffering LVGL's active buffer is the one it draws into
  // next -- the one NOT on screen. That is where the first frame of the new
  // mode has to go.
  const lv_draw_buf_t *active = lv_display_get_buf_active(disp);
  const int back = active != nullptr && active->data == panel_fb[1] ? 1 : 0;
  if (on) {
    if (flip_frame == nullptr) {
      flip_frame =
          static_cast<uint8_t *>(heap_caps_aligned_alloc(128, panel_fb_bytes, MALLOC_CAP_SPIRAM));
      if (flip_frame == nullptr) {
        logger_flip.error("no PSRAM for the flipped frame; staying upright");
        return;
      }
    }
    if (flip_ppa == nullptr) {
      ppa_client_config_t cfg = {};
      cfg.oper_type = PPA_OPERATION_SRM;
      if (ppa_register_client(&cfg, &flip_ppa) != ESP_OK) {
        flip_ppa = nullptr; // flip_flush_cb falls back to the CPU
        logger_flip.warn("no PPA client; the flip will rotate on the CPU");
      }
    }
    flip_back = back;
    lv_display_set_buffers(disp, flip_frame, nullptr, panel_fb_bytes,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flip_flush_cb);
  } else {
    // Back to rendering straight into the panel: the one not on screen first.
    // flip_back is the next free one, so the other is showing.
    lv_display_set_buffers(disp, panel_fb[flip_back], panel_fb[flip_back ^ 1], panel_fb_bytes,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, direct_flush_cb);
  }
  display_flipped.store(on);
  // Everything, into the new buffers. Twice when turning back: DIRECT mode
  // copies the last frame's changed areas forward from the other panel buffer,
  // which still holds a rotated picture until LVGL has drawn into both once.
  lv_obj_invalidate(lv_screen_active());
  if (!on) {
    lv_timer_t *again =
        lv_timer_create([](lv_timer_t *) { lv_obj_invalidate(lv_screen_active()); }, 100, nullptr);
    lv_timer_set_repeat_count(again, 1);
  }
  logger_flip.info("screen {}", on ? "flipped 180 degrees" : "upright");
}

// The touch input's own read, kept so the wrapper can call it.
static lv_indev_read_cb_t touch_read_upright = nullptr;

// A finger landing on a greyed control (a stepper at its limit). LVGL finds
// the widget under the point but sends a DISABLED one no events at all, so
// this is the only place that hears the press -- and answers it with the
// refusal, like every other greyed control.
static void touch_refuse_disabled(const lv_indev_data_t *data) {
  static bool was_down = false;
  const bool down = data->state == LV_INDEV_STATE_PRESSED;
  if (down && !was_down) {
    lv_point_t point = data->point;
    lv_obj_t *hit = lv_indev_search_obj(lv_screen_active(), &point);
    if (hit != nullptr && lv_obj_has_state(hit, LV_STATE_DISABLED)) {
      refusal_feedback();
    }
  }
  was_down = down;
}

static void touch_read_flip_aware(lv_indev_t *indev, lv_indev_data_t *data) {
  touch_read_upright(indev, data);
  if (display_flipped.load()) {
    data->point.x = panel_w - 1 - data->point.x;
    data->point.y = panel_h - 1 - data->point.y;
  }
  touch_refuse_disabled(data);
}

// Once the touch input exists: route its reads through the flip.
static void flip_wrap_touch(lv_indev_t *touch) {
  if (touch == nullptr || touch_read_upright != nullptr) {
    return;
  }
  touch_read_upright = lv_indev_get_read_cb(touch);
  if (touch_read_upright != nullptr) {
    lv_indev_set_read_cb(touch, touch_read_flip_aware);
  }
}

