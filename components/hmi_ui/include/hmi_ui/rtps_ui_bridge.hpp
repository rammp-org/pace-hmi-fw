#pragma once
// The RTPS receive task's writes into the UI (app-main-shrink §3: the RTPS callbacks become an
// RtpsUiBridge in the UI component). Moved from main.cpp's app_main.

#include <cstdint>

#include "lvgl.h"

#include "drive_ui/fn.hpp"
#include "hmi_ui/drive_band_view.hpp"
#include "hmi_ui/refusal_view.hpp"
#include "hmi_ui/status_band_view.hpp"
#include "messages/joystick_message.hpp"
#include "messages/mib_message.hpp"

namespace hmi::ui {

/// @brief What an MCB status or a diagnostics sample changes in the UI: subjects only, whose
/// observers then repaint (CS-UI-05). Both calls run on the RTPS receive task with
/// lvgl_mutex held by the caller (main's handlers take it: lv_subject_set_int runs the
/// observers synchronously, on this task, and they touch widgets). No lock of its own. One
/// instance, UiApp's.
class RtpsUiBridge {
public:
  struct Config {
    /// int: the MIB's system state (UiApp's).
    lv_subject_t *mib_state;
    /// The Drive screen's speed and profile.
    DriveBandView *drive_band;
    /// The status band's state text.
    StatusBandView *status_band;
    /// The error banner's text and footer.
    RefusalView *refusal;
    /// The seat values (UiApp's seat_apply_state).
    Fn<void(const MIB::seatState &seat)> seat_apply_state;
    /// DiagnosticsView's readings, raw, [item][reading].
    lv_subject_t (*diag_values)[rammp::kDiagFields];
  };

  constexpr explicit RtpsUiBridge(const Config &config) noexcept
      : config_(config) {}

  /// @brief An MCB status: the system state, the profile and speed, the MIB's state and error
  /// texts, and the seat values.
  /// @param status The status as received.
  /// RTPS receive task, lvgl_mutex held.
  void apply_mib_status(const MIB::MibStatus &status) const;

  /// @brief A diagnostics sample: each reading the table has a subject for.
  /// @param diag The sample as received.
  /// RTPS receive task, lvgl_mutex held.
  void apply_diagnostics(const rammp::Diagnostics &diag) const;

private:
  Config config_;
};

} // namespace hmi::ui
