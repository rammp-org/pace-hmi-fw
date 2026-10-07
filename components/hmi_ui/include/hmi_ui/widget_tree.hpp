#pragma once
// Walks over a widget and everything under it.

#include <cstddef>

#include "lvgl.h"

namespace hmi::ui {

/// How deep for_each_descendant goes below its root. The export's deepest tree is far less
/// (screen, panel, component, container, label); a branch below this is not visited.
inline constexpr size_t WIDGET_TREE_MAX_DEPTH = 32;

/// @brief Calls `visit(obj, ctx)` for every descendant of `root` (not `root` itself), in
///        pre-order: a child, then everything under it, then the next child. That is the order
///        a recursive walk visits them, without the recursion (CS-FLW-01).
/// @param root the widget whose descendants are visited
/// @param visit the visitor; it must not add or delete children
/// @param ctx passed to `visit`
/// UI task.
void for_each_descendant(lv_obj_t *root, void (*visit)(lv_obj_t *obj, void *ctx), void *ctx);

/// @brief Sets or clears LV_STATE_FOCUSED on `obj` and every widget under it. A focused row
///        has to look focused all the way down: LVGL only puts the state on the object the
///        group focused, but the export gives every descendant its own MAIN|FOCUSED style.
/// @param obj the row
/// @param focused the state
/// UI task.
void set_focused_recursive(lv_obj_t *obj, bool focused);

/// @brief Clears LV_OBJ_FLAG_CLICK_FOCUSABLE on every widget under `obj` (not `obj`), so a tap
///        on a label inside a button focuses nothing but the button.
/// @param obj the button or row
/// UI task.
void clear_click_focusable_recursive(lv_obj_t *obj);

} // namespace hmi::ui
