// split_main.py fragment of main.cpp (frag_display_flip: original lines 4237-4418); one TU, see main.cpp.
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
static espp::Logger logger_nav({.tag = "nav", .level = espp::Logger::Verbosity::INFO});
static espp::Logger logger_flip({.tag = "flip", .level = espp::Logger::Verbosity::INFO});

namespace hmi::ui {
namespace {

/// @brief DIRECT rendering into the DSI panel's frame buffers, the 180-degree screen flip,
/// and the touch input that turns with it.
///
/// main owns the one instance (constinit, no global constructor: G4). LVGL finds it through
/// the display's and the touch input's driver data, which nothing else here uses (espp's
/// Display and TouchpadInput keep theirs in user data).
class DisplayFlip {
public:
  /// @brief What the flip needs from main.
  struct Config {
    espp::Logger *log;                     ///< The flip's log (tag "flip").
    void (*present)(const uint8_t *frame); ///< The board's vsync-gated swap to `frame`.
    void (*refuse)();                      ///< A press on a greyed control: the refusal.
  };

  constexpr explicit DisplayFlip(const Config &config) noexcept
      : config_(config) {}

  /// @brief Renders `disp` in DIRECT mode straight into the panel's two frame buffers.
  /// @param disp The display.
  /// @param fb0 The panel's first frame buffer.
  /// @param fb1 The panel's second frame buffer.
  /// @param bytes The size of one frame buffer.
  /// @param w The panel's width, in pixels.
  /// @param h The panel's height, in pixels.
  /// app_main, before lv_task starts.
  void use_panel_buffers(lv_display_t *disp, void *fb0, void *fb1, size_t bytes, int32_t w,
                         int32_t h);

  /// @brief Turns the picture over, or back.
  /// @param on true for 180 degrees, false for upright.
  /// LVGL task, between frames (a settings step or the boot-time apply), so no flush is in flight.
  void set_flipped(bool on);

  /// @brief Once the touch input exists: routes its reads through the flip.
  /// @param touch The touch input; null does nothing.
  /// app_main, with lvgl_mutex held.
  void wrap_touch(lv_indev_t *touch);

private:
  static void direct_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
  static void flip_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
  static void touch_read_flip_aware(lv_indev_t *indev, lv_indev_data_t *data);
  void touch_refuse_disabled(const lv_indev_data_t *data);

