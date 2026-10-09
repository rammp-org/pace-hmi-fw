#pragma once
// The UI island's views, wired together: every view instance main.cpp used to define, with its
// Config, and the glue between them (arrivals, the menu's destinations, the holds, the seat
// command path, the settings and refusal observers), moved from main/main.cpp. What the UI
// needs from the rest of the firmware comes in through the ports of its Config (app_ports.hpp).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "logger.hpp"
#include "lvgl.h"

#include "messages/joystick_message.hpp"
#include "messages/mib_message.hpp"
#include "settings.hpp"
#include "settings_spec.hpp"
#include "stick/permit_hooks.hpp"

#include "drive_ui/drive_ui.hpp"
#include "drive_ui/fn.hpp"
#include "drive_ui/shared_subjects.hpp"
#include "drive_ui/stick_button.hpp"
#include "hmi_ui/actions_spec.h"
#include "hmi_ui/actions_view.hpp"
#include "hmi_ui/app_ports.hpp"
#include "hmi_ui/app_state.hpp"
#include "hmi_ui/bench_pin_view.hpp"
#include "hmi_ui/brightness_view.hpp"
#include "hmi_ui/diagnostics_view.hpp"
#include "hmi_ui/display_flip.hpp"
#include "hmi_ui/drive_band_view.hpp"
#include "hmi_ui/hold_gesture.hpp"
#include "hmi_ui/nav_port.hpp"
#include "hmi_ui/nav_view.hpp"
#include "hmi_ui/on_demand_screens.hpp"
#include "hmi_ui/overdraw.hpp"
#include "hmi_ui/post_stage.hpp"
#include "hmi_ui/refusal_texts.hpp"
#include "hmi_ui/refusal_view.hpp"
#include "hmi_ui/rtps_label_view.hpp"
#include "hmi_ui/rtps_ui_bridge.hpp"
#include "hmi_ui/seat_view.hpp"
#include "hmi_ui/settings_view.hpp"
#include "hmi_ui/status_band_view.hpp"
#include "hmi_ui/topbar_view.hpp"
#include "hmi_ui/ui_build.hpp"
#include "hmi_ui/ui_poll.hpp"

namespace hmi::ui {

struct ScreenChrome;

/// The Skunk Works tiles, one per actions_spec.h row, in table order.
inline constexpr ActionsView::Tile ACTION_TILES[] = {
#define ACTIONS_ROW(name_, title_, subtitle_, mcb_) {title_, subtitle_, (mcb_) != 0},
    ACTIONS_TABLE(ACTIONS_ROW)
#undef ACTIONS_ROW
};
static_assert(ACTION_COUNT <= ActionsView::TILES_MAX, "more actions than tiles");

/// One instance (main's, constinit: no global constructor, G4). Every view of the UI island and
/// the glue between them. Each call runs on the UI task (an LVGL timer, event or observer) with
/// lvgl_mutex held, or in app_main while it builds the UI before lv_task starts, unless it says
/// otherwise. No call here takes a lock: main's entry points on other tasks hold lvgl_mutex
/// around the views they reach (brightness, stick_button, rtps_bridge, display_flip).
class UiApp {
public:
  struct Config {
    const LinkPort *link;                 ///< the RTPS link
    const DriveInputs *drive;             ///< main's DriveAdapter, made over drive_ui()
    const CuesPort *cues;                 ///< haptic and sound cues
    const SelfTestPort *selftest;         ///< the self test
    const BoardPort *board;               ///< backlight, panel, restart
    const MainScreens *screens;           ///< the screens whose adapters stay in main
    const std::atomic<bool> *clock_valid; ///< the system clock holds a real time (housekeeping)
    espp::Logger *nav_log;                ///< "nav": the lost-cursor backstop's warning
    espp::Logger *flip_log;               ///< "flip": the screen flip
    espp::Logger *overdraw_log;           ///< "overdraw": what the overdraw pass cleared
    const PostPort *post;                 ///< the quick POST's IDF facts (main)
  };

  /// The Settings page of the actuator rows, after the settings_spec.hpp pages.
  static constexpr int32_t ACTUATORS_PAGE = SETTINGS_PAGE_COUNT;
  /// How long the backlight level must stay unchanged before it is saved.
  static constexpr uint32_t BRIGHTNESS_SAVE_DELAY_MS = 1000;
  /// The bench gate's PIN: a "not by accident" barrier in front of a bench screen, not a
  /// secret (anyone holding the firmware image has it).
  static constexpr char BENCH_PIN[] = "1234";
  static constexpr size_t BENCH_PIN_LEN = sizeof(BENCH_PIN) - 1;
  static_assert(BENCH_PIN_LEN == BenchPinView::PIN_LEN, "the model judges a four-digit PIN");

