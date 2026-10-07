#pragma once
// The screens built when opened and destroyed once left: Settings, Skunk Works, Diagnostics.

#include "lvgl.h"

namespace hmi::ui {

/// ui_init builds every screen at boot, and every widget lands in internal RAM - the
/// DMA-capable pool the W5500's SPI bounce buffer is later allocated from, with no NULL check
/// behind it. With the Settings and Skunk Works screens both resident that pool bottomed out at
/// 415 B, after the board had already boot-looped on it. So these, and Diagnostics, are
/// destroyed right after ui_init, built when opened, and destroyed again once left: at most one
/// exists at a time, and none while RTPS starts.
///
/// Everything the firmware hangs on one of them - DriveBand, TopBar and menu bindings, warning
/// banner, load hooks - is redone by its ensure function each time it is built. All of it is
/// object-bound, so it goes with the screen. One instance; UI task, lvgl_mutex held.
class OnDemandScreens {
public:
  struct Config {
    /// The chrome every screen carries, bound in this order: the DriveBand's status cells,
    /// the TopBar's RTPS label, its clock and link labels, then the burger key and menu.
    void (*bind_chrome)(lv_obj_t *band, lv_obj_t *bar, lv_obj_t *key, lv_obj_t *overlay);
    /// Settings' warning banner, bound to the cause and to the page (main's).
    void (*settings_bound)();
    /// Diagnostics' frequency label, bound to its subjects (main's).
    void (*diagnostics_bound)();
    /// Each screen's rows, cleared as it is left (before its destruction is queued).
    void (*settings_left)();
    void (*actions_left)();
    void (*diagnostics_left)();
    lv_event_cb_t screen_loaded; ///< main's SCREEN_LOADED handler (NavView arrival)
    void (*strip_overdraw)(const lv_obj_t *screen); ///< the redundant background fills
  };

  constexpr explicit OnDemandScreens(const Config &config) noexcept
      : config_(config) {}

  /// @brief Builds the Settings screen if it does not exist. The row template is deleted:
  ///        setting_page_open builds the rows.
  void ensure_settings();
  /// @brief Builds the Skunk Works screen if it does not exist; its placeholders are deleted.
  void ensure_actions();
  /// @brief Builds the Diagnostics screen if it does not exist; its template rows are deleted.
  void ensure_diagnostics();

private:
  // SCREEN_UNLOADED: clear the rows, then destroy the screen later (lv_async_call): the event
  // that asks for it is the screen's own, and an object is not deleted inside its own event.
  static void settings_unloaded_cb(lv_event_t *e);
  static void actions_unloaded_cb(lv_event_t *e);
  static void diagnostics_unloaded_cb(lv_event_t *e);
  // The deferred destructions; skipped if the screen was shown again in between.
  static void settings_destroy_cb(void *);
  static void actions_destroy_cb(void *);
  static void diagnostics_destroy_cb(void *);

  Config config_;
};

} // namespace hmi::ui