  Config config_;
  uint8_t *panel_fb_[2] = {nullptr, nullptr}; // the DSI panel's frame buffers
  size_t panel_fb_bytes_ = 0;
  int32_t panel_w_ = 0;
  int32_t panel_h_ = 0;
  uint8_t *flip_frame_ = nullptr; // LVGL's upright frame while flipped (PSRAM)
  ppa_client_handle_t flip_ppa_ = nullptr;
  int flip_back_ = 0; // which panel_fb_ the next rotated frame goes into
  std::atomic<bool> display_flipped_{false};
  // The touch input's own read, kept so the wrapper can call it.
  lv_indev_read_cb_t touch_read_upright_ = nullptr;
  bool was_down_ = false; // touch_refuse_disabled: the last read's press state
};

void DisplayFlip::use_panel_buffers(lv_display_t *disp, void *fb0, void *fb1, size_t bytes,
                                    int32_t w, int32_t h) {
  lv_display_set_driver_data(disp, this);
  lv_display_set_buffers(disp, fb0, fb1, bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, direct_flush_cb);
  panel_fb_[0] = static_cast<uint8_t *>(fb0);
  panel_fb_[1] = static_cast<uint8_t *>(fb1);
  panel_fb_bytes_ = bytes;
  panel_w_ = w;
  panel_h_ = h;
}

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
void DisplayFlip::direct_flush_cb(lv_display_t *disp, const lv_area_t * /*area*/,
                                  uint8_t *px_map) {
  if (!lv_display_flush_is_last(disp)) {
    lv_display_flush_ready(disp);
    return;
  }
  const DisplayFlip &self = *static_cast<const DisplayFlip *>(lv_display_get_driver_data(disp));
  // flush_ready itself arrives via the panel's on_color_trans_done; this call
  // additionally waits for the swap before letting LVGL render again.
  self.config_.present(px_map);
}

void DisplayFlip::flip_flush_cb(lv_display_t *disp, const lv_area_t *, uint8_t *px_map) {
  if (!lv_display_flush_is_last(disp)) {
    lv_display_flush_ready(disp);
    return;
  }
  DisplayFlip &self = *static_cast<DisplayFlip *>(lv_display_get_driver_data(disp));
  uint8_t *target = self.panel_fb_[self.flip_back_];
  const auto w = static_cast<uint32_t>(self.panel_w_);
  const auto h = static_cast<uint32_t>(self.panel_h_);
  bool rotated = false;
  if (self.flip_ppa_ != nullptr) {
    ppa_srm_oper_config_t srm = {};
    srm.in.buffer = px_map;
    srm.in.pic_w = w;
    srm.in.pic_h = h;
    srm.in.block_w = w;
    srm.in.block_h = h;
    srm.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    srm.out.buffer = target;
    srm.out.buffer_size = self.panel_fb_bytes_;
    srm.out.pic_w = w;
    srm.out.pic_h = h;
    srm.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_180;
    srm.scale_x = 1.0f;
    srm.scale_y = 1.0f;
    srm.mode = PPA_TRANS_MODE_BLOCKING;
    rotated = ppa_do_scale_rotate_mirror(self.flip_ppa_, &srm) == ESP_OK;
  }
  if (!rotated) {
    // No PPA (or it refused): the same turn on the CPU, then out of the cache
    // so the DSI DMA reads what was written.
    const auto stride =
        static_cast<int32_t>(lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565));
    lv_draw_sw_rotate(px_map, target, self.panel_w_, self.panel_h_, stride, stride,
                      LV_DISPLAY_ROTATION_180, LV_COLOR_FORMAT_RGB565);
    esp_cache_msync(target, self.panel_fb_bytes_, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
  // The same vsync-gated swap as direct_flush_cb; flush_ready arrives through
  // the panel's on_color_trans_done.
  self.config_.present(target);
  self.flip_back_ ^= 1;
}

void DisplayFlip::set_flipped(bool on) {
  if (on == display_flipped_.load() || panel_fb_[0] == nullptr) {
    return; // nothing to change, or DIRECT mode never came up
  }
  lv_display_t *disp = lv_display_get_default();
  // In DIRECT double buffering LVGL's active buffer is the one it draws into
  // next -- the one NOT on screen. That is where the first frame of the new
  // mode has to go.
  const lv_draw_buf_t *active = lv_display_get_buf_active(disp);
  const int back = active != nullptr && active->data == panel_fb_[1] ? 1 : 0;
  if (on) {
    if (flip_frame_ == nullptr) {
      flip_frame_ =
          static_cast<uint8_t *>(heap_caps_aligned_alloc(128, panel_fb_bytes_, MALLOC_CAP_SPIRAM));
      if (flip_frame_ == nullptr) {
        config_.log->error("no PSRAM for the flipped frame; staying upright");
        return;
      }
    }
    if (flip_ppa_ == nullptr) {
      ppa_client_config_t cfg = {};
      cfg.oper_type = PPA_OPERATION_SRM;
      if (ppa_register_client(&cfg, &flip_ppa_) != ESP_OK) {
        flip_ppa_ = nullptr; // flip_flush_cb falls back to the CPU
        config_.log->warn("no PPA client; the flip will rotate on the CPU");
      }
    }
    flip_back_ = back;
    lv_display_set_buffers(disp, flip_frame_, nullptr, panel_fb_bytes_,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flip_flush_cb);
  } else {
    // Back to rendering straight into the panel: the one not on screen first.
    // flip_back_ is the next free one, so the other is showing.
    lv_display_set_buffers(disp, panel_fb_[flip_back_], panel_fb_[flip_back_ ^ 1],
                           panel_fb_bytes_, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, direct_flush_cb);
  }
  display_flipped_.store(on);
  // Everything, into the new buffers. Twice when turning back: DIRECT mode
  // copies the last frame's changed areas forward from the other panel buffer,
  // which still holds a rotated picture until LVGL has drawn into both once.
  lv_obj_invalidate(lv_screen_active());
  if (!on) {
    lv_timer_t *again =
        lv_timer_create([](lv_timer_t *) { lv_obj_invalidate(lv_screen_active()); }, 100, nullptr);
    lv_timer_set_repeat_count(again, 1);
  }
  config_.log->info("screen {}", on ? "flipped 180 degrees" : "upright");
}

// A finger landing on a greyed control (a stepper at its limit). LVGL finds
// the widget under the point but sends a DISABLED one no events at all, so
// this is the only place that hears the press -- and answers it with the
// refusal, like every other greyed control.
void DisplayFlip::touch_refuse_disabled(const lv_indev_data_t *data) {
  const bool down = data->state == LV_INDEV_STATE_PRESSED;
  if (down && !was_down_) {
    lv_point_t point = data->point;
    lv_obj_t *hit = lv_indev_search_obj(lv_screen_active(), &point);
    if (hit != nullptr && lv_obj_has_state(hit, LV_STATE_DISABLED)) {
      config_.refuse();
    }
  }
  was_down_ = down;
}

void DisplayFlip::touch_read_flip_aware(lv_indev_t *indev, lv_indev_data_t *data) {
  DisplayFlip &self = *static_cast<DisplayFlip *>(lv_indev_get_driver_data(indev));
  self.touch_read_upright_(indev, data);
  if (self.display_flipped_.load()) {
    data->point.x = self.panel_w_ - 1 - data->point.x;
    data->point.y = self.panel_h_ - 1 - data->point.y;
  }
  self.touch_refuse_disabled(data);
}

void DisplayFlip::wrap_touch(lv_indev_t *touch) {
  if (touch == nullptr || touch_read_upright_ != nullptr) {
    return;
  }
  touch_read_upright_ = lv_indev_get_read_cb(touch);
  if (touch_read_upright_ != nullptr) {
    lv_indev_set_driver_data(touch, this);
    lv_indev_set_read_cb(touch, touch_read_flip_aware);
  }
}

} // namespace
} // namespace hmi::ui

// The board's swap, for the flush: present_frame waits for vsync. Its result
// was never used; a missed swap shows as one late frame.
static void present_on_panel(const uint8_t *frame) {
  (void)espp::M5StackTab5::get().present_frame(frame);
}

// The one DisplayFlip. app_main hands it the panel buffers and the touch input.
static constinit hmi::ui::DisplayFlip display_flip{
    {.log = &logger_flip, .present = present_on_panel, .refuse = refusal_feedback}};

// Settings "Flip screen" (frag_settings_ui's observer, and the boot-time apply).
static void set_display_flipped(bool on) { display_flip.set_flipped(on); }
