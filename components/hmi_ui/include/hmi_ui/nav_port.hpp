#pragma once
// What the views need from main's navigation (frag_nav), until nav moves into hmi_ui.

#include "lvgl.h"

namespace hmi::ui {

/// main's navigation calls, as plain function pointers (main/frag_state.inc fills one
/// `constexpr` table). All UI task.
struct NavPort {
  void (*to_key)();                                             ///< focus the burger key
  void (*use_group)(lv_group_t *group, const lv_obj_t *screen); ///< hand the joystick a group
  void (*focus_ring)(lv_obj_t *obj);    ///< the shared focus ring on a button
  void (*mirror_states)(lv_obj_t *obj); ///< carry PRESSED/CHECKED to the button's label
};

} // namespace hmi::ui
