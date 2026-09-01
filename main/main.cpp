/**
 * @file m5stack_tab5_example.cpp
 * @brief M5Stack Tab5 BSP Example
 *
 * This example demonstrates the comprehensive functionality of the M5Stack Tab5
 * development board including display, touch, audio, camera, IMU, power management,
 * and communication interfaces.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <stdlib.h>
#include <vector>

#include "m5stack-tab5.hpp"

#include "da7280.hpp"
#include "drv2605.hpp"

#include "kalman_filter.hpp"
#include "madgwick_filter.hpp"

#include "ui.h"

#include "sample_ui_home.h"

#include "button.hpp"
#include "continuous_adc.hpp"
#include "joystick.hpp"
#include "keypad_input.hpp"
#include "oneshot_adc.hpp"

#include "rtps_comms.hpp"

#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_timer.h"

using namespace std::chrono_literals;

static std::vector<uint8_t> audio_bytes;

static std::recursive_mutex lvgl_mutex;

// ---------------------------------------------------------------------------
// Frame-rate instrumentation (temporary; remove once tuning is finished).
//
// Reports over serial rather than the LVGL perf overlay, so throughput can be
// measured without eyes on the panel. RENDER_START/RENDER_READY fire only when
// LVGL actually rasterizes, so these numbers are real frame cost -- REFR_*
// would tick even on an idle screen and read as a meaningless "infinite fps".
//
// kFpsStress forces a full-screen invalidation every LVGL cycle. Without it a
// static SquareLine screen invalidates nothing and renders nothing, which
// measures the redraw path not at all. With it we get sustained worst case.
// ---------------------------------------------------------------------------
static constexpr bool kFpsInstrument = false;
static constexpr bool kFpsStress = false;
static constexpr bool kFpsHideImages = false;
static std::atomic<uint32_t> fps_frames{0};
static std::atomic<uint64_t> fps_render_us_total{0};
static std::atomic<uint32_t> fps_render_us_max{0};
static int64_t fps_render_start_us = 0;

static void fps_render_start_cb(lv_event_t *) { fps_render_start_us = esp_timer_get_time(); }

static void fps_render_ready_cb(lv_event_t *) {
  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - fps_render_start_us);
  fps_frames.fetch_add(1, std::memory_order_relaxed);
  fps_render_us_total.fetch_add(us, std::memory_order_relaxed);
  uint32_t prev = fps_render_us_max.load(std::memory_order_relaxed);
  while (us > prev && !fps_render_us_max.compare_exchange_weak(prev, us)) {
  }
}

// Subjects (observer pattern) feeding the Settings-screen axis bars; static
// lifetime because the bound observers keep pointers to them
static lv_subject_t adc_x_subject;
static lv_subject_t adc_y_subject;
static lv_subject_t adc_twist_subject;
// GPIO48 test button: press count (bound to the ButtonCounter label) and
// current pressed state (drives the ButtonPanel background via an observer)
static lv_subject_t button_count_subject;
static lv_subject_t button_pressed_subject;

// Lock state. Declared up here with the other subjects because flex_key_cb
// below reads them; the rest of the lock/unlock machinery lives in its own
// section further down, after the haptic and audio helpers it needs.
//
// 1 = locked, 0 = unlocked. The single source of truth for both padlock images
// and both labels, so a future "lock on request" is one set_locked(true) call
// and every bound widget follows.
static lv_subject_t locked_subject;
// 1 = the FlexPanel pager is usable. Deliberately NOT just !locked_subject:
// it stays 0 through the pause between the unlock landing and the auto-advance
// to the Drive page, so nothing can steer the pager out from under that scroll.
// Drives the pager's SCROLLABLE flag (touch), the LockedPanel arrows' HIDDEN
// flag (taps), the sibling pages' HIDDEN flags, and the guard in flex_key_cb
// (joystick) — the four independent ways the pager can be moved.
static lv_subject_t paging_subject;

// Joystick -> LVGL keypad. Held at the *level* the stick is at: whichever
// LV_KEY_* direction it is deflected toward, or 0 when centered. Reporting a
// held key rather than a one-shot latch is what lets LVGL's own key repeat
// ([lv_indev.c] re-sends the key every long_press_repeat_time) walk the
// settings list while the stick stays pushed.
static std::atomic<uint32_t> joy_key{0};

// GPIO48 "select" stays edge-latched, unlike the directions: repeating ENTER
// would re-click the focused row every repeat period, which for the theme-toggle
// row means it strobes. Consumed by the indev read.
static std::atomic<bool> select_key{false};

// Focus manager for the rows inside ui_SettingsFlexPanel. Deliberately NOT
// attached to any indev: ui_FlexPanel stays the keypad's focused object so
// left/right keep paging, and this group is driven directly from flex_key_cb.
// LVGL still does the real work on focus change — LV_STATE_FOCUSED for the
// styling and, because the rows carry LV_OBJ_FLAG_SCROLL_ON_FOCUS, the
// scroll-into-view.
static lv_group_t *settings_group = nullptr;

// An indev can own exactly one group, so the joystick's group follows the
// active screen: joystick_group keeps ui_FlexPanel focused for paging on
// MainScreenFlex, seat_group walks the SeatAdjustmentFlexScreen buttons. The
// swap happens on LV_EVENT_SCREEN_LOADED, so it covers every route between the
// screens including ExitButton1's own SquareLine handler.
static lv_indev_t *joystick_indev = nullptr;
static lv_group_t *joystick_group = nullptr;
static lv_group_t *seat_group = nullptr;        // seat screen, function buttons page
static lv_group_t *seat_adjust_group = nullptr; // seat screen, adjustment page

// Which FlexPanel child is currently centered in the viewport. Derived from
// live coordinates rather than a stored index, so it stays correct no matter
// how the panel got scrolled — joystick, touch drag, or the on-screen arrows.
static lv_obj_t *flex_current_page() {
  if (!ui_FlexPanel) {
    return nullptr;
  }
  lv_area_t panel;
  lv_obj_get_coords(ui_FlexPanel, &panel);
  const int32_t cx = (panel.x1 + panel.x2) / 2;
  for (uint32_t i = 0; i < lv_obj_get_child_count(ui_FlexPanel); i++) {
    lv_obj_t *child = lv_obj_get_child(ui_FlexPanel, i);
    lv_area_t a;
    lv_obj_get_coords(child, &a);
    if (cx >= a.x1 && cx <= a.x2) {
      return child;
    }
  }
  return nullptr;
}

// All keypad input lands here, because ui_FlexPanel is what the indev focuses.
// Left/right page the flex panel; up/down walk the settings rows once that page
// is showing; enter clicks the focused row.
//
// Paging reuses flex_scroll_next/previous from ui_events.cpp — the same
// functions the on-screen arrows call — so the joystick and touch share one
// notion of the current page instead of each keeping their own.
static void flex_key_cb(lv_event_t *e) {
  lv_obj_t *panel = lv_event_get_target_obj(e);
  // the group is global, so an inactive screen would otherwise scroll unseen
  if (lv_obj_get_screen(panel) != lv_screen_active()) {
    return;
  }
  const uint32_t key = lv_event_get_key(e);

  // Paging is blocked until the driving mode is unlocked. Removing the pager's
  // SCROLLABLE flag stops a touch drag but not this: flex_scroll_step reaches
  // lv_obj_scroll_to_x, which is programmatic and has no SCROLLABLE check.
  if (key == LV_KEY_RIGHT || key == LV_KEY_LEFT) {
    if (!lv_subject_get_int(&paging_subject)) {
      return;
    }
    if (key == LV_KEY_RIGHT) {
      flex_scroll_next(nullptr);
    } else {
      flex_scroll_previous(nullptr);
    }
    return;
  }

  // the remaining keys only mean anything on the settings page
  if (flex_current_page() != ui_SettingsMenu || !settings_group) {
    return;
  }
  lv_obj_t *focused = lv_group_get_focused(settings_group);
  if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
    if (!focused) {
      // first nudge onto the page lands on the top row rather than wrapping
      lv_group_focus_obj(lv_obj_get_child(ui_SettingsFlexPanel, 0));
    } else if (key == LV_KEY_DOWN) {
      lv_group_focus_next(settings_group);
    } else {
      lv_group_focus_prev(settings_group);
    }
  } else if (key == LV_KEY_ENTER && focused) {
    // the group has no indev, so LVGL won't route the press itself; the row's
    // SquareLine handlers listen for LV_EVENT_CLICKED
    lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
  }
}

// HAPTIC TEST settings row -> kHapticBuzzDuration of vibration.
//
// Set by init_haptic() once the DRV2605 answers, and left null if it does not,
// so the button is inert rather than fatal on a board with no motor fitted.
// The DRV2605 plays the armed waveform on its own once START is written, so
// this is a single I2C write and the LVGL task is never held for the duration.
// espp::I2c and BasePeripheral are both mutex-protected, so sharing the bus
// with the IMU/touch/expander traffic from other tasks is safe.
static espp::Drv2605 *haptic = nullptr;

// One ALERT_1000MS effect per sequencer slot, so the buzz lasts one second per
// slot. Keep kHapticBuzzDuration in step with kHapticBuzzSlots: it is what the
// label countdown runs on, and a mismatch shows up as a label that clears
// before the motor stops (or lingers after it).
static constexpr uint8_t kHapticBuzzSlots = 2;
static_assert(kHapticBuzzSlots < 8, "need a sequencer slot left for END");
static constexpr auto kHapticBuzzDuration = kHapticBuzzSlots * 1000ms;

// The button label is firmware state, so it goes through a subject rather than
// an lv_label_set_text() from the press handler. The buffers are static for the
// same reason the subject is: the subject stores its copy in them, and both have
// to outlive the label bound to them.
static constexpr const char *kHapticIdleText = "HAPTIC TEST";
static constexpr const char *kHapticBusyText = "VIBRATING";
static char haptic_label_buf[16];
static char haptic_label_prev_buf[16];
static_assert(sizeof(haptic_label_buf) > sizeof("HAPTIC TEST"), "label buffer too small");
static lv_subject_t haptic_label_subject;

// Restores the idle label when the buzz ends. The DRV2605 does not report that
// it finished a sequence, so the label is timed rather than polled — which is
// why kHapticBuzzDuration has to stay in step with the armed slot count.
//
// Created paused and re-armed on each press, so pressing again mid-buzz just
// restarts the countdown instead of stacking a second timer that would clear
// the label while the motor is still running. Runs on the LVGL task, like the
// handler that arms it.
static lv_timer_t *haptic_label_timer = nullptr;

static void haptic_label_reset_cb(lv_timer_t *timer) {
  lv_timer_pause(timer);
  lv_subject_copy_string(&haptic_label_subject, kHapticIdleText);
}

// Arms `slots` copies of `w` (plus the END marker) and fires the sequencer.
// The DRV2605 has exactly one waveform sequence, so every caller re-arms before
// starting rather than relying on whatever the previous one left in the slots —
// otherwise the unlock's single click and the HAPTIC TEST buzz would overwrite
// each other. Returns false (having logged) if there is no motor or the I2C
// burst fails, so callers can leave their own UI alone.
//
// Cheap enough for the LVGL task: slots+2 register writes at 400 kHz, and the
// DRV2605 plays the sequence on its own once START is written.
static bool haptic_play(espp::Drv2605::Waveform w, uint8_t slots) {
  if (!haptic) {
    return false; // no motor on this board (init_haptic already logged why)
  }
  std::error_code ec;
  for (uint8_t slot = 0; slot < slots; slot++) {
    if (!haptic->set_waveform(slot, w, ec)) {
      fmt::print("DRV2605 set_waveform failed: {}\n", ec.message());
      return false;
    }
  }
  if (!haptic->set_waveform(slots, espp::Drv2605::Waveform::END, ec)) {
    fmt::print("DRV2605 set_waveform failed: {}\n", ec.message());
    return false;
  }
  if (!haptic->start(ec)) {
    fmt::print("DRV2605 start failed: {}\n", ec.message());
    return false;
  }
  return true;
}

static void haptic_test_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!haptic_play(espp::Drv2605::Waveform::ALERT_1000MS, kHapticBuzzSlots)) {
    return; // nothing is vibrating, so leave the label alone
  }
  lv_subject_copy_string(&haptic_label_subject, kHapticBusyText);
  lv_timer_reset(haptic_label_timer);
  lv_timer_resume(haptic_label_timer);
}

// FPS COUNTER settings row -> LVGL's built-in perf overlay (lv_sysmon), which
// already renders FPS/CPU on the sys layer above every screen. Wired here rather
// than through a SquareLine CALL FUNCTION event so it needs no round trip
// through the design tool; either way import_ui.ps1's mirror never touches
// main.cpp.
static void fps_toggle_cb(lv_event_t *e) {
  LV_UNUSED(e);
  static bool shown = false;
  shown = !shown;
  if (shown) {
    lv_sysmon_show_performance(lv_display_get_default());
  } else {
    lv_sysmon_hide_performance(lv_display_get_default());
  }
}

// Observer for the ButtonPanel background. There's no built-in binding for a
// style property, so the widget work happens here; the observer is bound to the
// object, so it dies with it.
static void gpio48_panel_observer(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  // "not pressed" is the *current theme's* background, not a hardcoded black —
  // SquareLine registered this property as themeable, so reading the theme keeps
  // the panel correct after a dark/day switch
  lv_obj_set_style_bg_color(panel,
                            lv_subject_get_int(subject)
                                ? lv_palette_main(LV_PALETTE_BLUE)
                                : lv_color_hex(ui_get_theme_value(_ui_theme_color_background)),
                            LV_PART_MAIN);
}

// Runs on the Button's interrupt task (not in ISR context). Only touches
// subjects, never widgets directly.
static void gpio48_button_callback(const espp::Interrupt::Event &event) {
  // lv_subject_set_int runs the observers synchronously on this task, and they
  // touch widgets, so this needs the LVGL lock
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);

  // the panel follows every edge, so contact bounce can't strand it blue: the
  // last edge always wins
  lv_subject_set_int(&button_pressed_subject, event.active);

  // doubles as the "select" gesture for the settings list — the joystick has no
  // natural press of its own
  if (event.active) {
    select_key.store(true);
  }

  // only the counter is debounced. espp's GPIO glitch filters are ns-scale and
  // do nothing against millisecond-scale mechanical bounce.
  static int64_t last_press_us = 0;
  const int64_t now = esp_timer_get_time();
  if (event.active && (now - last_press_us) > 30000) {
    last_press_us = now;
    lv_subject_set_int(&button_count_subject, lv_subject_get_int(&button_count_subject) + 1);
  }
}

static bool load_audio(size_t &out_size, size_t &out_sample_rate);
static void play_click(espp::M5StackTab5 &tab5);

// DRV2605 haptic driver: brings the motor on the PCB's JST connector up and
// selects its effect library. The waveform slots are armed per play, by
// haptic_play().
static void init_haptic(espp::Logger &logger, espp::I2c &i2c);

// DA7280 haptic driver bring-up test (raw register read, no driver class yet)
static void test_da7280(espp::Logger &logger, espp::I2c &i2c,
                        const std::vector<uint8_t> &found_addresses);

// DA7280 driver functional test: exercises vibrate()/stop(), the
// acceleration/rapid-stop/frequency-tracking toggles, and a fault-status
// register peek, all with the Da7280 driver class (DRO mode).
static void test_da7280_functional(espp::Logger &logger, espp::I2c &i2c);

/////////////////////////////////////////////////////////////////////////////
// Push-and-hold gestures
//
// Several places in the HMI ask the user to hold an input for kHoldMs before
// something happens, and they all show the same thing while they wait: a widget
// filling 0..kHoldMax, emptying the moment the hold breaks.
//
//   LockedPanel           joystick up      ui_UnlockArc   unlocks driving mode
//   DrivePanel            joystick up      ui_UnlockArc1  -> DriveScreen
//   SeatAdjustmentMenu    joystick up      ui_UnlockArc2  -> SeatAdjustmentFlexScreen
//   DriveScreen           joystick down    ui_ExitBarPull      -> MainScreenFlex
//   Seat / buttons page   joystick down    ui_ExitBarPull1     -> MainScreenFlex
//   Seat / adjust page    joystick left    ui_ExitBarPushLeft  -> buttons page
//
// "Pull" is the stick toward the user, i.e. LV_KEY_DOWN; "push left" is
// LV_KEY_LEFT. Each exit bar lives on the page it applies to, so the gestures
// are gated on which page is showing as well as on the screen.
//
// They differ only in the input, in when they apply, and in what completing
// does, so they share one HoldGesture driver rather than a copy of the
// animation bookkeeping each. Each gesture owns a subject; the arc/bar is bound
// to it, so the widget is still only ever reached through a binding (see
// CLAUDE.md) and the fill animation only ever writes the subject.
//
// The lock state itself (locked_subject, paging_subject) is declared with the
// other subjects at the top of the file, because flex_key_cb needs it earlier.
// While locked the pager is frozen and every other page is hidden, so the
// LockedPanel really is the only thing reachable.
/////////////////////////////////////////////////////////////////////////////

static constexpr uint32_t kHoldMs = 1000; // hold time to fill a gesture widget
// Dead time before an exit bar starts filling. Down on the seat buttons page and
// left on the adjustment page each do double duty — they step the focus grid as
// well as feeding a hold gesture — so without this every single navigation step
// ticks its bar up and snaps it back. Longer than a key-repeat step (250 ms), so
// stepping through a grid leaves the bars alone entirely.
//
// The arcs get none of this: nothing shares their input, and they should answer
// the moment the stick moves.
static constexpr uint32_t kBarGraceMs = 500;
static constexpr int32_t kHoldMax = 100; // arc/bar range (LVGL's default)
// Beat between the unlock landing and the pager moving on to the Drive page,
// so the READY TO DRIVE state is legible rather than a flash.
static constexpr uint32_t kUnlockAdvanceMs = 1000;
// Poll cadence for the inputs. Matched to the ADC task's 33 ms period: joy_key
// cannot change faster than that, so a shorter period would only burn LVGL task
// time re-reading the same value.
static constexpr uint32_t kHoldPollMs = 33;

// Whether each input is currently released, and so free to start a new hold.
//
// This is the "let go first" rule, and it is per *input* rather than per
// gesture on purpose: completing one gesture usually navigates straight to a
// screen where another gesture watches the same input, and the user's thumb is
// still where it was. Without this, unlocking would roll on into the Drive
// screen with no second hold, because joystick-up triggers both.
static bool joy_up_armed = true;
static bool joy_down_armed = true;
static bool joy_left_armed = true;

// progress and holding carry default member initializers rather than being
// spelled out at each definition below: the three gestures differ only in the
// four fields that follow, and -Wmissing-field-initializers is happy with a
// defaulted member but not with an omitted one.
struct HoldGesture {
  lv_subject_t progress{}; // 0..kHoldMax; the arc/bar is bound to this
  bool *armed;             // the input's "released since the last completion"
  bool (*is_held)();       // is the input held right now (sampled on LVGL task)
  bool (*applies)();       // is this gesture live on the current screen/page
  void (*completed)();     // what a full hold does
  uint32_t grace_ms = 0;   // dead time before the widget starts filling
  bool holding = false;    // edge detector for is_held && applies
};

static void hold_anim_exec_cb(void *var, int32_t value) {
  lv_subject_set_int(&static_cast<HoldGesture *>(var)->progress, value);
}

// Cancels any in-flight fill and empties the widget. The gesture doubles as the
// animation's `var`, so it is also the handle lv_anim_delete matches on.
static void hold_reset(HoldGesture *g) {
  lv_anim_delete(g, hold_anim_exec_cb);
  lv_subject_set_int(&g->progress, 0);
}

static void hold_anim_completed_cb(lv_anim_t *a) {
  auto *g = static_cast<HoldGesture *>(lv_anim_get_user_data(a));
  // Disarm before running the action: `completed` navigates, and the input is
  // still held at this instant, so whatever gesture watches that input on the
  // far side must not read the same unbroken hold as a fresh one.
  *g->armed = false;
  g->holding = false;
  // Deleting the animation from inside its own completed_cb (via hold_reset) is
  // safe and expected: lv_anim.c removes it from the list before calling us,
  // and anim_timer explicitly re-reads the list afterwards.
  hold_reset(g);
  // Confirmation feedback, the same for all three. Both calls are non-blocking
  // - one short I2C burst, and one xStreamBufferSend with a zero timeout - so
  // neither holds the LVGL task for the duration of the effect it starts.
  haptic_play(espp::Drv2605::Waveform::STRONG_CLICK, 1);
  play_click(espp::M5StackTab5::get());
  g->completed();
}

// Runs on the LVGL task rather than the ADC/button tasks that produce the
// inputs: everything here is LVGL state, and this way each animation is created
// and destroyed on the task that runs it.
static void hold_poll(HoldGesture *g) {
  const bool held = g->is_held();
  if (!held) {
    *g->armed = true;
  }
  const bool hold = held && *g->armed && g->applies();
  if (hold == g->holding) {
    return;
  }
  g->holding = hold;
  if (!hold) {
    hold_reset(g);
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g);
  lv_anim_set_user_data(&a, g);
  lv_anim_set_exec_cb(&a, hold_anim_exec_cb);
  lv_anim_set_values(&a, 0, kHoldMax);
  lv_anim_set_duration(&a, kHoldMs);
  // The delay holds act_time negative, so the exec callback does not run and the
  // widget stays empty until it elapses. Completion lands at grace + duration,
  // which is what a gesture with a grace period costs in total.
  lv_anim_set_delay(&a, g->grace_ms);
  lv_anim_set_completed_cb(&a, hold_anim_completed_cb);
  lv_anim_start(&a);
}

// The inputs. joy_key is the same Schmitt-triggered latch the flex pager uses,
// so the engage/release thresholds stay in one place, and it resolves a diagonal
// to a single direction — which is what keeps these three mutually exclusive.
static bool joy_up_held() { return joy_key.load() == LV_KEY_UP; }
static bool joy_down_held() { return joy_key.load() == LV_KEY_DOWN; }
static bool joy_left_held() { return joy_key.load() == LV_KEY_LEFT; }

/////////////////////////////////////////////////////////////////////////////
// Lock / unlock, and the LockedPanel gesture
/////////////////////////////////////////////////////////////////////////////

// The pair of strings one label swaps between, chosen by locked_subject. Passed
// as observer user_data so a single callback serves both labels. Non-const
// because lv_subject_add_observer_obj takes a void* user_data.
struct LockText {
  const char *locked;
  const char *unlocked;
};
static LockText kLockTitleText{"DRIVING MODE LOCKED", "READY TO DRIVE"};
static LockText kLockHintText{"Push & hold joystick to unlock", "Driving Mode Unlocked"};

// lv_label_bind_text exists, but it binds a *string* subject - which would mean
// a second copy of the lock state to keep in step with locked_subject. An
// observer bound to the object keeps one source of truth, and is torn down with
// the label like any other object-bound observer.
static void lock_text_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *text = static_cast<const LockText *>(lv_observer_get_user_data(observer));
  lv_label_set_text(lv_observer_get_target_obj(observer),
                    lv_subject_get_int(subject) ? text->locked : text->unlocked);
}

// One-shot, armed by the unlock and cancelled by a re-lock that beats it. Held
// so set_locked(true) can delete it: a re-lock inside the kUnlockAdvanceMs
// window would otherwise be immediately undone by the advance it left running.
static lv_timer_t *unlock_advance_timer = nullptr;

// Hands the pager over and moves on to the Drive page. Enabling paging first is
// what makes the scroll possible: the sibling pages are hidden while paging is
// off, and a hidden flex child lays out at zero width, so there would be
// nothing to scroll to. flex_scroll_step calls lv_obj_update_layout before it
// measures, so the un-hide is accounted for by the time the step is computed.
static void unlock_advance_cb(lv_timer_t *) {
  unlock_advance_timer = nullptr; // repeat_count 1: LVGL deletes it after this
  lv_subject_set_int(&paging_subject, 1);
  flex_scroll_next(nullptr);
}

// Locks or unlocks the HMI. Callers must hold lvgl_mutex: writing a subject runs
// its observers synchronously on the calling task and those touch widgets.
// Callers already on the LVGL task (timers, event callbacks) are covered by the
// lock lv_task_handler() is called under.
//
// Unlocking does not release the pager itself - it arms unlock_advance_cb,
// which does that as it scrolls on. Locking releases nothing and takes the
// pager back, so re-locking from any page returns to the LockedPanel rather
// than stranding the user on a page they can no longer scroll away from.
static void set_locked(bool locked) {
  if (unlock_advance_timer) {
    lv_timer_delete(unlock_advance_timer);
    unlock_advance_timer = nullptr;
  }
  if (locked) {
    // Scroll home before paging_subject hides the other pages: once they are
    // hidden the pager's content is one page wide, and lv_obj_scroll_by_bounded
    // would be clamping against a scroll range that no longer matches where the
    // viewport actually is.
    lv_obj_scroll_to_x(ui_FlexPanel, 0, LV_ANIM_OFF);
    lv_subject_set_int(&paging_subject, 0);
  } else {
    unlock_advance_timer = lv_timer_create(unlock_advance_cb, kUnlockAdvanceMs, nullptr);
    lv_timer_set_repeat_count(unlock_advance_timer, 1);
  }
  lv_subject_set_int(&locked_subject, locked ? 1 : 0);
}

// Is `page` the flex page the user is actually looking at? The screen check
// matters because the pager keeps its scroll position while the DriveScreen is
// loaded over the top of it.
static bool showing_flex_page(lv_obj_t *page) {
  return lv_screen_active() == ui_MainScreenFlex && flex_current_page() == page;
}

static HoldGesture unlock_gesture{
    .armed = &joy_up_armed,
    .is_held = joy_up_held,
    .applies =
        [] {
          return lv_subject_get_int(&locked_subject) != 0 && showing_flex_page(ui_LockedPanel);
        },
    .completed = [] { set_locked(false); },
};

/////////////////////////////////////////////////////////////////////////////
// Entering and leaving the DriveScreen and the SeatAdjustmentFlexScreen
//
// Both work the same way: hold up on the matching MainScreenFlex page to go in,
// hold the joystick button to come back. Neither exit re-locks — the pager is
// still sitting on the page you left from, so you land back where you were and
// going in again is one more hold.
//
// Screen swaps go through the SquareLine helper rather than lv_screen_load, so
// they take the same path as the boot screen's own transition and re-create the
// target if it was ever destroyed.
/////////////////////////////////////////////////////////////////////////////

static void screen_return_to_main() {
  _ui_screen_change(&ui_MainScreenFlex, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_MainScreenFlex_screen_init);
}

static HoldGesture drive_enter_gesture{
    .armed = &joy_up_armed,
    .is_held = joy_up_held,
    .applies = [] { return showing_flex_page(ui_DrivePanel); },
    .completed =
        [] {
          _ui_screen_change(&ui_DriveScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                            &ui_DriveScreen_screen_init);
        },
};

static HoldGesture drive_exit_gesture{
    .armed = &joy_down_armed,
    .is_held = joy_down_held,
    .applies = [] { return lv_screen_active() == ui_DriveScreen; },
    .completed = screen_return_to_main,
    .grace_ms = kBarGraceMs,
};

// Which page of the seat screen's own pager is showing: 0 = the function
// buttons, 1 = the adjustment panel. Each page carries its own exit bar, so a
// gesture that ignored this would fill a bar the user cannot see. Kept as a
// plain flag rather than derived from the scroll position, because the slide is
// animated and a derived value would flip halfway through it.
static int seat_page = 0;

// Defined with the rest of the seat navigation below, which is where the button
// grid it restores focus into is declared.
static void seat_show_buttons_page();

// Pull to leave the seat screen entirely. Only on the buttons page, where
// ui_ExitBarPull1 lives.
//
// Joystick-down also walks the button grid, and both happen: focus steps down
// and clamps at the bottom row while the bar fills. That is deliberate — the
// bar promises "pull and hold to exit" without qualification, and gating it on
// the bottom row would make a pull from anywhere else look broken.
static HoldGesture seat_exit_gesture{
    .armed = &joy_down_armed,
    .is_held = joy_down_held,
    .applies = [] { return lv_screen_active() == ui_SeatAdjustmentFlexScreen && seat_page == 0; },
    .completed = screen_return_to_main,
    .grace_ms = kBarGraceMs,
};

// Push left to go back a page. Nothing else is bound to left on the adjustment
// panel — the joystick has no group there at all — so this one has the input to
// itself.
static HoldGesture seat_back_gesture{
    .armed = &joy_left_armed,
    .is_held = joy_left_held,
    .applies = [] { return lv_screen_active() == ui_SeatAdjustmentFlexScreen && seat_page == 1; },
    .completed = seat_show_buttons_page,
    .grace_ms = kBarGraceMs,
};

static HoldGesture seat_enter_gesture{
    .armed = &joy_up_armed,
    .is_held = joy_up_held,
    .applies = [] { return showing_flex_page(ui_SeatAdjustmentMenu); },
    .completed =
        [] {
          // The seat screen has a pager of its own, and unlike the MainScreenFlex
          // one it starts fresh on every visit rather than resuming where it was
          // left. Done before the swap so the reset is never seen mid-scroll.
          lv_obj_scroll_to_x(ui_SeatAdjustmentScreenFlexPanel, 0, LV_ANIM_OFF);
          _ui_screen_change(&ui_SeatAdjustmentFlexScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                            &ui_SeatAdjustmentFlexScreen_screen_init);
        },
};

static void hold_poll_cb(lv_timer_t *) {
  hold_poll(&unlock_gesture);
  hold_poll(&drive_enter_gesture);
  hold_poll(&drive_exit_gesture);
  hold_poll(&seat_enter_gesture);
  hold_poll(&seat_exit_gesture);
  hold_poll(&seat_back_gesture);
}

/////////////////////////////////////////////////////////////////////////////
// SeatAdjustmentFlexScreen: joystick navigation of both pages
//
// Both pages lay their buttons out with LV_FLEX_FLOW_ROW_WRAP, so the joystick
// walks them as the grid the user sees rather than as the flat list LVGL's own
// focus_next would give. Each page has its own group and its own cursor:
//
//   buttons page                    adjustment page
//   [ Elevation ] [ Real Tilt ]     [     -     ] [     +     ]
//   [ FW Tilt   ] [ Side Tilt ]     [ 0deg ] [ 15deg ] [ 25deg ]
//   [ Static    ] [ Dynamic   ]
//
// Note the adjustment page's rows are different lengths, which is why a grid
// carries a per-row count rather than one column total.
//
// Selecting one of the four live function buttons names it on the adjustment
// page and slides the pager across; push-left-and-hold there
// (seat_back_gesture) comes back. The adjustment buttons are navigable and
// pressable but carry no handler of their own yet, so pressing one only shows
// LVGL's pressed state.
/////////////////////////////////////////////////////////////////////////////

static constexpr int kGridMaxRows = 3;
static constexpr int kGridMaxCols = 3;

// A page's focusable buttons in visual order, plus where the cursor is. Cells
// are filled in at wiring time: the ui_* globals are null until ui_init runs.
struct ButtonGrid {
  lv_obj_t *cell[kGridMaxRows][kGridMaxCols];
  int cols[kGridMaxRows]; // buttons in each row; rows may differ in length
  int rows;
  int row; // cursor
  int col;
};

static ButtonGrid seat_buttons_grid;
static ButtonGrid seat_adjust_grid;

// Which seat function the SeatAdjustmentPanel is showing. A subject rather than
// an lv_label_set_text from the click handler, so the label is reached the same
// way as every other piece of UI state (see CLAUDE.md). Buffers are static for
// the same reason the subject is — the subject stores its copy in them, and both
// have to outlive the label bound to them.
static char seat_function_buf[24];
static char seat_function_prev_buf[24];
static_assert(sizeof(seat_function_buf) > sizeof("Elevation"), "label buffer too small");
static lv_subject_t seat_function_subject;

// Arrow keys arrive here as LV_EVENT_KEY on the focused button: lv_indev only
// consumes NEXT/PREV/ENTER/ESC itself and passes everything else through
// lv_group_send_data. That is what lets a grid do its own 2D movement.
//
// user_data is the grid the button belongs to, so one callback serves both
// pages. On the adjustment page LV_KEY_LEFT also feeds seat_back_gesture: the
// cursor moves and the back bar fills at the same time, matching how down
// behaves on the buttons page.
static void grid_key_cb(lv_event_t *e) {
  auto *g = static_cast<ButtonGrid *>(lv_event_get_user_data(e));
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    g->row--;
    break;
  case LV_KEY_DOWN:
    g->row++;
    break;
  case LV_KEY_LEFT:
    g->col--;
    break;
  case LV_KEY_RIGHT:
    g->col++;
    break;
  default:
    return;
  }
  // Clamp rather than wrap. On a control surface you steer by feel, and running
  // off one edge to reappear at the opposite one is disorienting. The column is
  // re-clamped after a row change too, since rows can be different lengths.
  g->row = std::clamp(g->row, 0, g->rows - 1);
  g->col = std::clamp(g->col, 0, g->cols[g->row] - 1);
  lv_group_focus_obj(g->cell[g->row][g->col]);
}

// Puts the cursor on `button`, so joystick movement carries on from wherever a
// finger last landed instead of from where the joystick left off.
static void grid_sync_cursor(ButtonGrid *g, lv_obj_t *button) {
  for (int r = 0; r < g->rows; r++) {
    for (int c = 0; c < g->cols[r]; c++) {
      if (g->cell[r][c] == button) {
        g->row = r;
        g->col = c;
      }
    }
  }
  lv_group_focus_obj(button);
}

// The adjustment buttons have no behaviour yet, so this is all a press does:
// take focus. LVGL still shows the pressed state on its own.
static void grid_click_cb(lv_event_t *e) {
  grid_sync_cursor(static_cast<ButtonGrid *>(lv_event_get_user_data(e)),
                   lv_event_get_target_obj(e));
}

// Both a tap and the joystick button land here: with seat_group owning the
// indev, LVGL raises LV_EVENT_CLICKED on the focused button for an ENTER key
// exactly as it does for a touch release.
//
// user_data is the button's own label for the four live buttons and null for the
// inert pair, so those only take focus.
static void seat_click_cb(lv_event_t *e) {
  lv_obj_t *button = lv_event_get_target_obj(e);
  grid_sync_cursor(&seat_buttons_grid, button);

  auto *label = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  if (!label) {
    return; // Static and Dynamic
  }
  lv_subject_copy_string(&seat_function_subject, lv_label_get_text(label));

  // The pager has SCROLLABLE cleared, so lv_obj_scroll_to_view would bail out
  // early (it checks that flag) — but lv_obj_scroll_to_x is programmatic and
  // still moves it. Step math mirrors flex_scroll_step's.
  lv_obj_update_layout(ui_SeatAdjustmentScreenFlexPanel);
  const int32_t step = lv_obj_get_width(ui_SeatFunctionsButtonsPanel) +
                       lv_obj_get_style_pad_column(ui_SeatAdjustmentScreenFlexPanel, LV_PART_MAIN);
  lv_obj_scroll_to_x(ui_SeatAdjustmentScreenFlexPanel, step, LV_ANIM_ON);

  // Scrolling the buttons out of sight does not stop the keypad reaching them:
  // the group still holds a focused button, so the joystick would go on moving
  // and clicking widgets nobody can see. Touch is already safe (LVGL only
  // descends into children the touch point actually lands on, and these are now
  // outside the viewport), so it is only the group that needs detaching.
  //
  // The adjustment page has its own group, so the joystick moves across with the
  // pager rather than staying on buttons nobody can see any more.
  lv_indev_set_group(joystick_indev, seat_adjust_group);
  seat_adjust_grid.row = 0;
  seat_adjust_grid.col = 0;
  lv_group_focus_obj(seat_adjust_grid.cell[0][0]);
  seat_page = 1;
}

// Declared up with seat_back_gesture, which is what calls it.
static void seat_show_buttons_page() {
  lv_obj_scroll_to_x(ui_SeatAdjustmentScreenFlexPanel, 0, LV_ANIM_ON);
  seat_page = 0;
  lv_indev_set_group(joystick_indev, seat_group);
  // Back to the button the user selected rather than the top-left one: the
  // grid's cursor still holds it, and returning to where you were is less
  // jarring than being bounced to the corner.
  lv_group_focus_obj(seat_buttons_grid.cell[seat_buttons_grid.row][seat_buttons_grid.col]);
}

// Hands the joystick to whichever group belongs to the screen being shown.
static void screen_loaded_cb(lv_event_t *e) {
  if (lv_event_get_target_obj(e) == ui_SeatAdjustmentFlexScreen) {
    lv_indev_set_group(joystick_indev, seat_group);
    seat_page = 0;
    seat_buttons_grid.row = 0;
    seat_buttons_grid.col = 0;
    lv_group_focus_obj(seat_buttons_grid.cell[0][0]);
  } else {
    lv_indev_set_group(joystick_indev, joystick_group);
  }
}

// Direct-mode flush. LVGL renders into the DPI panel's own frame buffers, so
// px_map is already a frame buffer and esp_lcd_panel_draw_bitmap takes its
// no-copy branch: it writes the cache back, sets cur_fb_index, and returns,
// instead of copying 1.8 MB.
//
// That branch is not self-synchronising: it fires on_color_trans_done
// SYNCHRONOUSLY (esp_lcd_panel_dpi.c), which the BSP wires to
// lv_display_flush_ready, so disp->flushing clears before this function even
// returns. The actual buffer swap only lands when the DSI DMA re-arms at the
// end of the current frame. present_frame() blocks until it does. Blocking here
// is what gates the renderer: LVGL is single-threaded and cannot start the next
// refresh while flush_cb is on the stack, so by the time this returns the DMA
// is scanning the frame we just queued and the other buffer is free to draw
// into. Costs up to one panel refresh period (~17 ms) per frame, during which
// the LVGL task holds lvgl_mutex.
//
// Ignoring `area` and flushing full-screen IS correct here: call_flush_cb passes
// the buffer START (not an offset), and refr_sync_areas already copies the
// previous frame's invalid areas forward, so both buffers stay coherent.
static void direct_flush_cb(lv_display_t *disp, const lv_area_t * /*area*/, uint8_t *px_map) {
  if (!lv_display_flush_is_last(disp)) {
    lv_display_flush_ready(disp);
    return;
  }
  auto &tab5 = espp::M5StackTab5::get();
  // flush_ready itself arrives via the panel's on_color_trans_done; this call
  // additionally waits for the swap before letting LVGL render again.
  tab5.present_frame(px_map);
}

