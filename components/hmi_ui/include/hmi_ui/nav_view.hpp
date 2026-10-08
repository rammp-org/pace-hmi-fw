#pragma once
// Moving between screens: the burger key and its menu on every screen, the joystick's focus
// groups, and what happens when a screen comes up.

#include <array>
#include <cstdint>

#include "lvgl.h"

#include "hmi_ui/shared_subjects.hpp"

namespace hmi::ui {

/// The menu's destinations, in the order MenuOverlay draws them. The menu has two levels: the
/// top rows, and Settings' own rows on the SubMenu panel (after its "< Settings" row, which is
/// the way back up). Adding a row in SquareLine means a line here, a row id in
/// NavView::ROW_IDS and a case in NavView::go: the row order is the menu order, nothing else
/// encodes it.
enum NavDest : int {
  NAV_DRIVE, ///< "Drive": the Drive screen, or Locked (where it is asked for)
  NAV_SEAT,  ///< "Seat Functions"
  // The rest alphabetically.
  NAV_BENCH,    ///< "Bench", behind the PIN gate
  NAV_DIAG,     ///< "Diagnostics"
  NAV_JOYSTICK, ///< "Joystick", the test screen with CALIBRATE on it
  NAV_LOG,      ///< "Log"
  NAV_SETTINGS, ///< "Settings": opens the level below rather than a screen
  NAV_SKUNK,    ///< "Skunk Works"
  NAV_TOP_COUNT,
  // Settings' level, in SubMenu order.
  NAV_SET_DISPLAY = NAV_TOP_COUNT, ///< "Display & sound": a Settings page
  NAV_SET_STICK,                   ///< "Joystick & driving": a Settings page
  NAV_INTERNET,                    ///< "Internet": Ethernet or WiFi, and which network
  NAV_UPDATE,                      ///< "Firmware update": a release from GitHub
  NAV_ABOUT,                       ///< "About": the firmware, and the board
  NAV_DEST_COUNT,
};

/// One instance. Spec V2 navigates with the burger menu: the key at the bottom of every screen
/// opens a full-screen overlay of destinations, and DRIVE in the band goes home from anywhere.
/// Every call is on the UI task (LVGL events, timers and screen loads) with lvgl_mutex held,
/// or in app_main before lv_task starts. The stick gate is main's: `gate_update` runs at
/// exactly the gate's trigger points here (nav_close_menu, nav_open_menu, nav_go, nav_arrive;
/// drive_session_table.hpp GATE_TRIGGERS), in the same places as before.
class NavView {
public:
  struct Config {
    const SharedSubjects *shared; ///< `locked` is read; `rtps_link`, `mib_state` gate two rows
    lv_subject_t *menu_slide;     ///< int: 0 = the menu appears at once, 1 = it slides
    lv_obj_t **menu_open;         ///< main's nav_menu_open: the overlay up, else null
    bool *menu_on_arrival;        ///< main's nav_menu_on_arrival: open it on the next load
    void (*gate_update)();        ///< main's nav_update_stick_gate
    void (*keep_overlay_fill)(lv_obj_t *obj); ///< main's overdraw exemption
    /// Greys a gated row while the MCB could not act on it (main's action_ready_observer).
    lv_observer_cb_t ready_observer;
    bool (*mcb_ready)();   ///< link CONNECTED and the MIB IDLE or ENABLED
    void (*refuse_seat)(); ///< Seat Functions refused: the refusal feedback and banner
    /// The DRIVE row: the drive session's MENU_ROW_DRIVE; true when it refused the pick.
    bool (*drive_row)();
    void (*drive_key)(); ///< the burger key on Drive, unlocked: MENU_KEY_DRIVE
    /// The destinations main opens itself (Skunk Works, Diagnostics, the two Settings pages).
    void (*open_dest)(NavDest dest);
    /// A screen came up: hand the joystick its group and put the cursor at its top (main's
    /// per-screen wiring).
    void (*enter_screen)(const lv_obj_t *screen);
    /// After the arrival's gate update (main: the bench PIN is asked again on every visit).
    void (*arrived)(const lv_obj_t *screen);
  };

  /// p21 "HOME BUTTON": a dissolve from any screen back to Drive.
  static constexpr uint32_t HOME_FADE_MS = 120;
  /// The overlay sits over the body; sliding it clear of the screen bottom is the closed
  /// position. 921 px in 280 ms is the spec's transition (were kMenu* at the end of frag_diag).
  static constexpr int32_t MENU_SHOWN_Y = 195;
  static constexpr int32_t MENU_HIDDEN_Y = 195 + 921;
  static constexpr uint32_t MENU_SLIDE_MS = 280;
  /// p21 "ROW PRESS": the row holds negative this long before anything moves.
  static constexpr uint32_t ROW_PRESS_MS = 300;

