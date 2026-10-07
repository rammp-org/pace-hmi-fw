// split_main.py fragment of main.cpp (frag_diag: original lines 2980-3229); one TU, see main.cpp.
#include "hmi_format/diag.hpp"
#include "hmi_ui/diagnostics_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// DiagnosticsScreen: live readings from the MCB
//
// Opened from the DIAGNOSTICS settings row, left by pulling and holding. One
// row per entry in RAMMP_DIAG_TABLE (messages/joystick_message.hpp): short label, label,
// and up to three readings, each under its unit. The MCB publishes them all on
// rammp::kMcbDiagnostics every rammp::kDiagPeriod; they land in
// diag_value (subjects, set under the LVGL lock by the RTPS handler in
// app_main) and the rows observe them.
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
/////////////////////////////////////////////////////////////////////////////

static_assert(rammp::kDiagFields == 3, "the DiagnosticComponent has exactly three readings");

// Readings, raw, [table row][reading]; kValueUnknown until the MCB sends one.
// Static: the rows' observers point at them, and they keep updating with no
// screen up.
static lv_subject_t diag_value[rammp::kDiagCount][rammp::kDiagFields];
static lv_subject_t diag_stale_subject; // 1 = nothing within rammp::kDiagTimeout, or ever
static lv_subject_t diag_rate_subject;  // arrival rate, tenths of a Hz
static lv_group_t *diag_group = nullptr;

// Keeps diag_stale_subject and diag_rate_subject current. LVGL task, from
// rtps_poll_cb: staleness has to be polled - nothing happens when a sample
// fails to arrive.
static void diag_poll() {
  const RtpsDiagStats stats = rtps_comms_diag_stats();
  const bool stale =
      stats.last_us == 0 ||
      esp_timer_get_time() - stats.last_us >
          std::chrono::duration_cast<std::chrono::microseconds>(rammp::kDiagTimeout).count();
  lv_subject_set_int(&diag_stale_subject, stale ? 1 : 0);
  lv_subject_set_int(&diag_rate_subject, stale ? 0 : stats.rate_tenths_hz);
}

// Each reading's container, units label and value label in the component.
struct DiagFieldIds {
  uint32_t container;
  uint32_t units;
  uint32_t value;
};
static constexpr DiagFieldIds kDiagFieldIds[rammp::kDiagFields] = {
    {UI_COMP_DIAGROW_CELL1, UI_COMP_DIAGROW_CELL1_UNIT1, UI_COMP_DIAGROW_CELL1_VALUE1},
    {UI_COMP_DIAGROW_CELL2, UI_COMP_DIAGROW_CELL2_UNIT2, UI_COMP_DIAGROW_CELL2_VALUE2},
    {UI_COMP_DIAGROW_CELL3, UI_COMP_DIAGROW_CELL3_UNIT3, UI_COMP_DIAGROW_CELL3_VALUE3},
};

static constexpr lv_style_selector_t kDiagStale = static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                                                  static_cast<lv_style_selector_t>(LV_STATE_USER_1);

void hmi::ui::DiagnosticsView::init_subjects() {
  for (size_t i = 0; i < rammp::kDiagCount; i++) {
    for (auto &reading : config_.values[i]) {
      lv_subject_init_int(&reading, format::VALUE_UNKNOWN);
    }
  }
  lv_subject_init_int(config_.stale, 1);
  lv_subject_init_int(config_.rate, 0);
}

lv_group_t *hmi::ui::DiagnosticsView::init() {
  group_ = lv_group_create();
  return group_;
}

// A reading, scaled by its decimals. stepper_format's StepperSpec carries the
// decimals; the rest of it does not apply here.
void hmi::ui::DiagnosticsView::value_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *field = static_cast<const Field *>(lv_observer_get_user_data(observer));
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const int32_t raw = lv_subject_get_int(subject);
  if (raw == format::VALUE_UNKNOWN) {
    lv_label_set_text(label, "--");
    return;
  }
  const rammp::DiagSpec &spec = rammp::kDiagItems[field->item];
  const format::StepperSpec format{nullptr, nullptr, 0, 0, 0, spec.decimals[field->field],
                                   nullptr};
  char text[24];
  format::stepper_format(format, raw, text);
  lv_label_set_text(label, text);
}

lv_opa_t hmi::ui::DiagnosticsView::blink_opa() const {
  return lv_subject_get_int(config_.blink) ? LV_OPA_COVER : LV_OPA_TRANSP;
}

// Every label under `obj` takes LV_STATE_USER_1 while stale, whose style is
// red text at the blink's opacity.
void hmi::ui::DiagnosticsView::mark_stale(lv_obj_t *obj, bool stale, lv_opa_t opa) {
  struct Mark {
    bool stale;
    lv_opa_t opa;
  } mark{stale, opa};
  for_each_descendant(
      obj,
      [](lv_obj_t *child, void *ctx) {
        const auto *m = static_cast<const Mark *>(ctx);
        if (lv_obj_check_type(child, &lv_label_class)) {
          lv_obj_set_style_text_color(child, lv_color_hex(STALE_COLOUR), kDiagStale);
          lv_obj_set_style_text_opa(child, m->opa, kDiagStale);
          lv_obj_set_state(child, LV_STATE_USER_1, m->stale);
        }
      },
      &mark);
}

// Bound to each row, on the stale and blink subjects both.
void hmi::ui::DiagnosticsView::row_stale_observer(lv_observer_t *observer, lv_subject_t *) {
  const auto *view = static_cast<const DiagnosticsView *>(lv_observer_get_user_data(observer));
  mark_stale(lv_observer_get_target_obj(observer), lv_subject_get_int(view->config_.stale) != 0,
             view->blink_opa());
}

