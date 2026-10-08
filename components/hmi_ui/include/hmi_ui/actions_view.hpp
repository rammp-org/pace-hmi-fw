#pragma once
// The SkunkWorksScreen: a grid of one-press actions.

#include <cstdint>

#include "lvgl.h"

#include "hmi_ui/fn.hpp"
#include "hmi_ui/nav_port.hpp"

namespace hmi::ui {

/// One tile per action main lists (actions_spec.h): its title, its subtitle, and whether it
/// needs the MCB. What a tile does is main's (Config::run). The stick walks the tiles as the
/// grid the flex panel wraps them into; the stick button (or a tap) runs the focused one. A
/// tile that needs the MCB is greyed (UNAVAILABLE) while main says the MCB could not act on
/// it, stays walkable, and refuses a press. The screen and its tiles exist only while it is up.
class ActionsView {
public:
  static constexpr int TILES_MAX = 8;
  /// A tile the MCB could not act on right now. Not LV_STATE_DISABLED: LVGL drops every input
  /// event aimed at a disabled object, LV_EVENT_KEY included, so the cursor landing on a
  /// greyed tile could never leave it again. A user state greys it and leaves it walkable.
  static constexpr lv_state_t UNAVAILABLE = LV_STATE_USER_2;
  static constexpr lv_style_selector_t UNAVAILABLE_STYLE =
      static_cast<lv_style_selector_t>(LV_PART_MAIN) |
      static_cast<lv_style_selector_t>(UNAVAILABLE);

  struct Tile {
    const char *title;
    const char *subtitle;
    bool needs_mcb;
  };

  struct Config {
    const NavPort *nav;
    const Tile *tiles;        ///< `count` tiles, in the order they are drawn
    const Fn<void()> *run;    ///< what each tile does, by index; UI task (a click)
    int count;                ///< at most TILES_MAX
    Fn<void()> screen_ensure; ///< builds the screen if it is not up (main's on-demand code)
    /// binds a tile needing the MCB to every subject main's readiness test reads
    Fn<void(lv_obj_t *tile, lv_observer_cb_t observer)> bind_ready;
    lv_observer_cb_t ready_observer; ///< sets UNAVAILABLE on its target while the MCB is not
    void (*refuse)();                ///< the refusal cue for a greyed tile
  };

  constexpr explicit ActionsView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Creates the tiles' group (what outlives the screen).
  /// @return the group, for the joystick
  /// app_main, before lv_task starts.
  lv_group_t *init();
  /// The tiles' group (init()), for the joystick.
  [[nodiscard]] lv_group_t *group() const { return group_; }
  /// @brief Builds the tiles and shows the screen.
  /// UI task (a menu pick or a hold gesture).
  void open();
  /// @brief Deletes the tiles (their observers and events go with them).
  /// UI task (the screen's unload).
  void clear();
  /// @brief Puts the joystick on tile `index`, clamped.
  /// @param index the tile
  /// UI task.
  void focus(int index);

private:
  static void click_cb(lv_event_t *e);
  static void key_cb(lv_event_t *e);

  Config config_;
  lv_obj_t *buttons_[TILES_MAX]{};
  int button_count_ = 0; ///< tiles on screen; 0 while it is not up
  int cursor_ = 0;       ///< which tile the joystick is on
  int cols_ = 1;         ///< tiles per row, measured once they are laid out
  lv_group_t *group_ = nullptr;
};

} // namespace hmi::ui