// DRV2605 haptic motor driver (the motor on the PCB's JST connector).
//
// The waveform is armed once here rather than per press: slots 0 and 1 each
// hold ALERT 1000MS (ROM effect 16, a one-second continuous buzz) so the two
// play back to back for kHapticBuzzDuration, and slot 2 holds END to terminate
// the sequence. Every later press is then just a START write and the chip runs
// the two seconds on its own — nothing blocks the LVGL task for the duration.
// The sequencer has 8 slots, and the last one used must hold the terminator.
//
// The bundled motor is an ERM; for an LRA swap MotorType::ERM -> LRA and
// Library::ERM_1 -> Library::LRA. ERM_1 is LIBRARY register value 2, i.e. TI's
// "Library B" (3 V rated ERM) — the enum names are offset by one from the
// datasheet's library numbering.
//
// The Tab5 internal bus runs at 1 MHz; the DRV2605 is fast-mode only (400 kHz
// max), so this device gets its own per-device clock rather than the bus rate.
static constexpr uint32_t kDrv2605SclSpeedHz = 400 * 1000;

static void init_haptic(espp::Logger &logger, espp::I2c &i2c) {
  std::error_code ec;
  // Function-local statics: both outlive this call, because haptic_test_cb
  // reaches the driver (and the driver the device) for the rest of the run.
  static auto drv2605_i2c_device = i2c.add_device<uint8_t>(
      {
          .device_address = espp::Drv2605::DEFAULT_ADDRESS,
          .timeout_ms = static_cast<int>(i2c.config().timeout_ms),
          .scl_speed_hz = kDrv2605SclSpeedHz,
          .log_level = espp::Logger::Verbosity::WARN,
      },
      ec);
  if (!drv2605_i2c_device) {
    logger.error("Could not create DRV2605 I2C device: {}", ec.message());
    return;
  }

  static espp::Drv2605 drv2605({
      .device_address = espp::Drv2605::DEFAULT_ADDRESS,
      .write = espp::make_i2c_addressed_write(drv2605_i2c_device),
      .read_register = espp::make_i2c_addressed_read_register(drv2605_i2c_device),
      .motor_type = espp::Drv2605::MotorType::ERM,
      .auto_init = false,
      .log_level = espp::Logger::Verbosity::WARN,
  });

  if (!drv2605.initialize(ec)) {
    logger.error("DRV2605 init failed at {:#02x} ({}) — check wiring",
                 espp::Drv2605::DEFAULT_ADDRESS, ec.message());
    return;
  }
  if (!drv2605.select_library(espp::Drv2605::Library::ERM_1, ec)) {
    logger.error("DRV2605 select_library failed: {}", ec.message());
    return;
  }
  // No waveform is armed here: the sequencer slots are shared between the
  // HAPTIC TEST buzz and the unlock click, so haptic_play() writes them on
  // every play instead.

  // Only published once the library is selected, so a half-configured driver
  // can never be reached from the button.
  haptic = &drv2605;
  logger.info("DRV2605 ready: HAPTIC TEST plays a {} ms buzz", kHapticBuzzDuration.count());
}

