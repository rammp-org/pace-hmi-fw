#pragma once
// DIRECT rendering into the DSI panel, the 180-degree screen flip (Settings "Flip screen")
// and the touch input that turns with it. Moved from main.cpp's frag_display_flip.inc;
// src/display_flip.cpp has the why.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/ppa.h"
#include "logger.hpp"
#include "lvgl.h"

namespace hmi::ui {

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

} // namespace hmi::ui
