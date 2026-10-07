// The widget-tree walks (moved from main/frag_settings_ui.inc).

#include "hmi_ui/widget_tree.hpp"

#include <array>
#include <cstdint>

// Pre-order, as the recursive walks it replaces: a child, everything under it,
// then the next child. An explicit stack instead of recursion (CS-FLW-01),
// bounded by WIDGET_TREE_MAX_DEPTH; each pass either visits a child or pops a
// level, so it ends after twice the number of widgets at most.
void hmi::ui::for_each_descendant(lv_obj_t *root, void (*visit)(lv_obj_t *obj, void *ctx),
                                  void *ctx) {
  struct Level {
    lv_obj_t *parent;
    uint32_t next; // the next child to visit
  };
  std::array<Level, WIDGET_TREE_MAX_DEPTH + 1> stack{};
  size_t depth = 0;
  stack[depth++] = {root, 0};
  while (depth > 0) {
    Level &top = stack[depth - 1];
    if (top.next >= lv_obj_get_child_count(top.parent)) {
      depth--;
      continue;
    }
    lv_obj_t *child = lv_obj_get_child(top.parent, static_cast<int32_t>(top.next));
    top.next++;
    visit(child, ctx);
    if (depth < stack.size()) {
      stack[depth++] = {child, 0};
    }
  }
}

// LVGL's click-focus (indev_click_focus in lv_indev.c) remembers the last
// CLICK_FOCUSABLE object a pointer pressed and DEFOCUSES it when the next one
// is pressed. Every lv_obj carries that flag by default, so the step buttons
// took part in it despite being in no group: tapping - and then + sent
// LV_EVENT_DEFOCUSED to -, and lv_obj_event strips LV_STATE_FOCUSED on that
// event, so a button inside a still-focused row dropped out of the focused
// theme. Nothing inside a row should have a focus life of its own, so the flag
// comes off every descendant and setting_focus_cb is left as the only writer
// of LV_STATE_FOCUSED in there. Descendants only: the row itself has to stay
// click-focusable, because that is how a tap on it reaches its group.
void hmi::ui::clear_click_focusable_recursive(lv_obj_t *obj) {
  for_each_descendant(
      obj, [](lv_obj_t *child, void *) { lv_obj_remove_flag(child, LV_OBJ_FLAG_CLICK_FOCUSABLE); },
      nullptr);
}

// A focused row has to look focused all the way down. LVGL only puts
// LV_STATE_FOCUSED on the object the group focused - the row itself - but the
// export gives every descendant its own MAIN|FOCUSED theme style, so without
// this the row's background would invert while the labels and the -/+ buttons
// on it stayed in the unfocused theme and became unreadable.
void hmi::ui::set_focused_recursive(lv_obj_t *obj, bool focused) {
  auto set = [](lv_obj_t *o, void *ctx) {
    if (*static_cast<const bool *>(ctx)) {
      lv_obj_add_state(o, LV_STATE_FOCUSED);
    } else {
      lv_obj_remove_state(o, LV_STATE_FOCUSED);
    }
  };
  set(obj, &focused);
  for_each_descendant(obj, set, &focused);
}