// DA7280 haptic driver bring-up test: read-only register probe, no driver
// class yet. Datasheet 7-bit slave address is 0x4A (its 0x94/0x95 values are
// the pre-shifted 8-bit write/read forms) — espp::I2c shifts internally, so
// the raw 7-bit address is passed here.
static constexpr uint8_t kDa7280Address = 0x4A;
static constexpr uint8_t kDa7280RegChipRev = 0x00;

static void test_da7280(espp::Logger &logger, espp::I2c &i2c,
                        const std::vector<uint8_t> &found_addresses) {
  bool found = std::find(found_addresses.begin(), found_addresses.end(), kDa7280Address) !=
               found_addresses.end();
  if (!found) {
    logger.warn("DA7280 not found at {:#02x} — check wiring", kDa7280Address);
    return;
  }
  uint8_t chip_rev = 0;
  if (!i2c.read_at_register(kDa7280Address, kDa7280RegChipRev, &chip_rev, 1)) {
    logger.error("DA7280 found at {:#02x} but CHIP_REV read failed", kDa7280Address);
    return;
  }
  logger.info("DA7280 CHIP_REV (reg {:#02x}) = {:#02x}", kDa7280RegChipRev, chip_rev);
}

// DA7280 driver functional test (SparkFun Qwiic Haptic Motor's bundled LRA
// electrical parameters). Exercises the driver's public API end to end:
//   1. Baseline smoke test (init + vibrate + stop)
//   2. Magnitude sweep across the DRO drive range
//   3. Acceleration mode: disabled, at high magnitudes (the "enabled at
//      high magnitude" case is invalid and is proven so at compile time
//      via static_assert, not exercised at runtime)
//   4. Rapid stop: enabled (snap stop) vs disabled (coast)
//   5. Frequency tracking: enabled vs disabled, with a fault-status peek
//   6. Custom Waveform Operation: SINE/SQUARE/TRIANGLE presets, then a raw
//      CUSTOM coefficient triple, then leaving the mode again
//
// Timing is deliberately generous: kDa7280DriveDuration is how long each
// vibration runs (long enough to unambiguously feel), kDa7280PauseDuration
// is silence between changes (long enough that "did it stop?" isn't a
// guess), and kDa7280StepPause gives time to read a step's header log
// before the first action in that step fires. These must be at namespace
// scope (not locals) since they're used as lambda default-argument values
// in test_da7280_functional() below.
static constexpr auto kDa7280DriveDuration = 750ms;
static constexpr auto kDa7280PauseDuration = 1500ms;
static constexpr auto kDa7280StepPause = 750ms;
static constexpr auto kDa7280FreqTrackDriveDuration = 1500ms;
static constexpr int kDa7280TotalTests = 15;