  constexpr explicit NavView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Takes the joystick's keypad indev and makes the two groups nav owns, in this
  ///        order: the fallback group (the screens whose content nothing focuses: Drive,
  ///        Update and Boot; it holds nothing, so the stick's LVGL half is idle there while the
  ///        hold poll still reads the same latch for the exit hold), then the menu rows' group
  ///        (filled per overlay when the menu opens). The indev starts on the fallback group.
  /// app_main, before lv_task starts.
  void init_groups(lv_indev_t *indev);
  /// The joystick's keypad indev (null before init_groups).
  [[nodiscard]] lv_indev_t *indev() const { return indev_; }
  /// The fallback group, for the screens with at most a button or two of their own.
  [[nodiscard]] lv_group_t *fallback_group() const { return fallback_group_; }

  /// @brief One screen's chrome: its burger key, the menu overlay, and the DriveBand whose
  ///        DRIVE cell goes home (null: none). Done again for a screen rebuilt on demand.
  void attach_chrome(lv_obj_t *key, lv_obj_t *overlay, lv_obj_t *band);
  /// True when the screen has a burger key (its chrome is attached).
  [[nodiscard]] bool has_chrome(const lv_obj_t *screen) const;
  /// True while a picked row waits out ROW_PRESS_MS.
  [[nodiscard]] bool row_press_pending() const { return press_timer_ != nullptr; }

  /// Carries PRESSED/CHECKED/FOCUSED from a control to every layer of it.
  static void mirror_states(lv_obj_t *obj);
  /// The joystick's cursor ring on a button.
  static void focus_ring(lv_obj_t *obj);
  /// Only the outermost object of a composite stays clickable.
  static void claim_clicks(lv_obj_t *obj);
  /// A button in a screen's focus group: ringed, mirrored and walkable with the stick.
  static void focusable_button(lv_obj_t *button);

  /// Makes `g` the joystick's group, with `screen`'s burger key appended as its last member.
  void use_group(lv_group_t *g, const lv_obj_t *screen);
  /// Down past the end of a list, grid or log: on to the burger key.
  void to_key() const;
  /// Closes the menu over the screen that is up and gives the stick back to it.
  void close_menu();
  /// Opens the menu at its top level.
  void open_menu(lv_obj_t *overlay);
  /// DRIVE in the band: the Drive screen while driving, the Locked screen while not.
  void home();
  /// Swaps the screen underneath to `dest`.
  void go(NavDest dest);
  /// Everything that happens when a screen comes up (SCREEN_LOADED, or by hand).
  void arrive(const lv_obj_t *screen);
  /// The screen's name (the remote UI's SCREEN answer); "?" for one not listed.
  [[nodiscard]] static const char *screen_name(const lv_obj_t *screen);

private:
  struct Chrome {
    lv_obj_t *screen;
    lv_obj_t *key;
    lv_obj_t *overlay;
    NavView *view;
  };
  struct Row {
    NavView *view;
    NavDest dest;
  };

  Chrome *chrome_of(const lv_obj_t *screen);
  [[nodiscard]] const Chrome *chrome_of(const lv_obj_t *screen) const;
  static void ring_style(lv_obj_t *obj, int32_t width, int32_t pad);
  static void key_focus_ring(lv_obj_t *key);
  void menu_slide(lv_obj_t *overlay, int32_t from, int32_t to, bool hide_after) const;
  void key_open_look(const lv_obj_t *screen, bool open);
  void menu_level(lv_obj_t *overlay, bool sub, lv_obj_t *focus);
  void drop_menu(const lv_obj_t *screen);
  void row_picked(lv_event_t *e, NavDest dest);

  static void chrome_forget_cb(lv_event_t *e);
  static void mirror_cb(lv_event_t *e);
  static void menu_anim_cb(void *obj, int32_t y);
  static void menu_hide_cb(lv_anim_t *a);
  static void press_done_cb(lv_timer_t *t);
  static void row_cb(lv_event_t *e);
  static void sub_back_cb(lv_event_t *e);
  static void key_cb(lv_event_t *e);
  static void row_key_cb(lv_event_t *e);
  static void key_key_cb(lv_event_t *e);
  static void button_key_cb(lv_event_t *e);
  static void band_cb(lv_event_t *e);

  Config config_;
  std::array<Chrome, 16> chrome_{}; ///< each screen's key and overlay, as attach_chrome wires them
  std::array<Row, NAV_DEST_COUNT> rows_{}; ///< the rows' callback data (the same for every overlay)
  lv_indev_t *indev_ = nullptr;            ///< the joystick's keypad indev
  lv_group_t *fallback_group_ = nullptr;   ///< the screens with little of their own to focus
  lv_group_t *menu_group_ = nullptr;       ///< the open menu's rows
  lv_group_t *screen_group_ = nullptr;     ///< the group the screen underneath uses
  lv_timer_t *press_timer_ = nullptr;      ///< a picked row waiting out ROW_PRESS_MS
  int drop_row_ = -1;                      ///< the row the destination's overlay replays on arrival
  bool menu_sub_ = false; ///< which level the open menu shows: false = the top rows
};

} // namespace hmi::ui