  constexpr explicit UiApp(const Config &config) noexcept
      : config_(config) {}
  UiApp(const UiApp &) = delete;
  UiApp &operator=(const UiApp &) = delete;
  UiApp(UiApp &&) = delete;
  UiApp &operator=(UiApp &&) = delete;
  ~UiApp() = default;

  // --- The build, called by app_main in this order, before lv_task starts ------------------

  /// @brief The build up to the joystick's input: the saved settings, the theme and the
  ///        backlight; every Settings row's subject; the Joystick screen's bars; the MCB, link
  ///        and band subjects; every resident screen's chrome, the clock and the Drive band; the
  ///        error banners, the diagnostics subjects and the 250 ms poll; the Joystick screen's
  ///        calibration and button counter.
  void build();
  /// @brief The build from the joystick's input on: the input's groups, backstop and key
  ///        repeat; the padlock and the three holds; the Log, Seat, Internet and About screens;
  ///        the Update screen; the seat values and the screen-loaded hooks; the build's finish
  ///        (perf overlay hidden, the overdraw pass, the boot screen's exit).
  /// @param keypad the joystick's keypad indev
  void build_input(lv_indev_t *keypad);
  /// @brief What outlives the screens built on demand (BenchGate's PIN pad, Settings, Skunk
  ///        Works, Diagnostics) and the perf overlay's font. After the self test's init.
  void build_on_demand_parts();

  // --- What main reaches ---------------------------------------------------------------------

  /// The drive UI, DrivePort's `Ui` (main makes the DriveAdapter over it). Constant.
  [[nodiscard]] constexpr DriveUi *drive_ui() noexcept { return &drive_ui_; }
  /// Navigation (the screens main keeps hand it their groups).
  [[nodiscard]] constexpr NavView &nav() noexcept { return nav_view_; }
  /// The backlight (main's brightness_set and brightness_step, under lvgl_mutex).
  [[nodiscard]] constexpr BrightnessView &brightness() noexcept { return brightness_view_; }
  /// The stick button's edges (the GPIO48 task and the remote UI, under lvgl_mutex).
  [[nodiscard]] constexpr StickButton &stick_button() noexcept { return stick_button_; }
  /// The TopBar (main sets the link label once RTPS has started, under lvgl_mutex).
  [[nodiscard]] constexpr TopBarView &topbar() noexcept { return topbar_view_; }
  /// DIRECT rendering and the flip (app_main hands it the panel buffers and the touch input).
  [[nodiscard]] constexpr DisplayFlip &display_flip() noexcept { return display_flip_; }
  /// What an MCB status or a diagnostics sample changes (the RTPS handlers, under lvgl_mutex).
  [[nodiscard]] constexpr const RtpsUiBridge &rtps_bridge() const noexcept { return rtps_bridge_; }
  /// int: MIB::MibSystemState, as the MIB last reported it.
  [[nodiscard]] constexpr lv_subject_t *mib_state() noexcept { return &mib_state_; }
  /// The stick output permit's channels (stick/permit_hooks.hpp): the POST gate, stick health
  /// and the hold reason. Any task, each through its own end (the table there).
  [[nodiscard]] constexpr hmi::stick::PermitHooks &permit_hooks() noexcept { return permit_hooks_; }
  /// The quick POST on the UI task: its runner, the rest windows, the indicator's state.
  [[nodiscard]] constexpr PostStage &post() noexcept { return post_stage_; }

  /// @brief The joystick's LVGL keypad read: moves the cursor through each screen's focus
  ///        group. It drains the latch the ADC task fills, so one flick of the stick = one
  ///        PRESSED cycle = one LV_EVENT_KEY.
  /// UI task (the keypad indev's read), lvgl_mutex held.
  void keypad_read(bool *up, bool *down, bool *left, bool *right, bool *enter, bool *escape);

private:
  // The build's steps (main's settings_ui_init .. screen_hooks_init), in build()'s order.
  void init_settings();
  void init_status_subjects();
  void bind_chrome();
  void bind_resident_chrome(const ScreenChrome &c);
  void bind_banners();
  void init_joystick_screen();
  void init_lock_screen();
  void init_screens();
  void init_update_screen();
  void init_screen_hooks();
  void bind_chrome_views(lv_obj_t *band, lv_obj_t *bar);