static void test_da7280_functional(espp::Logger &logger, espp::I2c &i2c) {
  std::error_code ec;
  auto da7280_i2c_device = i2c.add_device<uint8_t>(
      {
          .device_address = espp::Da7280::DEFAULT_ADDRESS,
          .timeout_ms = static_cast<int>(i2c.config().timeout_ms),
          .scl_speed_hz = i2c.config().clk_speed,
          .log_level = espp::Logger::Verbosity::WARN,
      },
      ec);
  if (!da7280_i2c_device) {
    logger.error("Could not create DA7280 I2C device: {}", ec.message());
    return;
  }

  espp::Da7280 da7280({
      .device_address = espp::Da7280::DEFAULT_ADDRESS,
      .motor_type = espp::Da7280::MotorType::LRA,
      .nominal_voltage = 2.106f,
      .abs_max_voltage = 2.26f,
      .max_current_ma = 165.4f,
      .impedance_ohms = 13.8f,
      .lra_freq_hz = 170.0f,
      .probe = espp::make_i2c_addressed_probe(da7280_i2c_device),
      .write = espp::make_i2c_addressed_write(da7280_i2c_device),
      .read_register = espp::make_i2c_addressed_read_register(da7280_i2c_device),
      .log_level = espp::Logger::Verbosity::INFO,
  });

  if (!da7280.initialize(ec)) {
    logger.error("DA7280 functional test: initialization failed: {}", ec.message());
    return;
  }

  // Every individual vibration is numbered [DA7280 TEST N/15] so a specific
  // failure (felt nothing, or an error) can be pinned to one line in the log
  // instead of just "somewhere in step 3". Status (IRQ_EVENT1/IRQ_STATUS1)
  // is checked after every test, not just the frequency-tracking ones, so
  // any future zero-output mystery has fault data attached instead of
  // needing a re-run to diagnose.
  // print_step_header draws a heavy divider (step boundary); run_test draws
  // a light divider (test boundary) before its own header line. Both print
  // raw via fmt::print (blank lines + dividers, no logger tag/timestamp
  // prefix) so the boundary is visually obvious when scrolling a busy log.
  auto print_step_header = [&](const char *text) {
    fmt::print("\n\n========================================\n");
    logger.info(text);
  };

  int test_index = 0;
  auto run_test = [&](const char *label, uint8_t magnitude,
                      std::chrono::milliseconds duration = kDa7280DriveDuration) {
    ++test_index;
    fmt::print("\n----------------------------------------\n");
    logger.info("[DA7280 TEST {:02}/{}] {}: vibrate({})", test_index, kDa7280TotalTests, label,
                magnitude);
    da7280.vibrate(magnitude, ec);
    if (ec) {
      logger.error("[DA7280 TEST {:02}/{}] vibrate failed: {}", test_index, kDa7280TotalTests,
                   ec.message());
    }
    std::this_thread::sleep_for(duration);
    da7280.check_faults(ec);
    da7280.stop(ec);
    std::this_thread::sleep_for(kDa7280PauseDuration);
  };

  // 1. Baseline smoke test
  print_step_header("[DA7280 1/6] Baseline");
  std::this_thread::sleep_for(kDa7280StepPause);
  run_test("Baseline", 25);

  // 2. Magnitude sweep (0-127 only: acceleration is enabled by default at
  // this point, and above 127 with acceleration enabled produces no output
  // at all rather than a clamped value - see vibrate()'s doc comment. The
  // 128-255 range is exercised deliberately in step 3, with acceleration
  // disabled.)
  print_step_header("[DA7280 2/6] Magnitude sweep");
  std::this_thread::sleep_for(kDa7280StepPause);
  for (uint8_t magnitude : {32, 64, 96, 127}) {
    run_test("Magnitude sweep", magnitude);
  }

  // 3. Acceleration mode: disabled, at 200/255 (should drive at full
  // strength, current/voltage-limited near 255 - see check_faults()'s
  // WARNING decode). The "enabled + >127" combination is NOT exercised
  // here at runtime: it's proven invalid at COMPILE time instead, by the
  // static_asserts right below using Da7280::is_valid_dro_magnitude(). If
  // magnitude 200/255 were paired with AccelerationEnabled=true in one of
  // those asserts, the build itself would fail with a clear message rather
  // than needing a hardware run + log read to discover the mistake.
  static_assert(espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/false, 200),
                "sanity check: 200 is valid with acceleration disabled");
  static_assert(espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/false, 255),
                "sanity check: 255 is valid with acceleration disabled");
  static_assert(!espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/true, 200),
                "sanity check: 200 must be rejected with acceleration enabled");
  static_assert(!espp::Da7280::is_valid_dro_magnitude(/*acceleration_enabled=*/true, 255),
                "sanity check: 255 must be rejected with acceleration enabled");
  print_step_header("[DA7280 3/6] Acceleration mode: disabled, at 200/255");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_acceleration_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  for (uint8_t magnitude : {200, 255}) {
    run_test("Accel disabled", magnitude);
  }
  da7280.set_acceleration_enabled(true, ec); // restore default

  // 4. Rapid stop: enabled should snap to a stop, disabled should coast.
  // Magnitude 100 (not 150): acceleration is still enabled at this point
  // (left that way at the end of step 3), so anything above 127 would
  // produce no output here too.
  print_step_header("[DA7280 4/6] Rapid stop: enabled (snap) then disabled (coast)");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_rapid_stop_enabled(true, ec);
  run_test("Rapid stop enabled", 100);

  da7280.set_rapid_stop_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  run_test("Rapid stop disabled", 100);
  da7280.set_rapid_stop_enabled(true, ec); // restore default

  // 5. Frequency tracking: enabled then disabled. A restrained/blocked
  // motor can trip a spurious fault with tracking enabled; run_test's
  // fault-status log is how to actually see that instead of guessing from
  // silence.
  print_step_header(
      "[DA7280 5/6] Frequency tracking: enabled then disabled, checking fault status");
  std::this_thread::sleep_for(kDa7280StepPause);
  da7280.set_frequency_tracking_enabled(true, ec);
  run_test("Freq tracking enabled", 80, kDa7280FreqTrackDriveDuration);

  da7280.set_frequency_tracking_enabled(false, ec);
  std::this_thread::sleep_for(kDa7280PauseDuration);
  run_test("Freq tracking disabled", 80, kDa7280FreqTrackDriveDuration);
  da7280.set_frequency_tracking_enabled(true, ec); // restore default

  // 6. Custom Waveform Operation: SINE/SQUARE/TRIANGLE presets, then a raw
  // CUSTOM coefficient triple. enable_custom_waveform(true, ...) disables
  // acceleration/rapid-stop/amp-PID/frequency-tracking for us (required by
  // the chip for this mode) - we restore them ourselves at the end since
  // the driver won't do that automatically, see enable_custom_waveform()'s
  // doc comment. Magnitude 150 is safe here regardless: acceleration is
  // off for the whole step.
  print_step_header("[DA7280 6/6] Custom Waveform Operation: SINE, SQUARE, TRIANGLE, CUSTOM");
  std::this_thread::sleep_for(kDa7280StepPause);
  if (!da7280.enable_custom_waveform(true, ec)) {
    logger.error("[DA7280 6/6] enable_custom_waveform(true) failed: {}", ec.message());
  }

  da7280.set_wave_shape(espp::Da7280::WaveShape::SINE, ec);
  run_test("Custom waveform SINE", 150);

  da7280.set_wave_shape(espp::Da7280::WaveShape::SQUARE, ec);
  run_test("Custom waveform SQUARE", 150);

  da7280.set_wave_shape(espp::Da7280::WaveShape::TRIANGLE, ec);
  run_test("Custom waveform TRIANGLE", 150);

  // Arbitrary asymmetric shape: stays low, then jumps sharply near the end
  // of the quarter-period - clearly distinct from the three presets above.
  da7280.set_wave_coefficients(0x10, 0x30, 0xF8, ec);
  run_test("Custom waveform CUSTOM", 150);

  if (!da7280.enable_custom_waveform(false, ec)) {
    logger.error("[DA7280 6/6] enable_custom_waveform(false) failed: {}", ec.message());
  }
  // Restore the closed-loop defaults enable_custom_waveform(true) turned off
  da7280.set_acceleration_enabled(true, ec);
  da7280.set_rapid_stop_enabled(true, ec);
  da7280.set_frequency_tracking_enabled(true, ec);
  da7280.set_amp_pid_enabled(false, ec);

  logger.info("DA7280 functional test complete");
}

