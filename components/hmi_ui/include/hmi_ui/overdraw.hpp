#pragma once
// Overdraw: SquareLine gives every container an opaque background; these clear the fills
// nobody can see (src/overdraw.cpp has the rule).

#include <cstdint>

#include "lvgl.h"

namespace hmi::ui {

/// "This object covers what is behind it": kept out of the overdraw pass, with its fill. LVGL
/// leaves the USER flags for exactly this; nothing in the export uses them.
inline constexpr lv_obj_flag_t OVERLAY_FLAG = LV_OBJ_FLAG_USER_1;

/// @brief Marks `obj` an overlay (OVERLAY_FLAG) and keeps it opaque whatever the theme says.
/// UI task (app_main and the views' binds).
void keep_overlay_fill(lv_obj_t *obj);

/// @brief Clears the redundant background fills under one screen; the screen keeps its own.
/// @param screen The screen.
/// @param overlay The flag that marks an overlay: it keeps its fill, subtree and all.
/// @return How many fills it cleared.
/// UI task (app_main at boot, a screen's *_ensure, the theme switch).
uint32_t strip_screen_overdraw(const lv_obj_t *screen, lv_obj_flag_t overlay);

/// @brief strip_screen_overdraw over every screen ui_init built; screens built on demand that
/// do not exist now are skipped.
/// @param overlay The flag that marks an overlay.
/// @return How many fills it cleared.
/// UI task (app_main at boot, the theme switch).
uint32_t strip_all_overdraw(lv_obj_flag_t overlay);

} // namespace hmi::ui
