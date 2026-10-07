// split_main.py fragment of main.cpp (frag_stick_button: original lines 853-953); one TU, see main.cpp.
#include "stick/button_edges.hpp"
static void stick_button_edge(bool active) {
  // lv_subject_set_int runs the observers synchronously on this task, and they
  // touch widgets, so this needs the LVGL lock
  std::lock_guard<std::recursive_mutex> lock(lvgl_mutex);

  // the panel follows every edge, so contact bounce can't strand it blue: the
  // last edge always wins
  lv_subject_set_int(&button_pressed_subject, active);
  // undebounced on purpose: the MCB wants the raw line state, and this is the
  // same every-edge signal the panel follows
  joy_button_pressed.store(active);

  // What the edge means (hmi::stick::ButtonEdges): a release that ends a short
  // press selects; only the counter is debounced. One instance, on this task.
  const int64_t now = esp_timer_get_time();
  static constinit hmi::stick::ButtonEdges edges;
  const hmi::stick::ButtonEdges::Effect effect = edges.edge(active, now);
  if (effect.select) {
    select_key.store(true);
  }
  if (effect.count) {
    lv_subject_set_int(&button_count_subject, lv_subject_get_int(&button_count_subject) + 1);
  }
}

// Runs on the Button's interrupt task (not in ISR context).
static void gpio48_button_callback(const espp::Interrupt::Event &event) {
  stick_button_edge(event.active);
}

static bool load_audio(size_t &out_size, size_t &out_sample_rate);
static void play_click(espp::M5StackTab5 &tab5);
// "Can't do that", heard (play_refusal) or heard and felt (refusal_feedback);
// defined with play_click. A warning or error sounds even with Sounds off.
static void play_refusal(bool warning = false);
static void refusal_feedback();

/////////////////////////////////////////////////////////////////////////////
// Push-and-hold gestures
//
// Three places in the HMI ask the user to hold an input for kHoldMs before
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
// The lock state itself (locked_subject) is declared with the other subjects at
// the top of the file. Locked still leaves the menu reachable -- Log, UI
// Settings and the bench tools are useful with the chair not driving -- but
// nothing moves: the stick only drives from Drive (stick_drives).
/////////////////////////////////////////////////////////////////////////////

#include "hmi_ui/hold_gesture.hpp"
static constexpr uint32_t kHoldMs = hmi::ui::HOLD_MS; // hold time to fill a gesture widget
// Dead time before a hold starts filling; a select is a press shorter than this
// (hmi::stick::SELECT_MAX_US, the same value).
static constexpr uint32_t kBarGraceMs = hmi::ui::HOLD_GRACE_MS;
static constexpr int32_t kHoldMax = hmi::ui::HOLD_MAX; // arc/bar range (LVGL's default)
// Beat between the unlock landing and the Drive screen dissolving in, so the
// unlocked padlock is legible rather than a flash.
static constexpr uint32_t kUnlockAdvanceMs = 1000;
// Poll cadence for the inputs (matched to the ADC task's 33 ms period).
static constexpr uint32_t kHoldPollMs = hmi::ui::HOLD_POLL_MS;
// The gestures' timings are the drive table's (drive_session_table.hpp).
static_assert(std::chrono::milliseconds{hmi::ui::HOLD_MS} == hmi::drive_session::kHoldFill);
static_assert(std::chrono::milliseconds{hmi::ui::HOLD_GRACE_MS} == hmi::drive_session::kBarGrace);
static_assert(std::chrono::milliseconds{hmi::ui::HOLD_POLL_MS} ==
              hmi::drive_session::kHoldPollPeriod);
static_assert(hmi::stick::SELECT_MAX_US == int64_t{hmi::ui::HOLD_GRACE_MS} * 1000,
              "a press short enough to select never starts a hold filling");