  // Readiness, holds and drive inputs.
  bool mcb_ready();
  bool seat_ready();
  void profile_clicked();
  bool entry_push();
  void hold_confirm();
  bool unlock_applies();
  bool drive_exit_applies();
  bool calibrate_applies();
  static bool joy_button_held();
  static bool calibrate_held();
  static bool menu_open();
  static int64_t now_us();
  static void store_profile(int32_t profile);
  static void hold_poll_cb(lv_timer_t *timer);
  static void rtps_poll_cb(lv_timer_t *timer);

  // Seat, bench gate, Settings, Skunk Works, Diagnostics.
  void seat_apply_state(const MIB::seatState &seat);
  void seat_request(rammp::SeatAxis axis, int32_t target);
  void seat_step(size_t row, int direction);
  void bench_accepted();
  static void style_pin_key(lv_obj_t *key);
  void store_setting(int param, int32_t value);
  static void setting_warning_observer(lv_observer_t *observer, lv_subject_t *subject);
  void bind_ready(lv_obj_t *tile, lv_observer_cb_t observer);
  static void action_ready_observer(lv_observer_t *observer, lv_subject_t *subject);
  void action_seat_up();
  static void diag_freq_observer(lv_observer_t *observer, lv_subject_t *subject);

  // Navigation and the screens built on demand.
  void refuse_seat();
  bool nav_drive_row();
  void nav_drive_key();
  void open_dest(NavDest dest);
  void enter_screen(const lv_obj_t *screen);
  void arrived(const lv_obj_t *screen);
  void cursor_lost(const lv_obj_t *screen);
  static void screen_loaded_cb(lv_event_t *e);
  void bind_on_demand_chrome(lv_obj_t *band, lv_obj_t *bar, lv_obj_t *key, lv_obj_t *overlay);
  void settings_bound();
  void diagnostics_bound();
  static void strip_screen(const lv_obj_t *screen);
  void strip_overdraw_logged();
  void theme_switched(uint8_t theme);
  void post_indicator();

  // Members in dependency order: a view's Config may read only what is declared above it.
  Config config_;

  // The stick output permit's channels, shared with the ADC task (hazard-c1-spec.md §3.2,
  // hazard-c3-spec.md §2.4): constant initialised with the rest of UiApp.
  hmi::stick::PermitHooks permit_hooks_{};
  // The quick POST (hazard-c3-spec.md §2.3): the POST gate's only writer.
  PostStage post_stage_{{
      .port = config_.post,
      .gate = &permit_hooks_.post_gate,
  }};

  // The subjects several views read (were main's mib_state_subject, rtps_link_subject,
  // locked_subject, entry_refused_subject, seat_axis_value).
  lv_subject_t mib_state_{}; ///< int: MIB::MibSystemState, as the MIB last reported it
  lv_subject_t rtps_link_{}; ///< int: LinkState
  lv_subject_t locked_{};    ///< int: 1 = locked, 0 = unlocked (set_locked)
  lv_subject_t refused_{};   ///< int: Refused; refusal panels up unless REFUSED_NONE
  /// Raw value per seat axis, in the table's units, as the MCB last reported it.
  lv_subject_t seat_axis_value_[rammp::kSeatAxisCount]{};
  uint8_t seat_axis_count_ = 0; ///< actuators in the table

  SharedSubjects shared_{
      .locked = &locked_,
      .mib_state = &mib_state_,
      .rtps_link = &rtps_link_,
      .rtps_blink = &rtps_blink_subject,
  };
  NavPort nav_port_{
      .to_key = hmi::ui::bind<&NavView::to_key>(&nav_view_),
      .use_group = hmi::ui::bind<&NavView::use_group>(&nav_view_),
      .focus_ring = NavView::focus_ring,
      .mirror_states = NavView::mirror_states,
  };

