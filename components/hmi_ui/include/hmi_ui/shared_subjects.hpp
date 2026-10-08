#pragma once
// The UI island's subjects several views read (UiApp's, and app_state's blink). The views reach
// them only through this table, which UiApp fills once with their addresses; app-main-shrink S7
// moves each subject to the view that owns it and its row goes from here.

#include "lvgl.h"

namespace hmi::ui {

/// Pointers to subjects owned by main, shared by several views. Nothing here owns a subject:
/// main initialises each before any view binds to it (V7), on the UI task.
struct SharedSubjects {
  lv_subject_t *locked;     ///< int: 1 = locked, 0 = unlocked (set_locked)
  lv_subject_t *mib_state;  ///< int: MIB::MibSystemState, as the MIB last reported it
  lv_subject_t *rtps_link;  ///< int: LinkState (link_state.hpp)
  lv_subject_t *rtps_blink; ///< int: 0/1 blink phase, flipped by UiPoll
};

} // namespace hmi::ui