// DiagnosticsFreqLabel, on the rate, stale and blink subjects.
void hmi::ui::DiagnosticsView::paint_freq(lv_obj_t *label) const {
  const bool stale = lv_subject_get_int(config_.stale) != 0;
  if (stale) {
    lv_label_set_text(label, "No data");
  } else {
    char text[32]; // "-214748364.-8 Hz - Live" at the very most
    format::diag_rate_text(lv_subject_get_int(config_.rate), text);
    lv_label_set_text(label, text);
  }
  lv_obj_set_style_text_color(label, lv_color_hex(STALE_COLOUR), kDiagStale);
  lv_obj_set_style_text_opa(label, blink_opa(), kDiagStale);
  lv_obj_set_state(label, LV_STATE_USER_1, stale);
}

// Clamped rather than wrapped, for the reason in grid_key_cb.
void hmi::ui::DiagnosticsView::focus(int index) {
  if (row_count_ == 0) {
    return;
  }
  cursor_ = std::clamp(index, 0, row_count_ - 1);
  lv_group_focus_obj(rows_[cursor_]);
}

// Up/down walk the rows, which scrolls a long table into view.
void hmi::ui::DiagnosticsView::key_cb(lv_event_t *e) {
  auto *view = static_cast<DiagnosticsView *>(lv_event_get_user_data(e));
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    view->focus(view->cursor_ - 1);
    return;
  case LV_KEY_DOWN:
    if (view->cursor_ + 1 >= view->row_count_) {
      view->config_.nav->to_key(); // below the last row is the burger key
      return;
    }
    view->focus(view->cursor_ + 1);
    return;
  default:
    return;
  }
}

// Deleting a row takes its observers and events with it and drops it from
// the group; the theme manager forgets it too.
void hmi::ui::DiagnosticsView::rows_clear() {
  for (int i = 0; i < row_count_; i++) {
    lv_obj_delete(rows_[i]);
  }
  row_count_ = 0;
  cursor_ = 0;
}

// Builds the rows and shows the screen. LVGL task (a click).
void hmi::ui::DiagnosticsView::open() {
  config_.screen_ensure(); // built on demand: see "Screens built on demand"
  rows_clear();
  const auto &specs = rammp::kDiagItems;
  for (uint8_t i = 0; i < rammp::kDiagCount; i++) {
    lv_obj_t *row = ui_DiagRow_create(ui_DiagnosticsFlexRows);
    rows_[row_count_++] = row;
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_DIAGROW_DIAGHEAD_DIAGNAMES_DIAGSHORT),
                      specs[i].short_name);
    lv_label_set_text(ui_comp_get_child(row, UI_COMP_DIAGROW_DIAGHEAD_DIAGNAMES_DIAGLABEL),
                      specs[i].label);
    for (uint8_t f = 0; f < rammp::kDiagFields; f++) {
      if (specs[i].unit[f][0] == '\0') { // a reading this item does not have
        lv_obj_add_flag(ui_comp_get_child(row, kDiagFieldIds[f].container), LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_label_set_text(ui_comp_get_child(row, kDiagFieldIds[f].units), specs[i].unit[f]);
      fields_[i][f] = {i, f};
      lv_subject_add_observer_obj(&config_.values[i][f], value_observer,
                                  ui_comp_get_child(row, kDiagFieldIds[f].value), &fields_[i][f]);
    }
    lv_subject_add_observer_obj(config_.stale, row_stale_observer, row, this);
    lv_subject_add_observer_obj(config_.blink, row_stale_observer, row, this);
    lv_group_add_obj(group_, row);
    lv_obj_add_event_cb(row, key_cb, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(row, config_.row_focus_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(row, config_.row_focus_cb, LV_EVENT_DEFOCUSED, nullptr);
    // Exported in its focused preview state, like the actuator rows; the
    // focus callbacks are left as the only writers of LV_STATE_FOCUSED.
    set_focused_recursive(row, false);
    clear_click_focusable_recursive(row);
  }
  _ui_screen_change(&ui_DiagnosticsScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_DiagnosticsScreen_screen_init);
}

// The one instance; the readings, the stale and rate subjects and the group stay out here
// (rtps_poll_cb, the RTPS handler, the on-demand screen and nav use them).
static constinit hmi::ui::DiagnosticsView diag_view{{
    .nav = &ui_nav_port,
    .values = diag_value,
    .stale = &diag_stale_subject,
    .rate = &diag_rate_subject,
    .blink = &rtps_blink_subject,
    .screen_ensure = diagnostics_screen_ensure,
    .row_focus_cb = setting_focus_cb,
}};
static void diag_freq_observer(lv_observer_t *observer, lv_subject_t *) {
  diag_view.paint_freq(lv_observer_get_target_obj(observer));
}
static void diag_focus(int index) { diag_view.focus(index); }
static void diag_rows_clear() { diag_view.rows_clear(); }
static void diagnostics_open() { diag_view.open(); }

/////////////////////////////////////////////////////////////////////////////
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
// nav_attach_chrome is called once per screen -- from app_main for the screens
// ui_init builds, and from the *_screen_ensure() functions for the ones built
// on demand.
//
// This lives in main.cpp rather than a ui_nav.cpp: every destination is a
// file-static here (the *_open functions, the focus groups, the PIN gate), and
// a header exporting all of them to one caller would be more coupling than the
// split removes.
/////////////////////////////////////////////////////////////////////////////

// The overlay sits over the body; sliding it clear of the screen bottom is the
// closed position. 921 px in 280 ms is the spec's transition.
static constexpr int32_t kMenuShownY = 195;
static constexpr int32_t kMenuHiddenY = 195 + 921;
static constexpr uint32_t kMenuSlideMs = 280;
// p21 "ROW PRESS": the row holds negative this long before anything moves.
static constexpr uint32_t kRowPressMs = 300;