  // The chrome: every DriveBand's status cells, every TopBar's RTPS label, clock and link.
  PerfOverlay perf_overlay_; ///< the "FPS counter" Skunk Works slot
  // MCB status panel
  //
  // One view (hmi::ui::StatusBandView) serves both labels on every DriveBand
  // instance. It sets the text (from the shared spec, so the MCB's logs and
  // these labels use the same words) and the colour — a style property, which
  // has no built-in binding, hence an observer rather than lv_label_bind_text.
  StatusBandView status_band_view_{{.shared = &shared_}};
  RtpsLabelView rtps_label_view_{{.shared = &shared_}};
  // DriveScreen: drive-mode selection and the speed readout (hmi::ui::DriveBandView)
  //
  // Three plain LVGL buttons, so they are already touch-clickable; all this adds
  // is what a tap means and which one looks selected. Deliberately touch-only:
  // the joystick is busy driving on this screen, and stealing left/right for menu
  // navigation is exactly the class of bug that made pulling back exit the
  // screen.
  DriveBandView drive_band_view_{{
      .profiles = {static_cast<int32_t>(MIB::DriveProfile::HIGH),
                   static_cast<int32_t>(MIB::DriveProfile::NORMAL),
                   static_cast<int32_t>(MIB::DriveProfile::LOW)},
      // The ADC task cannot take the LVGL lock, so the profile reaches it through an atomic.
      .store_profile = store_profile,
      // PUBLISH_DRIVE: the drive request as it stands, with the new profile (rows 29-34).
      .profile_clicked = hmi::ui::bind<&UiApp::profile_clicked>(this),
      .nav = &nav_port_,
  }};
  // TopBar clock
  //
  // The MIB sends Unix time plus its UTC offset in every MibStatus. It sets the
  // system clock and the RTC, so the time keeps running through a lost link, and
  // across a reboot without the MIB. Clock1 on every TopBar shows the system
  // clock. No TZ is set, so the system clock simply holds the local wall time the
  // MIB reported and nothing converts again.
  TopBarView topbar_view_{{.clock_valid = config_.clock_valid}};
  // Backlight
  //
  // One brightness setting, 5..100 %, whoever changes it: the RTPS brightness
  // command, the Tab5's side button, and the Brightness row of Settings.
  // Saved a second after it stops changing, so a run
  // of steps is one flash write rather than one per step.
  // Its subject is the Settings row's (SettingSubjects): the row steps it too.
  BrightnessView brightness_view_{{
      .subject = setting_subjects.value(SETTINGS_PARAM_BRIGHTNESS),
      .min_percent = kBrightnessMinPercent,
      .max_percent = kBrightnessMaxPercent,
      .backlight = config_.board->backlight,
      .save = settings_set_brightness,
  }};

