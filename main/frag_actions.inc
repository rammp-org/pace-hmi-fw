// split_main.py fragment of main.cpp (frag_actions: original lines 2782-2979); one TU, see main.cpp.
#include "hmi_ui/actions_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// SkunkWorksScreen: a grid of one-press actions
//
// The menu's Skunk Works row. One tile per entry in actions_spec.h: the spec
// gives each its title, subtitle and whether it needs the MCB; kActionRun below
// says what it does. The stick walks the tiles as a grid; the stick button (or
// a tap) runs the focused one.
//
// A button that needs the MCB greys out while mcb_ready() is false, so it says
// nothing would happen before anyone presses it - and the local actions stay
// reachable, which a full-screen banner would not allow.
//
// Like the SettingsScreen, the screen and its tiles exist only
// while it is up - see "Screens built on demand".
/////////////////////////////////////////////////////////////////////////////

// What each action does. LVGL task (a click).
static void action_haptic_test() {
  haptic_play(espp::Drv2605::Waveform::ALERT_1000MS, kHapticBuzzSlots);
}

static void action_self_test() { selftest_request(SelfTestTrigger::LOCAL, 0); }

// An RTPS command: the request a "+" press on the actuators page makes. The
// seat moves only if the MCB agrees, and a refusal flashes on that page.
static void action_seat_up() { seat_step(rammp::index_of(rammp::SeatAxis::ELEVATION), +1); }

static void action_restart_hmi() {
  static espp::Logger action_logger({.tag = "actions", .level = espp::Logger::Verbosity::INFO});
  action_logger.warn("restart requested from the actions screen");
  esp_restart();
}

// In actions_spec.h order.
static void (*const kActionRun[])() = {
    action_haptic_test, // ACTION_HAPTIC_TEST
    action_self_test,   // ACTION_SELF_TEST
    action_seat_up,     // ACTION_SEAT_UP
    fps_toggle,         // ACTION_FPS_COUNTER
    action_restart_hmi, // ACTION_RESTART_HMI
};
static_assert(std::size(kActionRun) == ACTION_COUNT,
              "every actions_spec.h entry needs its function here");

static constexpr hmi::ui::ActionsView::Tile kActionSpecs[] = {
#define ACTIONS_ROW(name_, title_, subtitle_, mcb_) {title_, subtitle_, (mcb_) != 0},
    ACTIONS_TABLE(ACTIONS_ROW)
#undef ACTIONS_ROW
};
static_assert(ACTION_COUNT <= hmi::ui::ActionsView::TILES_MAX, "more actions than tiles");

// A tile the MCB could not act on right now (hmi::ui::ActionsView::UNAVAILABLE);
// frag_nav greys its gated menu rows the same way.
static constexpr lv_state_t kActionUnavailable = hmi::ui::ActionsView::UNAVAILABLE;
static constexpr lv_style_selector_t kActionUnavailableStyle =
    hmi::ui::ActionsView::UNAVAILABLE_STYLE;

// Greys an MCB action while the MCB could not act on it. Bound to every
// subject mcb_ready() reads, so it follows the link and the state both.
static void action_ready_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_set_state(lv_observer_get_target_obj(observer), kActionUnavailable, !mcb_ready());
}

// The one instance; the tiles' actions stay out here, and so does the group
// (frag_nav reads it).
static constinit hmi::ui::ActionsView actions_view{{
    .nav = &ui_nav_port,
    .tiles = kActionSpecs,
    .run = kActionRun,
    .count = ACTION_COUNT,
    .screen_ensure = actions_screen_ensure,
    .bind_ready = bind_to_drive_blocked_cause,
    .ready_observer = action_ready_observer,
    .refuse = refusal_feedback,
}};
static void actions_open() { actions_view.open(); }
static void actions_clear() { actions_view.clear(); }
static void action_focus(int index) { actions_view.focus(index); }
