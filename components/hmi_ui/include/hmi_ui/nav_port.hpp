#pragma once
// What the views need from navigation: NavView's API as plain function pointers (main fills one
// table: NavView's static look functions, and the two calls on its one instance).

#include "hmi_ui/fn.hpp"
#include "lvgl.h"

namespace hmi::ui {

/// main's navigation calls, as plain function pointers (main/frag_state.inc fills one
/// `constexpr` table). All UI task.
struct NavPort {
  Fn<void()> to_key;                                             ///< focus the burger key
  Fn<void(lv_group_t *group, const lv_obj_t *screen)> use_group; ///< hand the joystick a group
  void (*focus_ring)(lv_obj_t *obj);    ///< the shared focus ring on a button
  void (*mirror_states)(lv_obj_t *obj); ///< carry PRESSED/CHECKED to the button's label
};

} // namespace hmi::ui
