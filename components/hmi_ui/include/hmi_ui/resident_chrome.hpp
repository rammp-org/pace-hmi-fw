#pragma once
// The chrome of every screen ui_init builds (moved from main.cpp's app_main wiring).

#include "lvgl.h"

#include "hmi_ui/fn.hpp"

namespace hmi::ui {

/// One screen's chrome: its TopBar, DriveBand, burger key and menu overlay, and whether the
/// band's DRIVE cell goes home.
struct ScreenChrome {
  lv_obj_t *bar;
  lv_obj_t *band;
  lv_obj_t *key;
  lv_obj_t *overlay;
  bool band_goes_home;
};

/// @brief Calls @p bind once per screen ui_init builds, in the screen order of the export's
///        header list. The three built on demand bind their own chrome when they are built,
///        and BenchMotorsScreen is destroyed after ui_init, so none of them is listed.
/// @param bind What to bind on each (UiApp: the band, TopBar and RTPS label views, and nav).
/// app_main, after ui_init, before lv_task starts.
void for_each_resident_chrome(Fn<void(const ScreenChrome &chrome)> bind);

} // namespace hmi::ui