  // Saying why driving is not permitted, the padlock, and the holds.
  // Saying why driving is not permitted (hmi::ui::RefusalView)
  //
  // One cause, several ErrorBanners. On the Locked screen, and on the screens a
  // menu refusal can happen over: why a request to drive, or to open Seat
  // Functions, was refused - the unlock hold is gated in applies(), so a refused
  // one otherwise does nothing at all. On the DriveScreen and the SeatScreen: why
  // it was cut short, by the link dropping or the MCB faulting. All word the
  // cause from hmi_rtps_spec.hpp (REFUSAL_TEXTS).
  RefusalView refusal_view_{{
      .shared = &shared_,
      .texts = &REFUSAL_TEXTS,
      .refused = &refused_,
      .wifi = config_.link->wifi,
      .mcb_ready = hmi::ui::bind<&UiApp::mcb_ready>(this),
      .play_refusal = config_.cues->refusal,
      .keep_overlay_fill = keep_overlay_fill,
      .button_held = joy_button_held,
      .now_us = now_us,
      .menu_open = menu_open,
      // A push is decided by the drive session (rows 38-39: locked, on the Locked screen, no
      // menu, MCB not ready).
      .entry_push = hmi::ui::bind<&UiApp::entry_push>(this),
      .grace_ms = HOLD_GRACE_MS,
  }};
  // Locked means "not driving", and the Locked screen is where that changes.
  // Holding the stick button -- the legend under the padlock says so; the spec's
  // ACTIVATE DRIVE button is gone -- asks the MIB to start driving, and nothing
  // unlocks until the MIB says it has (the drive session; DrivePort::lock_open_visual). The ring
  // round the padlock shows where that stands: it fills while the button is held, a quarter of it
  // goes round while the MIB is asked, and it closes when the MIB answers. Then the spec's 01b
  // frame -- the shackle rises, the band flips to ACTIVE -- held one second, and Drive dissolves in
  // (Motion timing, "ACTIVATE DRIVE").
  //
  // Locking again is the MIB's call too: the chair stops, asked (the drive-exit
  // hold) or not (a fault, the link), and the drive session's relock (TICK_FOLLOW) brings the
  // Locked screen back with the reason on its banner.
  DriveUi drive_ui_{{
      .shared = &shared_,
      .refused = &refused_,
      .refused_timer = hmi::ui::bind<&RefusalView::timer>(&refusal_view_),
      .menu_open = &nav_menu_open,
      .menu_on_arrival = &nav_menu_on_arrival,
      .profile = &drive_profile_published,
      .publish_drive = config_.link->publish_drive,
      .input = config_.drive->input,
      .stick_drives = &stick_drives,
      .nav_home = hmi::ui::bind<&NavView::home>(&nav_view_),
      .refusal_feedback = config_.cues->refused,
      .haptic_click = config_.cues->haptic_click,
  }};
  // The one engine; its confirmation is the same for all three gestures.
  // Push-and-hold gestures
  //
  // Three places in the HMI ask the user to hold an input for HOLD_MS before
  // something happens:
  //
  //   LockedScreen   stick button    enters driving mode  -> DriveScreen
  //   DriveScreen    stick button    leaves driving mode  -> LockedScreen
  //   JoystickScreen stick button, or a finger on CALIBRATE: starts calibration
  //
  // Both on the button, never the stick (issue #11): an enable held on the stick
  // left the user holding it forward the moment the chair would take it, and
  // the chair drove off at full speed.
  //
  // Everything else that used to be a hold -- enter seat, enter actions, and the
  // "pull the stick back to leave" exits on seat, bench, settings, actions,
  // diagnostics and log -- is a burger-menu row now, one tap away from anywhere,
  // so those gestures and the bars that showed their fill are gone.
  //
  // The DriveScreen is the odd one out because pulling the stick back is how you
  // reverse: an exit on LV_KEY_DOWN there fired every time the user drove
  // backwards, so it exits on the stick button instead.
  //
  // Each gesture's fill runs through a subject: the ring round the padlock and
  // Calibrate's meter are bound to theirs, and the exit hold's has no widget.
  //
  // The lock state itself is locked_, with the other subjects above. Locked still leaves the menu
  // reachable -- Log, UI Settings and the bench tools are useful with the chair not driving -- but
  // nothing moves: the stick only drives from Drive (stick_drives).
  HoldEngine hold_engine_{{
      .confirm = hmi::ui::bind<&UiApp::hold_confirm>(this),
      // The self-test overlay owns the stick while it is up (see keypad_read).
      .overlay_up = config_.selftest->overlay_visible,
      .before_poll = hmi::ui::bind<&RefusalView::poll>(&refusal_view_),
  }};
  // Holding the stick button on the Locked screen: how driving is asked for, the same hold
  // that leaves it on Drive (and so the same "let go first" flag). Only while there is
  // something to ask; a MIB that is not ready gets the refusal once the press is a hold.
  HoldGesture unlock_gesture_{
      .armed = drive_ui_.button_armed(),
      .is_held = joy_button_held,
      .applies = hmi::ui::bind<&UiApp::unlock_applies>(this),
      .completed = config_.drive->unlock_hold_done,
      // The button doubles as select (a tap opens the menu from the key), so a tap must not
      // tick the ring.
      .grace_ms = HOLD_GRACE_MS,
  };
  // Exits on the stick BUTTON, not on pulling the stick back: pulling back is how you drive in
  // reverse. It asks the MIB to stop; the session's relock closes the screen once it has.
  HoldGesture drive_exit_gesture_{
      .armed = drive_ui_.button_armed(),
      .is_held = joy_button_held,
      .applies = hmi::ui::bind<&UiApp::drive_exit_applies>(this),
      .completed = config_.drive->exit_hold_done,
      .grace_ms = HOLD_GRACE_MS,
  };
  // Calibrate is press-and-HOLD: the stick button, or a finger on Calibrate, with its own
  // "let go first" flag (DriveUi's calibrate_armed).
  HoldGesture calibrate_gesture_{
      .armed = drive_ui_.calibrate_armed(),
      .is_held = calibrate_held,
      .applies = hmi::ui::bind<&UiApp::calibrate_applies>(this),
      .completed = config_.screens->calibration_toggle,
      .grace_ms = HOLD_GRACE_MS,
  };
  std::array<HoldGesture *, 3> hold_gestures_{
      &unlock_gesture_,
      &drive_exit_gesture_,
      &calibrate_gesture_,
  };
  // The stick button's edges: the pressed panel, the level the ADC task publishes, the select
  // key and the press counter.
  // One edge of the stick button, from the GPIO or from the remote UI channel --
  // one path, so what a script presses is the real handling. Touches subjects,
  // never widgets directly. Any task.
  //
  // "Select" -- the keypad's ENTER, which clicks whatever the joystick has
  // focused -- fires on RELEASE, and only for a press shorter than
  // hmi::stick::SELECT_MAX_US. The same button is held to stop driving and to start a
  // calibration, and a select on the press edge would have clicked the focused
  // control (on the Drive screen: opened the menu) at the start of every hold.
  // SELECT_MAX_US is a hold's grace period, so a press short enough to select
  // never starts a hold filling, and one long enough to fill never selects.
  StickButton stick_button_{{
      .pressed = joystick_view.pressed(),
      .count = joystick_view.count(),
      .level = &joy_button_pressed,
      .select = &select_key,
  }};

