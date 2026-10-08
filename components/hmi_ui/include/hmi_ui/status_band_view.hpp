#pragma once
// The DriveBand's two status cells: DRIVE (ACTIVE or LOCKED) and STATE (what the MIB reports).

#include <array>
#include <cstddef>
#include <cstdint>

#include "lvgl.h"

#include "drive_ui/shared_subjects.hpp"

namespace hmi::ui {

/// One instance serves both status labels of every DriveBand. Each label gets an observer per
/// subject its text depends on; every one of them is object-bound and dies with the label.
/// Reads the `mib_state`, `rtps_link` and `locked` subjects of `SharedSubjects`, and owns the
/// MIB's two wordings, `drive_text` and `state_text` (CS-UI-05); all are initialised before
/// `bind` (V7).
class StatusBandView {
public:
  /// Shown instead of a stale STATE: the MCB has gone quiet, so the last value it sent is no
  /// longer something the HMI can stand behind.
  static constexpr const char *UNKNOWN_TEXT = "---";

  struct Config {
    const SharedSubjects *shared; ///< UiApp's subjects
  };

  /// The texts' buffer size, the shared spec's kMcbTextLen (UiApp static_asserts it): shows up
  /// to 15 chars.
  static constexpr size_t TEXT_SIZE = 16;

  constexpr explicit StatusBandView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Initialises the two text subjects empty: no override, so the cells start on the
  ///        enum's words. app_main, before any band binds (V7).
  void init_texts();
  /// string: the MIB's wording for the DRIVE cell (empty = none). Written by main's MibStatus
  /// handler under lvgl_mutex.
  [[nodiscard]] lv_subject_t *drive_text() { return &drive_text_; }
  /// string: the MIB's wording for the STATE cell (empty = none), written the same way.
  [[nodiscard]] lv_subject_t *state_text() { return &state_text_; }

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
  void show(lv_obj_t *label, Kind kind);

  Config config_;
  lv_subject_t drive_text_{};
  lv_subject_t state_text_{};
  std::array<char, TEXT_SIZE> drive_text_buf_{};
  std::array<char, TEXT_SIZE> drive_text_prev_buf_{};
  std::array<char, TEXT_SIZE> state_text_buf_{};
  std::array<char, TEXT_SIZE> state_text_prev_buf_{};
};

} // namespace hmi::ui
