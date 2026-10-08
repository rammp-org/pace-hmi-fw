// Overdraw: strip redundant background fills. Moved from main.cpp's frag_overdraw.inc.
#include "hmi_ui/overdraw.hpp"

#include <iterator>
#include <numeric>

#include "ui.h"

namespace hmi::ui {
namespace {

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
bool color_eq(lv_color_t a, lv_color_t b) {
  return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

uint32_t strip_redundant_backgrounds(lv_obj_t *obj, lv_color_t behind, lv_obj_flag_t overlay) {
  uint32_t stripped = 0;
  lv_color_t painted = behind;
  // An overlay keeps its fill, subtree and all. The test below asks whether an
  // object paints what its ANCESTORS already painted, which is true of a
  // nested container and false of anything that covers a SIBLING: spec V2's
  // fault banner and burger menu are exactly the theme background, so their
  // fill reads as redundant -- and taking it away leaves them drawn on top of
  // the content they exist to hide, with the speed digits showing through the
  // fault text. Marked by `overlay` (main's kOverlayFlag) where they are wired.
  if (lv_obj_has_flag(obj, overlay)) {
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
    stripped += strip_redundant_backgrounds(lv_obj_get_child(obj, static_cast<int32_t>(i)), painted,
                                            overlay);
  }
  return stripped;
}

} // namespace

// The screen itself always keeps its fill: it is what the redundant children
// were duplicating, and something has to paint the background.
uint32_t strip_screen_overdraw(const lv_obj_t *screen, lv_obj_flag_t overlay) {
  const lv_color_t base = lv_obj_get_style_bg_color(screen, LV_PART_MAIN);
  uint32_t stripped = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++) {
    stripped += strip_redundant_backgrounds(lv_obj_get_child(screen, static_cast<int32_t>(i)), base,
                                            overlay);
  }
  return stripped;
}

// Every screen ui_init built. Kept in one place so the boot pass and the
// theme-change pass cannot drift apart. The screens built on demand are
// nullptr while they do not exist, and skipped. Returns how many fills it cleared.
uint32_t strip_all_overdraw(lv_obj_flag_t overlay) {
  const lv_obj_t *const screens[] = {ui_LockedScreen,    ui_DriveScreen,       ui_SeatScreen,
                                     ui_BenchGateScreen, ui_SettingsScreen,    ui_JoystickScreen,
                                     ui_LogScreen,       ui_SkunkWorksScreen,  ui_DiagnosticsScreen,
                                     ui_UpdateScreen,    ui_BenchMotorsScreen, ui_InternetScreen,
                                     ui_AboutScreen};
  const uint32_t stripped = std::accumulate(
      std::begin(screens), std::end(screens), uint32_t{0},
      [overlay](uint32_t sum, const lv_obj_t *screen) {
        return screen != nullptr ? sum + strip_screen_overdraw(screen, overlay) : sum;
      });
  return stripped;
}

} // namespace hmi::ui

// Kept out of the overdraw pass, and kept opaque whatever the theme says.
void hmi::ui::keep_overlay_fill(lv_obj_t *obj) {
  lv_obj_add_flag(obj, OVERLAY_FLAG);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
}