  // The screens.
  // SeatScreen: joystick navigation of both pages
  //
  // The joystick walks each page as the grid the user sees rather than as the
  // flat list LVGL's own focus_next would give. Each page has its own group and
  // its own cursor:
  //
  //   function buttons                adjustment page (spec 04b)
  //   [ FB Tilt   ] [ Side Tilt   ]   [ < ]
  //   [ Elevation ] [ Translation ]   [     -     ] [     +     ]
  //   [ Static    ] [ Dynamic     ]   [ 0deg ] [ 15deg ] [ 25deg ]
  //
  // The rows are different lengths, which is why a grid carries a per-row count
  // rather than one column total. The ButtonGrid also serves the bench gate's
  // PIN pad.
  //
  // Picking one of the four function buttons names its motion on the adjustment
  // page and shows it over the buttons; "<", or left from the first column,
  // hides it again. The page's buttons step the motion or send it to a preset.
  SeatView seat_view_{{
      .nav = &nav_port_,
      .values = seat_axis_value_,
      .step = hmi::ui::bind<&UiApp::seat_step>(this),
      .request = hmi::ui::bind<&UiApp::seat_request>(this),
      .show_buttons_page = hmi::ui::bind<&SeatView::show_buttons_page>(&seat_view_),
      .keep_overlay_fill = keep_overlay_fill,
  }};
  // BenchGateScreen: the 4-digit PIN
  //
  // Spec V2 draws the pad as eleven discrete buttons (1-9, 0, backspace) rather
  // than the one lv_keyboard the old screen used, so LVGL brings neither the key
  // text nor the 2D arrow walk with it: each button carries its digit in
  // user_data, and the joystick walks them through the same ButtonGrid the seat
  // pages use. The bottom row has no left-hand key, which is the hole
  // grid_key_cb steps over.
  //
  // The four dots above are the only readout. They are not clickable, they just
  // show how many digits are in; the export gives them no CHECKED look, so the
  // wiring adds one in the theme's text colour.
  //
  // The PIN is a build-time constant. It gates a bench screen, not anything
  // safety-related, so it is a "not by accident" barrier rather than a secret:
  // anyone holding the firmware image has it either way.
  BenchPinView bench_pin_view_{{
      .pin = std::string_view(BENCH_PIN, BENCH_PIN_LEN),
      .accepted = hmi::ui::bind<&UiApp::bench_accepted>(this),
      .off_bottom = hmi::ui::bind<&NavView::to_key>(&nav_view_),
      .style_key = style_pin_key,
  }};
  // SettingsScreen: pages of -/+ rows
  //
  // One screen for every setting that is a few numbers. A page is a title, a
  // line of instructions and some rows; each row is the export's
  // SettingRow component (Parameter1 is only the template, deleted at boot): short
  // label, label, value, and - / + buttons that grey out at the ends of the
  // range. Up/down move between rows, left/right step the focused one (holding
  // the stick repeats), and touch works on the buttons.
  //
  // Two kinds of page, which differ only in what a step does:
  //   settings   settings_spec.hpp. The step sets the row's subject, clamped to
  //              the range; whatever owns that subject applies and saves it.
  //   actuators  RAMMP_SEAT_AXIS_TABLE, reached through the PIN (DEBUG
  //              ACTUATORS). The HMI owns none of these values: a step publishes a
  //              SeatCommand, and a row's number changes only when MibStatus says
  //              so. That keeps a limit or an interlock one decision made in one
  //              place, instead of two boards disagreeing about where the seat is.
  //
  // Memory: the screen and its rows exist only while it is up - see "Screens
  // built on demand".
  SettingsView settings_view_{{
      .nav = &nav_port_,
      .locked = &locked_,
      .subjects = &setting_subjects,
      .seat_values = seat_axis_value_,
      .screen_ensure = hmi::ui::bind<&OnDemandScreens::ensure_settings>(&on_demand_),
      .actuators_page = ACTUATORS_PAGE,
      .seat_step = hmi::ui::bind<&UiApp::seat_step>(this),
      .refuse = config_.cues->refused,
  }};
  // SkunkWorksScreen: a grid of one-press actions
  //
  // The menu's Skunk Works row. One tile per entry in actions_spec.h: the spec
  // gives each its title, subtitle and whether it needs the MCB; action_run_ below
  // says what it does. The stick walks the tiles as a grid; the stick button (or
  // a tap) runs the focused one.
  //
  // A button that needs the MCB greys out while mcb_ready() is false, so it says
  // nothing would happen before anyone presses it - and the local actions stay
  // reachable, which a full-screen banner would not allow.
  //
  // Like the SettingsScreen, the screen and its tiles exist only
  // while it is up - see "Screens built on demand".
  // What each Skunk Works tile does, in actions_spec.h order.
  std::array<Fn<void()>, ACTION_COUNT> action_run_{
      Fn<void()>{config_.cues->haptic_test},               // ACTION_HAPTIC_TEST
      Fn<void()>{config_.selftest->run},                   // ACTION_SELF_TEST
      hmi::ui::bind<&UiApp::action_seat_up>(this),         // ACTION_SEAT_UP
      hmi::ui::bind<&PerfOverlay::toggle>(&perf_overlay_), // ACTION_FPS_COUNTER
      Fn<void()>{config_.board->restart},                  // ACTION_RESTART_HMI
  };
  ActionsView actions_view_{{
      .nav = &nav_port_,
      .tiles = ACTION_TILES,
      .run = action_run_.data(),
      .count = ACTION_COUNT,
      .screen_ensure = hmi::ui::bind<&OnDemandScreens::ensure_actions>(&on_demand_),
      .bind_ready = hmi::ui::bind<&UiApp::bind_ready>(this),
      .ready_observer = action_ready_observer,
      .refuse = config_.cues->refused,
  }};
  // It owns the readings (the RTPS handler writes them through RtpsUiBridge), the stale and
  // rate subjects and the rows' group; the 250 ms poll keeps the stale and rate current.
  // DiagnosticsScreen: live readings from the MCB
  //
  // Opened from the DIAGNOSTICS settings row, left by pulling and holding. One
  // row per entry in RAMMP_DIAG_TABLE (messages/joystick_message.hpp): short label, label,
  // and up to three readings, each under its unit. The MCB publishes them all on
  // rammp::kMcbDiagnostics every rammp::kDiagPeriod; they land in
  // DiagnosticsView's readings (subjects, set under the LVGL lock by main's
  // RTPS handler, through RtpsUiBridge) and the rows observe them.
  //
  // DiagnosticsFreqLabel shows how fast they are arriving ("2.0 Hz - Live").
  // Once nothing has arrived for rammp::kDiagTimeout - or nothing ever has -
  // every row's text and the label turn red and blink: readings the MCB stopped
  // sending must not sit there looking current.
  //
  // The red comes through LV_STATE_USER_1. The labels' text colours are themed
  // for DEFAULT and FOCUSED, and a style on a higher state outranks both without
  // touching anything the theme manager re-applies on a theme change.
  //
  // Built on demand like the settings and actions screens (see "Screens built
  // on demand").
  DiagnosticsView diag_view_{{
      .nav = &nav_port_,
      .stats = config_.link->diag_stats,
      .timeout_us =
          std::chrono::duration_cast<std::chrono::microseconds>(rammp::kDiagTimeout).count(),
      .blink = &rtps_blink_subject,
      .screen_ensure = hmi::ui::bind<&OnDemandScreens::ensure_diagnostics>(&on_demand_),
      .row_focus_cb = SettingsView::focus_cb,
  }};
  // Moving between screens
  //
  // Spec V2 navigates with the burger menu: the key at the bottom of every
  // screen opens a full-screen overlay of destinations, and DRIVE in the band
  // goes home from anywhere. A screen change is just a screen change (see
  // docs/ui-architecture.md).
  //
  // Swaps go through the SquareLine helper rather than lv_screen_load, so they
  // take the same path as the boot screen's own transition and re-create the
  // target if it was ever destroyed.
  //
  // The burger menu, and moving around with the joystick
  //
  // Spec V2 gives every screen but BootScreen the same chrome: a TopBar across
  // the top, a DriveBand under it, a MenuKey in the bottom 162 px, and a
  // MenuOverlay that exactly covers the 720x921 body between them and starts
  // hidden. Tapping the key slides the overlay up over the body; the TopBar, the
  // band and the key itself do not move, which is what the spec means by
  // "sticky".
  //
  // The joystick reaches all of it. Every screen's focus group ends with that
  // screen's burger key: push down past the last row (or key, or log line) and
  // the key is focused; push up from it and you are back where you were. The
  // stick button presses whatever is focused -- a row, a button, the key. In the
  // open menu, up and down walk the rows, and left (or the key) closes it.
  //
  // The Drive screen is the exception, because there the stick drives: nothing
  // on it takes focus but its key, which has no focus ring, so a short press of
  // the stick button opens the menu and a hold still stops the chair
  // (drive_exit_gesture). The button selects on RELEASE, and only for a press
  // shorter than a hold's grace, so a hold never also clicks.
  //
  // What the cursor looks like: rows (menu, settings, diagnostics) go negative,
  // the spec's own "selected"; buttons get a ring in the theme's focus colour,
  // because on a button the negative already means pressed -- a drive mode
  // filled is the one selected, and the key filled white is "menu open".
  //
  // Every screen carries its own instance of all four pieces of chrome, so
  // NavView::attach_chrome is called once per screen -- by bind_chrome for the
  // screens ui_init builds, and by OnDemandScreens for the ones built on demand.
  NavView nav_view_{{
      .shared = &shared_,
      .menu_slide = setting_subjects.value(SETTINGS_PARAM_MENU_SLIDE),
      .menu_open = &nav_menu_open,
      .menu_on_arrival = &nav_menu_on_arrival,
      // The stick gate's writer is DriveUi's, at exactly NavView's GATE_TRIGGERS sites.
      .gate_update = hmi::ui::bind<&DriveUi::update_stick_gate>(&drive_ui_),
      .keep_overlay_fill = keep_overlay_fill,
      .ready_observer = action_ready_observer,
      .ready_data = this,
      .mcb_ready = hmi::ui::bind<&UiApp::mcb_ready>(this),
      .refuse_seat = hmi::ui::bind<&UiApp::refuse_seat>(this),
      .drive_row = hmi::ui::bind<&UiApp::nav_drive_row>(this),
      .drive_key = hmi::ui::bind<&UiApp::nav_drive_key>(this),
      .open_dest = hmi::ui::bind<&UiApp::open_dest>(this),
      .enter_screen = hmi::ui::bind<&UiApp::enter_screen>(this),
      .arrived = hmi::ui::bind<&UiApp::arrived>(this),
      .cursor_lost = hmi::ui::bind<&UiApp::cursor_lost>(this),
  }};
  OnDemandScreens on_demand_{{
      .bind_chrome = hmi::ui::bind<&UiApp::bind_on_demand_chrome>(this),
      .settings_bound = hmi::ui::bind<&UiApp::settings_bound>(this),
      .diagnostics_bound = hmi::ui::bind<&UiApp::diagnostics_bound>(this),
      .settings_left = hmi::ui::bind<&SettingsView::rows_clear>(&settings_view_),
      .actions_left = hmi::ui::bind<&ActionsView::clear>(&actions_view_),
      .diagnostics_left = hmi::ui::bind<&DiagnosticsView::rows_clear>(&diag_view_),
      .screen_loaded = screen_loaded_cb,
      .screen_loaded_data = &nav_view_,
      .strip_overdraw = strip_screen,
  }};
  DisplayFlip display_flip_{{
      .log = config_.flip_log,
      .present = config_.board->present,
      .refuse = config_.cues->refused,
  }};
  RtpsUiBridge rtps_bridge_{{
      .mib_state = &mib_state_,
      .drive_band = &drive_band_view_,
      .status_band = &status_band_view_,
      .refusal = &refusal_view_,
      .seat_apply_state = hmi::ui::bind<&UiApp::seat_apply_state>(this),
      .diag_values = diag_view_.values(),
  }};
  UiPoll ui_poll_{{
      .shared = &shared_,
      .theme = setting_subjects.value(SETTINGS_PARAM_THEME),
      .link_state = config_.link->state,
      .link_seen = config_.link->seen,
      .diag_poll = hmi::ui::bind<&DiagnosticsView::poll>(&diag_view_), // staleness, same tick
      .post_tick = hmi::ui::bind<&PostStage::tick>(&post_stage_),      // before the drive tick
      .drive_tick = config_.drive->tick, // and the wait for the MCB to drive
      .post_indicator = hmi::ui::bind<&UiApp::post_indicator>(this), // after it
      // The theme switch restored the redundant background fills: take them out again, and
      // save the setting (the first place firmware sees the result of any route to it).
      .theme_switched = hmi::ui::bind<&UiApp::theme_switched>(this),
  }};
};

} // namespace hmi::ui
