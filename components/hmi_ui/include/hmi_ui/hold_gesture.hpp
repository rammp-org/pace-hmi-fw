#pragma once
// Push-and-hold gestures: hold an input for HOLD_MS (after a grace period) and something
// happens. The widget a gesture fills is bound to its `progress` subject.

#include <cstdint>
#include <span>

#include "lvgl.h"

namespace hmi::ui {

/// The fill's range (LVGL's default for an arc or bar).
inline constexpr int32_t HOLD_MAX = 100;
/// How long a hold takes to fill, after the gesture's grace period.
inline constexpr uint32_t HOLD_MS = 1000;
/// Dead time before a hold starts filling. The stick button doubles as select, and a select is
/// a press shorter than this, so a tap never ticks a fill up and snaps it back.
inline constexpr uint32_t HOLD_GRACE_MS = 500;
/// Poll cadence for the inputs. Matched to the ADC task's 33 ms period: joy_key cannot change
/// faster than that, so a shorter period would only burn UI task time re-reading the same value.
inline constexpr uint32_t HOLD_POLL_MS = 33;

class HoldEngine;

/// @brief Is a pointer (touch) down on `target` while `screen` is the active screen: the touch
///        half of a hold, polled at the gesture's cadence rather than tracked from press
///        events, so there is no event to miss. False when `target` is null.
/// UI task, lvgl_mutex held.
[[nodiscard]] bool touch_held_on(const lv_obj_t *screen, const lv_obj_t *target);

/// One gesture. `progress`, `holding` and `engine` carry defaults so a definition names only
/// the fields a gesture differs in (-Wmissing-field-initializers accepts a defaulted member).
struct HoldGesture {
  lv_subject_t progress{};            ///< 0..HOLD_MAX; the arc/bar is bound to this
  bool *armed;                        ///< the input's "released since the last completion"
  bool (*is_held)();                  ///< is the input held right now (sampled on the UI task)
  bool (*applies)();                  ///< is this gesture live on the current screen/page
  void (*completed)();                ///< what a full hold does
  uint32_t grace_ms = 0;              ///< dead time before the widget starts filling
  bool holding = false;               ///< edge detector for is_held && applies
  const HoldEngine *engine = nullptr; ///< the engine that started its fill (set by `poll`)
};

/// Runs the gestures: starts a fill when a held, armed input meets a gesture that applies,
/// cancels it when either goes, and completes it when the fill is full. One instance; every
/// call runs on the UI task with lvgl_mutex held (the poll timer, the fill animation).
class HoldEngine {
public:
  struct Config {
    /// Confirmation feedback when a hold completes, the same for every gesture (main's
    /// STRONG_CLICK haptic and click sound). Must not block the UI task.
    void (*confirm)();
    /// The self-test overlay is up: it owns the stick, so every fill is cancelled.
    bool (*overlay_up)();
    /// Runs before the gestures on each poll without the overlay (main's refusal check, which
    /// shadows the unlock gesture on the same input and cadence).
    void (*before_poll)();
  };

  constexpr explicit HoldEngine(const Config &config) noexcept
      : config_(config) {}

  /// @brief One poll of one gesture: re-arms its input when released; starts or cancels the
  ///        fill on a change of "held, armed and applies".
  /// UI task, lvgl_mutex held.
  void poll(HoldGesture *g) const;

  /// @brief One poll of every gesture, in order (main's hold poll timer, every HOLD_POLL_MS).
  ///        With the overlay up, cancels any fill instead and polls nothing.
  /// UI task, lvgl_mutex held.
  void poll_all(std::span<HoldGesture *const> gestures) const;

  /// @brief Cancels any fill in flight and empties the widget. The gesture is also the
  ///        animation's `var`, so it is the handle lv_anim_delete matches on.
  /// UI task, lvgl_mutex held.
  static void reset(HoldGesture *g);

private:
  static void anim_exec_cb(void *var, int32_t value);
  static void anim_completed_cb(lv_anim_t *a);

  Config config_;
};

} // namespace hmi::ui