extern "C" void app_main(void) {
  espp::Logger logger({.tag = "M5Stack Tab5 Example", .level = espp::Logger::Verbosity::INFO});
  logger.info("Starting example!");

  //! [m5stack tab5 example]
  espp::M5StackTab5 &tab5 = espp::M5StackTab5::get();
  logger.info("Running on M5Stack Tab5");

  // first let's get the internal i2c bus and probe for all devices on the bus
  logger.info("Probing internal I2C bus...");
  auto &i2c = tab5.internal_i2c();
  std::vector<uint8_t> found_addresses;
  for (uint8_t address = 1; address < 128; address++) {
    if (i2c.probe_device(address)) {
      found_addresses.push_back(address);
    }
  }
  logger.info("Found devices at addresses: {::#02x}", found_addresses);

  // DRV2605 haptic motor driver, armed with the 1 s waveform the HAPTIC TEST
  // settings row plays (the button is wired up after ui_init() below).
  init_haptic(logger, i2c);

  // DA7280 haptic driver bring-up test (raw register read, no driver yet)
  test_da7280(logger, i2c, found_addresses);

  // DA7280 driver functional test (Da7280 driver class, DRO mode)
  test_da7280_functional(logger, i2c);

  // Initialize the IO expanders
  logger.info("Initializing IO expanders...");
  if (!tab5.initialize_io_expanders()) {
    logger.error("Failed to initialize IO expanders!");
    return;
  }

  // EXT5V_EN (0x43 P2) is asserted by the expander's default output mask; read it
  // back to confirm the M5-Bus / 2.54-10P / HY2.0-4P 5V rail is live
  auto ext_5v = tab5.get_io_expander_output(0x43, 2);
  logger.info("EXT_5V_EN: {}", ext_5v ? (*ext_5v ? "enabled" : "DISABLED") : "read failed");

  logger.info("Initializing lcd...");
  // initialize the LCD
  if (!tab5.initialize_lcd()) {
    logger.error("Failed to initialize LCD!");
    return;
  }

  // Query LCD controller
  auto controller_type = tab5.get_display_controller();
  const char *controller_name = tab5.get_display_controller_name();
  logger.info(controller_name);

  // initialize the display with full-screen draw buffers (the vendored BSP in
  // components/m5stack-tab5 allocates them in PSRAM)
  logger.info("Initializing display...");
  auto pixel_buffer_size = tab5.display_width() * tab5.display_height();
  if (!tab5.initialize_display(pixel_buffer_size)) {
    logger.error("Failed to initialize display!");
    return;
  }

  // Switch LVGL to DIRECT render mode over the panel's own frame buffers. The
  // BSP already handed those buffers to lv_display_set_buffers, but espp's
  // Display hardcodes RENDER_MODE_PARTIAL; DIRECT is what lets LVGL treat them
  // as real frame buffers (tracking dirty areas across both) so a flush is a
  // vsync-gated flip (see direct_flush_cb) rather than a copy. DIRECT requires
  // rotation 0, which is what this panel runs at.
  {
    void *fb0 = nullptr;
    void *fb1 = nullptr;
    esp_err_t fb_err = esp_lcd_dpi_panel_get_frame_buffer(tab5.lcd_panel_handle(), 2, &fb0, &fb1);
    const size_t fb_bytes = tab5.display_width() * tab5.display_height() * sizeof(uint16_t);
    // DIRECT mode renders into screen-sized frame buffers and direct_flush_cb
    // assumes the panel's native orientation, so it is incompatible with LVGL
    // software rotation. Assert here rather than depend on the
    // lv_display_set_rotation(ROTATION_0) call much further down.
    assert(lv_display_get_rotation(lv_display_get_default()) == LV_DISPLAY_ROTATION_0 &&
           "DIRECT render mode requires rotation 0");
    if (fb_err == ESP_OK && fb0 && fb1) {
      lv_display_set_buffers(lv_display_get_default(), fb0, fb1, fb_bytes,
                             LV_DISPLAY_RENDER_MODE_DIRECT);
      lv_display_set_flush_cb(lv_display_get_default(), direct_flush_cb);
      logger.info("LVGL rendering directly into the DSI frame buffers (DIRECT mode)");
    } else {
      logger.error("Could not get DPI frame buffers ({}); leaving BSP flush in place",
                   esp_err_to_name(fb_err));
    }
  }

  // run the LVGL refresh timer at 60 fps — the espp lv_conf.h compiles in a
  // 33 ms (30 fps) default period; the lv_task loop below already calls
  // lv_task_handler every 16 ms so it can keep up
  lv_timer_set_period(lv_display_get_refr_timer(lv_display_get_default()), 16);

  if (kFpsInstrument) {
    lv_display_add_event_cb(lv_display_get_default(), fps_render_start_cb, LV_EVENT_RENDER_START,
                            nullptr);
    lv_display_add_event_cb(lv_display_get_default(), fps_render_ready_cb, LV_EVENT_RENDER_READY,
                            nullptr);
    logger.info("FPS instrumentation enabled (stress={})", kFpsStress);
  }

  auto touch_callback = [&](const auto &touch) {
    // NOTE: since we're directly using the touchpad data, and not using the
    // TouchpadInput + LVGL, we'll need to ensure the touchpad data is
    // converted into proper screen coordinates instead of simply using the
    // raw values.
    static auto previous_touchpad_data = tab5.touchpad_convert(touch);
    auto touchpad_data = tab5.touchpad_convert(touch);
    if (touchpad_data != previous_touchpad_data) {
      logger.debug("Touch: {}", touchpad_data);
      previous_touchpad_data = touchpad_data;

      // play a click sound only on the press transition (release + re-touch
      // required before it plays again)
      static bool was_pressed = false;
      bool is_pressed = touchpad_data.num_touch_points > 0;
      if (is_pressed && !was_pressed) {
        play_click(tab5);
      }
      was_pressed = is_pressed;
    }
  };

  // make the filter we'll use for the IMU to compute the orientation
  static constexpr float angle_noise = 0.001f;
  static constexpr float rate_noise = 0.1f;
  static espp::KalmanFilter<2> kf;
  kf.set_process_noise(rate_noise);
  kf.set_measurement_noise(angle_noise);
  static constexpr float beta = 0.5f; // higher = more accelerometer, lower = more gyro
  static espp::MadgwickFilter f(beta);

  using Imu = espp::M5StackTab5::Imu;
  auto kalman_filter_fn = [](float dt, const Imu::Value &accel,
                             const Imu::Value &gyro) -> Imu::Value {
    // Apply Kalman filter
    float accelRoll = atan2(accel.y, accel.z);
    float accelPitch = atan2(-accel.x, sqrt(accel.y * accel.y + accel.z * accel.z));
    kf.predict({espp::deg_to_rad(gyro.x), espp::deg_to_rad(gyro.y)}, dt);
    kf.update({accelRoll, accelPitch});
    float roll, pitch;
    std::tie(roll, pitch) = kf.get_state();
    // return the computed orientation
    Imu::Value orientation{};
    orientation.roll = roll;
    orientation.pitch = pitch;
    orientation.yaw = 0.0f;
    return orientation;
  };

  auto madgwick_filter_fn = [](float dt, const Imu::Value &accel,
                               const Imu::Value &gyro) -> Imu::Value {
    // Apply Madgwick filter
    f.update(dt, accel.x, accel.y, accel.z, espp::deg_to_rad(gyro.x), espp::deg_to_rad(gyro.y),
             espp::deg_to_rad(gyro.z));
    float roll, pitch, yaw;
    f.get_euler(roll, pitch, yaw);
    // return the computed orientation
    Imu::Value orientation{};
    orientation.roll = espp::deg_to_rad(roll);
    orientation.pitch = espp::deg_to_rad(pitch);
    orientation.yaw = espp::deg_to_rad(yaw);
    return orientation;
  };

  logger.info("Initializing IMU...");
  // initialize the IMU
  if (!tab5.initialize_imu(kalman_filter_fn)) {
    logger.error("Failed to initialize IMU!");
    return;
  }

  // initialize the uSD card
  using SdCardConfig = espp::M5StackTab5::SdCardConfig;
  SdCardConfig sdcard_config{};
  if (!tab5.initialize_sdcard(sdcard_config)) {
    logger.warn("Failed to initialize uSD card, there may not be a uSD card inserted!");
  } else {
    uint32_t size_mb = 0;
    uint32_t free_mb = 0;
    if (tab5.get_sd_card_info(&size_mb, &free_mb)) {
      logger.info("uSD card size: {} MB, free space: {} MB", size_mb, free_mb);
    } else {
      logger.warn("Failed to get uSD card info");
    }
  }

  logger.info("Initializing RTC...");
  // initialize the RTC
  if (!tab5.initialize_rtc()) {
    logger.error("Failed to initialize RTC!");
    return;
  }

  auto current_time = std::tm{};
  if (!tab5.get_rtc_time(current_time)) {
    logger.error("Failed to get RTC time");
    return;
  }

  // only set the time if the year is before 2024
  if (current_time.tm_year < 124) {
    // set the RTC time to a known value (2024-01-15 14:30:45)
    // Set time using std::tm
    std::tm time = {};
    time.tm_year = 124; // 2024 - 1900
    time.tm_mon = 0;    // January (0-based)
    time.tm_mday = 15;  // 15th
    time.tm_hour = 14;  // 2 PM
    time.tm_min = 30;
    time.tm_sec = 45;
    time.tm_wday = 1; // Monday
    if (!tab5.set_rtc_time(time)) {
      logger.error("Failed to set RTC time");
      return;
    }
  } else {
    logger.info("RTC time is already set to a valid value {:%Y-%m-%d %H:%M:%S}", current_time);
  }

  logger.info("Initializing battery management...");
  // initialize battery monitoring
  if (!tab5.initialize_battery_monitoring()) {
    logger.error("Failed to initialize battery monitoring!");
    return;
  }

  // enable charging
  tab5.set_charging_enabled(true);

  logger.info("Initializing sound...");
  // initialize the sound
  if (!tab5.initialize_audio()) {
    logger.error("Failed to initialize sound!");
    return;
  }

  // Brightness control with button
  logger.info("Initializing button...");
  auto button_callback = [&](const auto &state) {
    logger.info("Button state: {}", state.active);
    if (state.active) {
      // Cycle through brightness levels: 25%, 50%, 75%, 100%
      static int brightness_level = 0;
      float brightness_values[] = {0.25f, 0.5f, 0.75f, 1.0f};
      brightness_level = (brightness_level + 1) % 4;
      float new_brightness = brightness_values[brightness_level];
      tab5.brightness(new_brightness);
      logger.info("Set brightness to {:.0f}%", new_brightness * 100);
    }
  };
  if (!tab5.initialize_button(button_callback)) {
    logger.warn("Failed to initialize button");
  }

  logger.info("Setting up LVGL UI...");
  // set the background color to white
  lv_obj_t *bg = lv_obj_create(lv_screen_active());
  lv_obj_set_size(bg, tab5.display_width(), tab5.display_height());
  lv_obj_set_style_bg_color(bg, lv_color_make(255, 255, 255), 0);

  // add text in the center of the screen
  lv_obj_t *label = lv_label_create(lv_screen_active());
  static std::string label_text = "\n\n\n\nTouch the screen!";
  lv_label_set_text(label, label_text.c_str());
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);

  // Create style for line 0 (blue line, used for kalman filter)
  static lv_style_t style_line0;
  lv_style_init(&style_line0);
  lv_style_set_line_width(&style_line0, 8);
  lv_style_set_line_color(&style_line0, lv_palette_main(LV_PALETTE_BLUE));
  lv_style_set_line_rounded(&style_line0, true);

  // make a line for showing the direction of "down"
  lv_obj_t *line0 = lv_line_create(lv_screen_active());
  static lv_point_precise_t line_points0[] = {{0, 0},
                                              {tab5.display_width(), tab5.display_height()}};
  lv_line_set_points(line0, line_points0, 2);
  lv_obj_add_style(line0, &style_line0, 0);

  // Create style for line 1 (red line, used for madgwick filter)
  static lv_style_t style_line1;
  lv_style_init(&style_line1);
  lv_style_set_line_width(&style_line1, 8);
  lv_style_set_line_color(&style_line1, lv_palette_main(LV_PALETTE_RED));
  lv_style_set_line_rounded(&style_line1, true);

  // make a line for showing the direction of "down"
  lv_obj_t *line1 = lv_line_create(lv_screen_active());
  static lv_point_precise_t line_points1[] = {{0, 0},
                                              {tab5.display_width(), tab5.display_height()}};
  lv_line_set_points(line1, line_points1, 2);
  lv_obj_add_style(line1, &style_line1, 0);

  static auto rotate_display = [&]() {
    std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
    static auto rotation = LV_DISPLAY_ROTATION_0;
    rotation = static_cast<lv_display_rotation_t>((static_cast<int>(rotation) + 1) % 4);
    lv_display_t *disp = lv_display_get_default();
    lv_disp_set_rotation(disp, rotation);
    // update the size of the screen
    lv_obj_set_size(bg, tab5.rotated_display_width(), tab5.rotated_display_height());
  };

  // add a button in the top left which (when pressed) will rotate the display
  // through 0, 90, 180, 270 degrees
  lv_obj_t *btn = lv_btn_create(lv_screen_active());
  lv_obj_set_size(btn, 50, 50);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *label_btn = lv_label_create(btn);
  lv_label_set_text(label_btn, LV_SYMBOL_REFRESH);
  // center the text in the button
  lv_obj_align(label_btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(
      btn, [](auto event) { rotate_display(); }, LV_EVENT_PRESSED, nullptr);

  // disable scrolling on the screen (so that it doesn't behave weirdly when
  // rotated and drawing with your finger)
  lv_obj_set_scrollbar_mode(lv_screen_active(), LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);

  // Load the SquareLine Studio UI. This creates ui_Screen1 and makes it the
  // active screen; the demo widgets above stay on the (now hidden) default
  // screen. To go back to the demo screen at runtime, keep a pointer to it
  // (lv_screen_active() before this call) and lv_screen_load() it again.
  logger.info("Loading SquareLine UI...");
  // The Tab5 panel is natively 720x1280 portrait; rotate LVGL 270 degrees so
  // the UI is 1280x720 landscape (use ROTATION_90 for the other direction).
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_0);
  ui_init();

  // Benchmark against the real flex UI (PNG assets only) rather than the boot
  // screen, whose SVG logo otherwise dominates every measurement. Temporary,
  // paired with kFpsInstrument.
  if (kFpsInstrument) {
    lv_screen_load(ui_MainScreenFlex);
    // Experiment: hide the two scaled RGB565A8 images. lv_image_set_scale(150)
    // sets transformed=true in lv_draw_sw_img.c, which skips the fast
    // RGB565A8 blit and routes through transform_and_recolor per pixel.
    // Hiding them isolates that cost from the rest of the screen.
    if (kFpsHideImages) {
      lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Sample wheelchair dashboard mockup (static, no sensor wiring): builds its
  // own screen and swaps it in over ui_MainScreen. Comment out to see the
  // real telemetry UI again; RTPS/ADC binding below is unaffected either way.
  // sample_ui_home_init();
  // lv_screen_load(sample_ui_home_screen);

  // Bind the Settings-screen axis bars to the ADC subjects (observer pattern).
  // Bars show the calibrated joystick position as a percentage: -100..+100,
  // centered at 0. Raw millivolts still go to RTPS (see the ADC task).
  lv_subject_init_int(&adc_x_subject, 0);
  lv_subject_init_int(&adc_y_subject, 0);
  lv_subject_init_int(&adc_twist_subject, 0);
  lv_bar_set_range(ui_XBar, -100, 100);
  lv_bar_set_range(ui_YBar, -100, 100);
  lv_bar_set_range(ui_TwistBar, -100, 100);
  lv_bar_bind_value(ui_XBar, &adc_x_subject);
  lv_bar_bind_value(ui_YBar, &adc_y_subject);
  lv_bar_bind_value(ui_TwistBar, &adc_twist_subject);

  // GPIO48 test button. The label has a built-in binding; the panel background
  // is a style property with no binding, so it gets an observer bound to the
  // object (torn down with it).
  lv_subject_init_int(&button_count_subject, 0);
  lv_subject_init_int(&button_pressed_subject, 0);
  lv_label_bind_text(ui_ButtonCounter, &button_count_subject, "%d");
  lv_subject_add_observer_obj(&button_pressed_subject, gpio48_panel_observer, ui_ButtonPanel,
                              nullptr);

  // GPIO48, pulled up and shorted to ground on press (board-wide convention),
  // so active LOW. Constructed after the subjects are initialized, because the
  // interrupt task starts here and its callback writes them. The internal
  // pull-up is redundant against the external one but harmless.
  logger.info("Initializing GPIO48 test button...");
  static espp::Button gpio48_button({
      .name = "GPIO48 Button",
      .interrupt_config =
          {
              .gpio_num = 48,
              .callback = gpio48_button_callback,
              .active_level = espp::Button::ActiveLevel::LOW,
              .interrupt_type = espp::Button::InterruptType::ANY_EDGE,
              .pullup_enabled = true,
          },
      .task_config = {.name = "Button", .stack_size_bytes = 4 * 1024, .priority = 5},
  });

  // Joystick as an LVGL keypad input device, driving the MainScreenFlex
  // horizontal pager. The read function runs on the LVGL task and drains the
  // latch the ADC task fills, so one flick of the stick = one PRESSED cycle =
  // one LV_EVENT_KEY. Touch keeps working; indevs coexist.
  logger.info("Adding joystick keypad input device...");
  static espp::KeypadInput joystick_keypad(
      {.read = [](bool *up, bool *down, bool *left, bool *right, bool *enter, bool *escape) {
        const uint32_t key = joy_key.load(); // held, so LVGL can repeat it
        *left = key == LV_KEY_LEFT;
        *right = key == LV_KEY_RIGHT;
        *up = key == LV_KEY_UP;
        *down = key == LV_KEY_DOWN;
        *enter = select_key.exchange(false); // one-shot
        *escape = false;
      }});
  joystick_indev = joystick_keypad.get_input_device();
  joystick_group = lv_group_create();
  lv_group_add_obj(joystick_group, ui_FlexPanel);
  lv_indev_set_group(joystick_indev, joystick_group);
  // hold-to-repeat feel. LVGL's defaults (400 ms then every 100 ms) are tuned
  // for a keyboard and run the settings list far too fast for a joystick you
  // steer with; these are the two knobs if it feels wrong on the bench.
  lv_indev_set_long_press_time(joystick_keypad.get_input_device(), 500);
  lv_indev_set_long_press_repeat_time(joystick_keypad.get_input_device(), 250);
  lv_group_focus_obj(ui_FlexPanel);
  // drop the built-in arrow scroll so only flex_key_cb acts on the key. Done
  // here rather than in ui_MainScreenFlex.c because import_ui.ps1 mirrors the
  // SquareLine export (robocopy /MIR) and would delete the edit.
  lv_obj_remove_flag(ui_FlexPanel, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
  lv_obj_add_event_cb(ui_FlexPanel, flex_key_cb, LV_EVENT_KEY, nullptr);

  // (Older exports marked the flex pages hidden, which broke paging outright:
  // flex_scroll_step sizes its step from child 0, and a hidden child lays out
  // at zero width. The current export no longer does, so the un-hide that used
  // to live here is gone. If paging ever goes dead again after a re-import,
  // check the HIDDEN flag on ui_LockedPanel first.)

  // Lock/unlock for the LockedPanel page. Locked and un-paged at boot, which is
  // what the export already draws, so the initial observer run is a no-op
  // rather than a visible flicker.
  lv_subject_init_int(&locked_subject, 1);
  lv_subject_init_int(&paging_subject, 0);
  // padlock swap: each image hides on the state that is not its own
  lv_obj_bind_flag_if_eq(ui_Lock, &locked_subject, LV_OBJ_FLAG_HIDDEN, 0);
  lv_obj_bind_flag_if_eq(ui_Unlock, &locked_subject, LV_OBJ_FLAG_HIDDEN, 1);
  lv_subject_add_observer_obj(&locked_subject, lock_text_observer, ui_TextPanel, &kLockTitleText);
  lv_subject_add_observer_obj(&locked_subject, lock_text_observer, ui_Info4, &kLockHintText);

  // The push-and-hold gestures. Each one's subject drives exactly one widget,
  // and one shared timer polls them all — only the gesture whose applies() is
  // true on the current screen can be filling at any moment. The two exit bars
  // live on screens ui_init has already built.
  lv_subject_init_int(&unlock_gesture.progress, 0);
  lv_subject_init_int(&drive_enter_gesture.progress, 0);
  lv_subject_init_int(&drive_exit_gesture.progress, 0);
  lv_subject_init_int(&seat_enter_gesture.progress, 0);
  lv_arc_bind_value(ui_UnlockArc, &unlock_gesture.progress);
  lv_arc_bind_value(ui_UnlockArc1, &drive_enter_gesture.progress);
  lv_arc_bind_value(ui_UnlockArc2, &seat_enter_gesture.progress);
  lv_subject_init_int(&seat_exit_gesture.progress, 0);
  lv_subject_init_int(&seat_back_gesture.progress, 0);
  lv_bar_set_range(ui_ExitBarPull, 0, kHoldMax);
  lv_bar_bind_value(ui_ExitBarPull, &drive_exit_gesture.progress);
  lv_bar_set_range(ui_ExitBarPull1, 0, kHoldMax);
  lv_bar_bind_value(ui_ExitBarPull1, &seat_exit_gesture.progress);
  lv_bar_set_range(ui_ExitBarPushLeft, 0, kHoldMax);
  lv_bar_bind_value(ui_ExitBarPushLeft, &seat_back_gesture.progress);
  lv_timer_create(hold_poll_cb, kHoldPollMs, nullptr);

  // SeatAdjustmentFlexScreen. Both grids are built here rather than declared
  // with initialisers because the ui_* globals only exist once ui_init has run.
  seat_buttons_grid.rows = 3;
  seat_buttons_grid.cols[0] = 2;
  seat_buttons_grid.cell[0][0] = ui_SeatButton1;
  seat_buttons_grid.cell[0][1] = ui_SeatButton2;
  seat_buttons_grid.cols[1] = 2;
  seat_buttons_grid.cell[1][0] = ui_SeatButton3;
  seat_buttons_grid.cell[1][1] = ui_SeatButton4;
  seat_buttons_grid.cols[2] = 2;
  seat_buttons_grid.cell[2][0] = ui_SeatButton5;
  seat_buttons_grid.cell[2][1] = ui_SeatButton6;

  // 2 then 3: the wrap layout fits "-" and "+" on one row and the three presets
  // on the next.
  seat_adjust_grid.rows = 2;
  seat_adjust_grid.cols[0] = 2;
  seat_adjust_grid.cell[0][0] = ui_SeatAdjustmentButton1;
  seat_adjust_grid.cell[0][1] = ui_SeatAdjustmentButton2;
  seat_adjust_grid.cols[1] = 3;
  seat_adjust_grid.cell[1][0] = ui_SeatAdjustmentButton3;
  seat_adjust_grid.cell[1][1] = ui_SeatAdjustmentButton4;
  seat_adjust_grid.cell[1][2] = ui_SeatAdjustmentButton5;

  // Neither page's buttons carry a FOCUSED style in the export, so joystick
  // focus would be invisible. Recolour the 2 px border they already have, the
  // same way the settings rows do, so it tracks the day/dark theme instead of
  // being a hardcoded accent.
  //
  // The selector is spelled out rather than written `LV_PART_MAIN |
  // LV_STATE_FOCUSED` as the C export does: C++ deprecates a bitwise OR between
  // two different enum types, and -Werror turns that into a build failure.
  auto style_focus = [](lv_obj_t *button) {
    static constexpr lv_style_selector_t kFocused =
        static_cast<lv_style_selector_t>(LV_PART_MAIN) |
        static_cast<lv_style_selector_t>(LV_STATE_FOCUSED);
    ui_object_set_themeable_style_property(button, kFocused, LV_STYLE_BORDER_COLOR,
                                           _ui_theme_color_focused);
    ui_object_set_themeable_style_property(button, kFocused, LV_STYLE_BORDER_OPA,
                                           _ui_theme_alpha_focused);
  };

  // The label each function button names on the adjustment page, or null for the
  // two inert ones. Indexed to match seat_buttons_grid.
  lv_obj_t *seat_labels[3][2] = {
      {ui_SeatButtonLabel1, ui_SeatButtonLabel2},
      {ui_SeatButtonLabel3, ui_SeatButtonLabel4},
      {nullptr, nullptr},
  };

  seat_group = lv_group_create();
  for (int r = 0; r < seat_buttons_grid.rows; r++) {
    for (int c = 0; c < seat_buttons_grid.cols[r]; c++) {
      lv_obj_t *button = seat_buttons_grid.cell[r][c];
      lv_group_add_obj(seat_group, button);
      lv_obj_add_event_cb(button, grid_key_cb, LV_EVENT_KEY, &seat_buttons_grid);
      lv_obj_add_event_cb(button, seat_click_cb, LV_EVENT_CLICKED, seat_labels[r][c]);
      style_focus(button);
    }
  }

  // The adjustment buttons get navigation and focus only — grid_click_cb moves
  // the cursor and nothing else, so pressing one does nothing beyond LVGL's own
  // pressed state until their behaviour is written.
  seat_adjust_group = lv_group_create();
  for (int r = 0; r < seat_adjust_grid.rows; r++) {
    for (int c = 0; c < seat_adjust_grid.cols[r]; c++) {
      lv_obj_t *button = seat_adjust_grid.cell[r][c];
      lv_group_add_obj(seat_adjust_group, button);
      lv_obj_add_event_cb(button, grid_key_cb, LV_EVENT_KEY, &seat_adjust_grid);
      lv_obj_add_event_cb(button, grid_click_cb, LV_EVENT_CLICKED, &seat_adjust_grid);
      style_focus(button);
    }
  }

  lv_subject_init_string(&seat_function_subject, seat_function_buf, seat_function_prev_buf,
                         sizeof(seat_function_buf), lv_label_get_text(ui_AngleSettingLabel));
  lv_label_bind_text(ui_AngleSettingLabel, &seat_function_subject, nullptr);

  // Hand the joystick between groups as the screen changes. Registered on both
  // screens so every route in and out is covered.
  lv_obj_add_event_cb(ui_SeatAdjustmentFlexScreen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED,
                      nullptr);
  lv_obj_add_event_cb(ui_MainScreenFlex, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, nullptr);

  // Freeze the pager until the unlock hands it over. Each of these closes one
  // route into it, and they are genuinely independent — see paging_subject.
  // SCROLLABLE stops a touch drag; hiding the LockedPanel's own arrows stops a
  // tap on them (the other pages keep theirs, since they are only reachable
  // once paging is on); hiding the sibling pages leaves nothing to scroll to at
  // all. The joystick is handled by the guard in flex_key_cb.
  lv_obj_bind_flag_if_eq(ui_FlexPanel, &paging_subject, LV_OBJ_FLAG_SCROLLABLE, 1);
  lv_obj_bind_flag_if_eq(ui_ArrowsPanel, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);
  lv_obj_bind_flag_if_eq(ui_DrivePanel, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);
  // ui_SeatAdjustmentMenu, not ui_SeatAdjustmentPanel: a re-import renamed this
  // flex page, and the new SeatAdjustmentFlexScreen took the old name for one of
  // its own children. Binding the wrong one still compiles and fails silently.
  lv_obj_bind_flag_if_eq(ui_SeatAdjustmentMenu, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);
  lv_obj_bind_flag_if_eq(ui_SettingsMenu, &paging_subject, LV_OBJ_FLAG_HIDDEN, 0);

  // Focus group for the settings rows. Built by walking the children rather
  // than naming ui_Button10/5/6/... so rows added in SquareLine are picked up
  // on the next import with no change here. The FOCUSED styling (blue border)
  // comes from the export, so there is nothing to style in code.
  settings_group = lv_group_create();
  for (uint32_t i = 0; i < lv_obj_get_child_count(ui_SettingsFlexPanel); i++) {
    lv_group_add_obj(settings_group, lv_obj_get_child(ui_SettingsFlexPanel, i));
  }

  // LV_USE_PERF_MONITOR makes lv_display_create() show the overlay immediately
  // (lv_display.c calls lv_sysmon_show_performance), so start it hidden — it's a
  // debug readout, not part of the normal HMI. The label already exists by now,
  // which is what makes the hide safe.
  lv_sysmon_hide_performance(lv_display_get_default());
  lv_obj_add_event_cb(ui_FPSCounterButton, fps_toggle_cb, LV_EVENT_CLICKED, nullptr);

  // HAPTIC TEST settings row. CLICKED (not PRESSED) so the joystick's ENTER
  // key drives it too — the keypad indev raises the same event as a touch.
  // The label reads VIBRATING for as long as the motor runs, so the binding and
  // the countdown timer are set up before the handler that drives them.
  lv_subject_init_string(&haptic_label_subject, haptic_label_buf, haptic_label_prev_buf,
                         sizeof(haptic_label_buf), kHapticIdleText);
  lv_label_bind_text(ui_HapticTestLabel, &haptic_label_subject, nullptr);
  haptic_label_timer = lv_timer_create(haptic_label_reset_cb,
                                       static_cast<uint32_t>(kHapticBuzzDuration.count()), nullptr);
  lv_timer_pause(haptic_label_timer);
  lv_obj_add_event_cb(ui_HapticTestButton, haptic_test_cb, LV_EVENT_CLICKED, nullptr);

  // The overlay hardcodes LVGL's 14 px default font (lv_sysmon_create sets no
  // font at all), which is unreadable on a 1280x720 panel at arm's length.
  // There's no API or Kconfig for it, but the label is parented to the sys
  // layer, and with LV_USE_MEM_MONITOR off it is that layer's only child.
  if (lv_obj_t *perf_label =
          lv_obj_get_child(lv_display_get_layer_sys(lv_display_get_default()), 0)) {
    lv_obj_set_style_text_font(perf_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_pad_all(perf_label, 10, 0); // grow the backing box to match
  }

  logger.info("Initializing touch...");
  if (!tab5.initialize_touch(touch_callback)) {
    logger.error("Failed to initialize touch!");
    return;
  }

  // start a simple thread to do the lv_task_handler every 8ms — the refresh
  // timer runs at 16ms (60 fps), polling at twice that rate keeps its firing
  // jitter well under a frame
  logger.info("Starting LVGL task...");
  espp::Task lv_task(
      {.callback = [](std::mutex &m, std::condition_variable &cv) -> bool {
         auto start_time = std::chrono::high_resolution_clock::now();
         {
           std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
           if (kFpsStress) {
             lv_obj_invalidate(lv_screen_active());
           }
           lv_task_handler();
         }
         if (kFpsInstrument) {
           static int64_t last_report_us = 0;
           const int64_t now_us = esp_timer_get_time();
           if (last_report_us == 0)
             last_report_us = now_us;
           if (now_us - last_report_us >= 1000000) {
             const uint32_t frames = fps_frames.exchange(0);
             const uint64_t total_us = fps_render_us_total.exchange(0);
             const uint32_t max_us = fps_render_us_max.exchange(0);
             const float secs = (now_us - last_report_us) / 1e6f;
             last_report_us = now_us;
             fmt::print("[FPS] {:.1f} fps | render avg {:.2f} ms | max {:.2f} ms\n", frames / secs,
                        frames ? (total_us / 1000.0f) / frames : 0.0f, max_us / 1000.0f);
           }
         }
         std::unique_lock<std::mutex> lock(m);
         // Always yield at least one tick: once a render cycle exceeds 8 ms
         // the deadline is already past and wait_until returns without
         // yielding, which pins core 1 at priority 20 and starves IDLE1.
         const auto deadline =
             std::max(start_time + 8ms, std::chrono::high_resolution_clock::now() + 1ms);
         cv.wait_until(lock, deadline, []() { return false; });
         return false;
       },
       .task_config = {
           .name = "lv_task",
           .stack_size_bytes = 32 * 1024,
           .priority = 20,
           .core_id = 1,
       }});
  if (!lv_task.start()) {
    logger.error("Failed to start LVGL task!");
    return;
  }

  // load the audio file (wav file bundled in memory)
  size_t wav_size = 0;
  size_t wav_sample_rate = 0;
  if (!load_audio(wav_size, wav_sample_rate)) {
    logger.error("Failed to load audio file!");
    return;
  }
  logger.info("Loaded {} bytes of audio", wav_size);

  logger.info("Setting audio sample rate to {} Hz", wav_sample_rate);
  tab5.audio_sample_rate(wav_sample_rate);

  // unmute the audio and set the volume to 60%
  tab5.mute(false);
  tab5.volume(60.0f);

  // set the brightness to 75%
  tab5.brightness(75.0f);

  // make a task to read out various data such as IMU, battery monitoring, etc.
  // and print it to screen
  logger.info("Starting data display task...");
  espp::Task imu_task(
      {.callback = [&](std::mutex &m, std::condition_variable &cv) -> bool {
         // sleep first in case we don't get IMU data and need to exit early
         {
           std::unique_lock<std::mutex> lock(m);
           cv.wait_for(lock, 20ms);
         }
         static auto &tab5 = espp::M5StackTab5::get();
         static auto imu = tab5.imu();

         //////////////////////////////////////////////////////////////////////////
         // Update the Date/Time from the RTC
         //////////////////////////////////////////////////////////////////////////
         std::tm rtc_time;
         std::string rtc_text = "";
         if (tab5.get_rtc_time(rtc_time)) {
           rtc_text = fmt::format("\n{:%Y-%m-%d %H:%M:%S}\n", rtc_time);
         }

         //////////////////////////////////////////////////////////////////////////
         // Update the battery status
         //////////////////////////////////////////////////////////////////////////
         auto battery_status = tab5.read_battery_status();
         std::string battery_text =
             fmt::format("\nBattery: {:0.2f} V, {:0.1f} mA, {:0.1f} %, Charging: {}\n",
                         battery_status.voltage_v, battery_status.current_ma,
                         battery_status.charge_percent, battery_status.is_charging ? "Yes" : "No");

         auto now = esp_timer_get_time(); // time in microseconds
         static auto t0 = now;
         auto t1 = now;
         float dt = (t1 - t0) / 1'000'000.0f; // convert us to s
         t0 = t1;

         //////////////////////////////////////////////////////////////////////////
         // Update the IMU data
         //////////////////////////////////////////////////////////////////////////
         std::error_code ec;
         // update the imu data
         if (!imu->update(dt, ec)) {
           return false;
         }
         // get accel
         auto accel = imu->get_accelerometer();
         auto gyro = imu->get_gyroscope();
         auto temp = imu->get_temperature();
         auto orientation = imu->get_orientation();
         auto gravity_vector = imu->get_gravity_vector();
         // invert the axes
         gravity_vector.y = -gravity_vector.y;
         gravity_vector.x = -gravity_vector.x;

         // now update the gravity vector line to show the direction of "down"
         // taking into account the configured rotation of the display
         auto rotation = lv_display_get_rotation(lv_display_get_default());
         if (rotation == LV_DISPLAY_ROTATION_90) {
           std::swap(gravity_vector.x, gravity_vector.y);
           gravity_vector.x = -gravity_vector.x;
         } else if (rotation == LV_DISPLAY_ROTATION_180) {
           gravity_vector.x = -gravity_vector.x;
           gravity_vector.y = -gravity_vector.y;
         } else if (rotation == LV_DISPLAY_ROTATION_270) {
           std::swap(gravity_vector.x, gravity_vector.y);
           gravity_vector.y = -gravity_vector.y;
         }

         // separator for imu
         std::string imu_text = "\nIMU Data:\n";
         imu_text += fmt::format("Accel: {:02.2f} {:02.2f} {:02.2f}\n", accel.x, accel.y, accel.z);
         imu_text += fmt::format("Gyro: {:03.2f} {:03.2f} {:03.2f}\n", espp::deg_to_rad(gyro.x),
                                 espp::deg_to_rad(gyro.y), espp::deg_to_rad(gyro.z));
         imu_text += fmt::format("Angle: {:03.2f} {:03.2f}\n", espp::rad_to_deg(orientation.roll),
                                 espp::rad_to_deg(orientation.pitch));
         imu_text += fmt::format("Temp: {:02.1f} C\n", temp);

         // use the pitch to to draw a line on the screen indiating the
         // direction from the center of the screen to "down"
         int x0 = tab5.rotated_display_width() / 2;
         int y0 = tab5.rotated_display_height() / 2;

         int x1 = x0 + 50 * gravity_vector.x;
         int y1 = y0 + 50 * gravity_vector.y;

         static lv_point_precise_t line_points0[2] = {};
         line_points0[0].x = x0;
         line_points0[0].y = y0;
         line_points0[1].x = x1;
         line_points0[1].y = y1;

         // Now show the madgwick filter
         auto madgwick_orientation = madgwick_filter_fn(dt, accel, gyro);
         float roll = madgwick_orientation.roll;
         float pitch = madgwick_orientation.pitch;
         [[maybe_unused]] float yaw = madgwick_orientation.yaw;
         float vx = sin(pitch);
         float vy = -cos(pitch) * sin(roll);
         [[maybe_unused]] float vz = -cos(pitch) * cos(roll);

         // invert the axes
         vx = -vx;
         vy = -vy;

         // now update the line to show the direction of "down" based on the
         // configured rotation of the display
         if (rotation == LV_DISPLAY_ROTATION_90) {
           std::swap(vx, vy);
           vx = -vx;
         } else if (rotation == LV_DISPLAY_ROTATION_180) {
           vx = -vx;
           vy = -vy;
         } else if (rotation == LV_DISPLAY_ROTATION_270) {
           std::swap(vx, vy);
           vy = -vy;
         }

         x1 = x0 + 50 * vx;
         y1 = y0 + 50 * vy;

         static lv_point_precise_t line_points1[2] = {};
         line_points1[0].x = x0;
         line_points1[0].y = y0;
         line_points1[1].x = x1;
         line_points1[1].y = y1;

         std::string text = fmt::format("{}\n\n\n\n\n", label_text);
         text += battery_text;
         text += rtc_text;
         text += imu_text;

         std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
         lv_label_set_text(label, text.c_str());
         lv_line_set_points(line0, line_points0, 2);
         lv_line_set_points(line1, line_points1, 2);

         return false;
       },
       .task_config = {
           .name = "Data Display Task",
           .stack_size_bytes = 6 * 1024,
           .priority = 10,
           .core_id = 1,
       }});
  imu_task.start();

  // guards the joystick range-mapping math (center/range deadbands, circular
  // clamp, and that twist stays independent of the X/Y gimbal). Asserts, so it
  // aborts loudly on a regression; compiles to nothing under NDEBUG.
  espp::joystick_selftest();

  logger.info("Starting continuous adc...");

  // X/Y: ADC1_CH0/CH1 = GPIO16/GPIO17 on the M5-Bus header.
  // Twist: ADC2_CH3 = GPIO52 on the M5-Bus (the W5500 INT moved to GPIO4 to
  // free it — analog inputs can't be re-routed through the GPIO matrix).
  // The twist channel is sampled oneshot rather than through the continuous
  // driver: mixing both units via ADC_CONV_BOTH_UNIT produced a stream of
  // invalid DMA frames on the P4 (log spam that starved LVGL's first frame).
  std::vector<espp::AdcConfig> channels{
      {.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_0, .attenuation = ADC_ATTEN_DB_12},
      {.unit = ADC_UNIT_1, .channel = ADC_CHANNEL_1, .attenuation = ADC_ATTEN_DB_12}};
  static const espp::AdcConfig twist_channel{
      .unit = ADC_UNIT_2, .channel = ADC_CHANNEL_3, .attenuation = ADC_ATTEN_DB_12};
  // this initailizes the DMA and filter task for the continuous adc
  espp::ContinuousAdc adc({.sample_rate_hz = 1 * 1000,
                           .channels = channels,
                           .convert_mode = ADC_CONV_SINGLE_UNIT_1,
                           .window_size_bytes = 1024,
                           .log_level = espp::Logger::Verbosity::WARN});
  adc.start();
  static espp::OneshotAdc twist_adc({.unit = ADC_UNIT_2, .channels = {twist_channel}});

  // Joystick calibration. These are the numbers to tune per unit: run
  // scripts/rtps_adc_plot.py, note the resting mV of each axis and the mV at
  // full deflection each way, and put them here. The values below are the
  // ideal-divider defaults (0-3300 mV, centered) and WILL be off on real
  // hardware.
  //
  // X/Y are one circular gimbal, so their per-axis deadbands are 0 and the
  // deadzone lives on the vector radius instead (center_deadzone_radius /
  // range_deadzone below). Twist is a separate pot on its own axis, so it
  // carries its own center/range deadbands.
  //
  // AXIS WIRING: the gimbal pots are cross-wired relative to the channel
  // names. ADC1_CH1 (GPIO17) is the HORIZONTAL axis and reads higher to the
  // right, so it feeds the joystick's X with no inversion. ADC1_CH0 (GPIO16)
  // is the VERTICAL axis and reads *lower* moving up, so it feeds Y with
  // invert_output — after which +Y is up, as the rest of the code assumes.
  // Fixed here at the calibration level so every consumer (UI bars, the
  // keypad pager, RTPS) sees correct axes without compensating itself.
  static constexpr espp::FloatRangeMapper::Config kHorizontalCal{
      .center = 1650.0f, .center_deadband = 0.0f, .minimum = 0.0f, .maximum = 3300.0f};
  static constexpr espp::FloatRangeMapper::Config kVerticalCal{.center = 1650.0f,
                                                               .center_deadband = 0.0f,
                                                               .minimum = 0.0f,
                                                               .maximum = 3300.0f,
                                                               .invert_output = true};
  static constexpr espp::FloatRangeMapper::Config kTwistCal{.center = 1650.0f,
                                                            .center_deadband = 60.0f,
                                                            .minimum = 0.0f,
                                                            .maximum = 3300.0f,
                                                            .range_deadband = 40.0f};
  static espp::Joystick stick({.x_calibration = kHorizontalCal,
                               .y_calibration = kVerticalCal,
                               .z_calibration = kTwistCal,
                               .type = espp::Joystick::Type::CIRCULAR,
                               .center_deadzone_radius = 0.10f,
                               .range_deadzone = 0.05f,
                               .log_level = espp::Logger::Verbosity::WARN});

  // customization knobs: sampling/LVGL/RTPS cadence, and how often the serial
  // line is printed. The log is divided down because 30 lines/s is the
  // console-flood pattern that starved LVGL once before.
  static constexpr auto kAdcUpdatePeriod = 33ms; // 30 Hz: ADC read, LVGL bars, RTPS publish
  static constexpr int kAdcLogDivider = 6;       // serial log every Nth cycle (~5 Hz)
  auto adc_task_fn = [&adc, &channels](std::mutex &m, std::condition_variable &cv) {
    static uint32_t cycle = 0;
    const bool log_this_cycle = (cycle++ % kAdcLogDivider) == 0;

    // see the AXIS WIRING note at the calibrations: CH1 is horizontal, CH0 is
    // vertical
    auto vert_mv = adc.get_mv(channels[0]);  // ADC1_CH0 (GPIO16)
    auto horiz_mv = adc.get_mv(channels[1]); // ADC1_CH1 (GPIO17)
    // twist pot on ADC2 (GPIO52), sampled oneshot — see comment at the
    // channel definitions above
    auto twist_mv = twist_adc.read_mv(twist_channel);

    if (vert_mv && horiz_mv && twist_mv) {
      // raw mV -> calibrated [-1,1] per axis: circular deadzone on the X/Y
      // gimbal, twist mapped independently by its own range mapper. X is the
      // horizontal channel, Y the vertical one (inverted by kVerticalCal).
      stick.update(*horiz_mv, *vert_mv, *twist_mv);

      // Analog -> keypad level. Schmitt trigger (engage past kKeyEngage, release
      // below kKeyRelease) so the boundary can't chatter; between the two
      // thresholds the previous state holds. The direction is recomputed every
      // cycle, so rolling the stick from one direction to another re-aims
      // without needing to pass through center. The larger component wins, so a
      // diagonal resolves to one direction rather than two.
      //
      // A light touch on purpose: menus should answer well before the stick is
      // anywhere near its travel limit. The release threshold sits exactly on
      // the joystick's own center_deadzone_radius, so a direction lets go the
      // moment the stick is back inside the dead zone, and the deadzone already
      // suppresses any noise below it.
      static constexpr float kKeyEngage = 0.20f;
      static constexpr float kKeyRelease = 0.10f;
      {
        static bool engaged = false;
        const float x = stick.x();
        const float y = stick.y();
        const float mag = std::max(std::abs(x), std::abs(y));
        if (mag > kKeyEngage) {
          engaged = true;
        } else if (mag < kKeyRelease) {
          engaged = false;
        }
        if (!engaged) {
          joy_key.store(0);
        } else if (std::abs(x) >= std::abs(y)) {
          joy_key.store(x > 0 ? LV_KEY_RIGHT : LV_KEY_LEFT);
        } else {
          // +Y is up after kVerticalCal's inversion, and up the list is prev
          joy_key.store(y > 0 ? LV_KEY_UP : LV_KEY_DOWN);
        }
      }
      {
        // lv_subject_set_int runs the bar's observer callback synchronously,
        // which touches the widget, so it needs the LVGL lock
        std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);
        lv_subject_set_int(&adc_x_subject, static_cast<int32_t>(stick.x() * 100.0f));
        lv_subject_set_int(&adc_y_subject, static_cast<int32_t>(stick.y() * 100.0f));
        lv_subject_set_int(&adc_twist_subject, static_cast<int32_t>(stick.z() * 100.0f));
      }

      // stream the RAW snapshot to the PC (rtps_adc_plot.py); this is what the
      // calibration constants above get measured from, so it stays in mV.
      // Ordered by logical axis (X = horizontal, Y = vertical) to match the
      // rest of the code, but the values are untouched: the vertical trace
      // still falls as the stick moves up, since invert_output is applied by
      // the mapper, not here.
      // quiet no-op until RTPS is up and a subscriber is discovered
      auto to_mv = [](float v) { return static_cast<uint32_t>(std::max(v, 0.0f)); };
      rtps_comms_publish_adc(to_mv(*horiz_mv), to_mv(*vert_mv), to_mv(*twist_mv));
    }

    if (log_this_cycle) {
      auto fmt_mv = [](const std::optional<float> &v) {
        return v ? fmt::format("{} mV", static_cast<int>(*v)) : std::string("no value");
      };
      // monotonically increasing tag: if the serial log ever goes quiet and
      // later resumes with a gap in this counter, the task kept running and the
      // console transport dropped the lines; a continuous sequence would mean
      // the task itself had paused
      static uint32_t print_seq = 0;
      // fmt::print("#{} horiz(CH1): {}\tvert(CH0): {}\ttwist: {}\n", print_seq++,
      //            fmt_mv(horiz_mv), fmt_mv(vert_mv), fmt_mv(twist_mv));
    }
    // NOTE: sleeping in this way allows the sleep to exit early when the
    // task is being stopped / destroyed
    {
      std::unique_lock<std::mutex> lk(m);
      cv.wait_for(lk, kAdcUpdatePeriod);
    }
    // don't want to stop the task
    return false;
  };
  auto adc_task = espp::Task({.callback = adc_task_fn,
                              .task_config = {.name = "Read ADC"},
                              .log_level = espp::Logger::Verbosity::INFO});
  adc_task.start();

  // bring up W5500 Ethernet + RTPS last so a missing cable / module can't
  // delay the HMI; on failure the UI keeps running without comms
  logger.info("Starting RTPS comms...");
  // remote LCD brightness (rtps_brightness.py on the PC); floor at 5% so a
  // remote command can't turn the screen fully off. brightness() drives the
  // backlight directly (no LVGL), so it's safe from the RTPS receive task.
  rtps_comms_on_brightness(
      [](float percent) { espp::M5StackTab5::get().brightness(std::max(percent, 5.0f)); });
  if (!rtps_comms_start()) {
    logger.warn("RTPS comms not started (Ethernet bring-up failed)");
  }

  // loop forever
  while (true) {
    std::this_thread::sleep_for(1s);
  }
  //! [m5stack tab5 example]
}

static bool load_audio(size_t &out_size, size_t &out_sample_rate) {
  // if the audio_bytes vector is already populated, return the size
  if (audio_bytes.size() > 0) {
    return true;
  }

  // load the audio data. these are configured in the CMakeLists.txt file

  extern const uint8_t click_wav_start[] asm("_binary_click_wav_start");
  extern const uint8_t click_wav_end[] asm("_binary_click_wav_end");
  audio_bytes = std::vector<uint8_t>(click_wav_start, click_wav_end);
  // ensure we have at least a wav header
  if (audio_bytes.size() < 44) {
    audio_bytes.clear();
    return false;
  }
  // get the sample rate from the wav header (bytes 24-27)
  uint32_t sample_rate = *(reinterpret_cast<const uint32_t *>(&audio_bytes[24]));
  // set the audio sample rate accordingly
  // decode the wav file header (first 44 bytes) and remove it
  if (audio_bytes.size() > 44) {
    audio_bytes.erase(audio_bytes.begin(), audio_bytes.begin() + 44);
  }
  out_size = audio_bytes.size();
  out_sample_rate = sample_rate;
  return true;
}

static void play_click(espp::M5StackTab5 &tab5) {
  if (audio_bytes.size() > 0) {
    tab5.play_audio(audio_bytes);
  }
}
