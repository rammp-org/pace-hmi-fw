#pragma once
// The TopBar's RTPS label: coloured by the link state, and blinking while the network is up but
// nobody is peering.

#include <cstdint>

#include "lvgl.h"

#include "hmi_ui/shared_subjects.hpp"

namespace hmi::ui {

/// One instance serves the RTPS label of every TopBar. It reads the `rtps_link` and
/// `rtps_blink` subjects of `SharedSubjects`, both initialised before `bind` (V7).
class RtpsLabelView {
public:
  /// The label's colour while the network is up and nothing peers (NO_IP, NO_PEER).
  static constexpr uint32_t STATUS_ORANGE = 0xFF8C00;

  struct Config {
    const SharedSubjects *shared; ///< main's subjects; `rtps_link` and `rtps_blink` are read
  };

  constexpr explicit RtpsLabelView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Binds one TopBar instance's RTPS label to `rtps_link` and `rtps_blink`, in that
  ///        order. The observers are object-bound and die with the label.
  /// @param bar the TopBar instance; null does nothing
  /// UI task (app_main at boot, a screen's *_ensure later); lvgl_mutex held once lv_task runs.
  void bind(lv_obj_t *bar);

private:
  // Bound to both subjects; reads both regardless of which one fired. UI task (or the setter's
  // task, under lvgl_mutex: observers run synchronously).
  static void observer_cb(lv_observer_t *observer, lv_subject_t *subject);

  Config config_;
};

} // namespace hmi::ui
