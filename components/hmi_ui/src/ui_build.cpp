#include "hmi_ui/ui_build.hpp"

#include <cstdint>

#include "ui.h"

namespace hmi::ui {

void build_screens(const lv_image_dsc_t *boot_logo, bool benchmark_drive_screen) {
  // The Tab5 panel is natively 720x1280 portrait, which is what the UI is drawn
  // for. DIRECT rendering needs rotation 0; Flip screen turns the picture in
  // the flush instead (set_display_flipped).
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_0);
  ui_init();
  // ui_init builds every screen, and this one is dead: the actuators are a
  // page of the SettingsScreen now (DEBUG ACTUATORS). Its widget tree sits in
  // internal RAM, and the W5500's SPI bounce buffer is allocated from the same
  // DMA-capable pool when RTPS starts - with the Skunk Works screen added
  // that pool ran dry and the board boot-looped (spi_master does not check the
  // allocation). Delete the screen in SquareLine and this line goes too: the
  // build fails on it, which is the reminder.
  ui_BenchMotorsScreen_screen_destroy();
  // Built on demand instead: see "Screens built on demand".
  ui_SettingsScreen_screen_destroy();
  ui_SkunkWorksScreen_screen_destroy();
  ui_DiagnosticsScreen_screen_destroy();

  // Swap the boot logo from the export's embedded SVG to a pre-rasterised A8
  // mask (main/boot_logo.c). Done here rather than in the SquareLine project
  // because import_ui.ps1 mirrors components/ui/ with robocopy /MIR and would put the
  // SVG straight back on the next import.
  //
  // This is what lets LV_USE_SVG, LV_USE_THORVG and LV_USE_VECTOR_GRAPHIC all
  // stay off: this wordmark was the only vector asset in the project, and the
  // entire ThorVG engine was compiled in to draw it once at boot.
  //
  // The scale is deliberately left alone. The export draws this at
  // lv_image_set_scale(300) and LVGL scales about the image's centre pivot, so
  // boot_logo.c is rasterised at the size the SVG DECLARED (457x196) rather
  // than its on-screen size -- feed it a pre-scaled source and the logo lands
  // about 40 px from where it sits today.
  //
  // A8 carries no colour of its own, so the wordmark's white has to come from
  // image_recolor. That is the same convention the export already uses for the
  // other single-ink assets (the padlock, the arrows); it just never set one
  // here, because a vector image brought its own fill.
  lv_image_set_src(ui_Image3, boot_logo);
  lv_obj_set_style_image_recolor(ui_Image3, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(ui_Image3, LV_OPA_COVER, LV_PART_MAIN);

  // Benchmark against a real screen rather than the boot screen, whose logo
  // otherwise dominates every measurement. Only with CONFIG_HMI_DEBUG_FPS.
  if (benchmark_drive_screen) {
    lv_screen_load(ui_DriveScreen);
  }
}

void finish_build(Fn<void()> strip_overdraw) {
  // LV_USE_PERF_MONITOR makes lv_display_create() show the overlay immediately
  // (lv_display.c calls lv_sysmon_show_performance), so start it hidden — it is
  // a debug readout, not part of the normal HMI. The label already exists by
  // now, which is what makes the hide safe. The "FPS counter" Skunk Works slot
  // toggles it from here on.
  lv_sysmon_hide_performance(lv_display_get_default());

  // Remove the redundant nested background fills (see strip_redundant_backgrounds).
  strip_overdraw();

  // Leave the boot logo. Spec V1 did this with a screen-change event set in
  // SquareLine; firmware owns it now, because the honest moment to leave is
  // when everything behind the first screen is wired -- which is here, not a
  // fixed delay the designer picked. kBootHoldMs is only so the wordmark is
  // readable rather than a flash.
  //
  // LockedScreen and not DriveScreen: locked_subject starts at 1 and the chair
  // does not drive until someone unlocks it.
  static constexpr uint32_t kBootHoldMs = 1200;
  lv_timer_t *boot_done = lv_timer_create(
      [](lv_timer_t *) {
        _ui_screen_change(&ui_LockedScreen, LV_SCREEN_LOAD_ANIM_FADE_ON, 280, 0,
                          &ui_LockedScreen_screen_init);
      },
      kBootHoldMs, nullptr);
  lv_timer_set_repeat_count(boot_done, 1);
}

void perf_overlay_font() {
  // The overlay hardcodes LVGL's 14 px default font (lv_sysmon_create sets no
  // font at all), which is unreadable on a 1280x720 panel at arm's length.
  // There's no API or Kconfig for it, but the label is parented to the sys
  // layer, and with LV_USE_MEM_MONITOR off it is that layer's only child.
  if (lv_obj_t *perf_label =
          lv_obj_get_child(lv_display_get_layer_sys(lv_display_get_default()), 0)) {
    lv_obj_set_style_text_font(perf_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_pad_all(perf_label, 10, 0); // grow the backing box to match
  }
}

// Wired as an action rather than through a SquareLine CALL FUNCTION event so it needs no
// round trip through the design tool (moved from main/frag_status_band.inc, fps_toggle).
void PerfOverlay::toggle() {
  shown_ = !shown_;
  if (shown_) {
    lv_sysmon_show_performance(lv_display_get_default());
  } else {
    lv_sysmon_hide_performance(lv_display_get_default());
  }
}

} // namespace hmi::ui
