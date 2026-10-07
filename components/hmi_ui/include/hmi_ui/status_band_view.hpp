#pragma once
// The DriveBand's two status cells: DRIVE (ACTIVE or LOCKED) and STATE (what the MIB reports).

#include <cstdint>

#include "lvgl.h"

#include "hmi_ui/shared_subjects.hpp"

namespace hmi::ui {

/// One instance serves both status labels of every DriveBand. Each label gets an observer per
/// subject its text depends on; every one of them is object-bound and dies with the label.
/// Reads the `mib_state`, `rtps_link`, `state_text` and `locked` subjects of `SharedSubjects`,
/// and binds `drive_text` too; all are initialised before `bind` (V7).
class StatusBandView {
public:
  /// Shown instead of a stale STATE: the MCB has gone quiet, so the last value it sent is no
  /// longer something the HMI can stand behind.
  static constexpr const char *UNKNOWN_TEXT = "---";

  struct Config {
    const SharedSubjects *shared; ///< main's subjects
  };

  constexpr explicit StatusBandView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Binds one DriveBand instance's two labels, seven observers in all, in the order
  ///        the band has always bound them. Called once per instance at start-up, and again by
  ///        each screen built on demand, since destroying a screen takes its bindings with it.
  /// @param panel the DriveBand instance; null does nothing
  /// UI task (app_main at boot, a screen's *_ensure later); lvgl_mutex held once lv_task runs.
  void bind(lv_obj_t *panel);

private:
  /// Which cell a label is.
  enum class Kind : uint8_t {
    DRIVE_STATUS, ///< DRIVE: ACTIVE or LOCKED
    STATE,        ///< STATE: OK, STARTING, ERROR or the MIB's own wording
  };

  // The observers of the DRIVE and the STATE label: whichever subject fired, the answer
  // depends on all of them, so the subject argument is ignored. UI task, or the setter's task
  // under lvgl_mutex (observers run synchronously in lv_subject_set_*).
  static void drive_observer_cb(lv_observer_t *observer, lv_subject_t *subject);
  static void state_observer_cb(lv_observer_t *observer, lv_subject_t *subject);
  void show(lv_obj_t *label, Kind kind) const;

  Config config_;
};

} // namespace hmi::ui
