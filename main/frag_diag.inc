// split_main.py fragment of main.cpp (frag_diag: original lines 2980-3229); one TU, see main.cpp.
#include "hmi_format/diag.hpp"
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

// Each reading's container, units label and value label in the component.
struct DiagFieldIds {
  int container;
  int units;
  int value;
};
static constexpr DiagFieldIds kDiagFieldIds[rammp::kDiagFields] = {
    {UI_COMP_DIAGROW_CELL1, UI_COMP_DIAGROW_CELL1_UNIT1, UI_COMP_DIAGROW_CELL1_VALUE1},
    {UI_COMP_DIAGROW_CELL2, UI_COMP_DIAGROW_CELL2_UNIT2, UI_COMP_DIAGROW_CELL2_VALUE2},
    {UI_COMP_DIAGROW_CELL3, UI_COMP_DIAGROW_CELL3_UNIT3, UI_COMP_DIAGROW_CELL3_VALUE3},
};

// Readings, raw, [table row][reading]; kValueUnknown until the MCB sends one.
// Static: the rows' observers point at them, and they keep updating with no
// screen up.
static lv_subject_t diag_value[rammp::kDiagCount][rammp::kDiagFields];
static lv_subject_t diag_stale_subject; // 1 = nothing within rammp::kDiagTimeout, or ever
static lv_subject_t diag_rate_subject;  // arrival rate, tenths of a Hz

// Which reading a value observer shows; static so an observer can point at it.
struct DiagField {
  uint8_t item;
  uint8_t field;
};
static DiagField diag_fields[rammp::kDiagCount][rammp::kDiagFields];

static lv_obj_t *diag_rows[rammp::kDiagCount];
static int diag_row_count; // rows on screen; 0 while it is not up
static int diag_cursor;    // which row the joystick is on
static lv_group_t *diag_group = nullptr;

static constexpr lv_style_selector_t kDiagStale = static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                                                  static_cast<lv_style_selector_t>(LV_STATE_USER_1);
static constexpr uint32_t kDiagStaleColour = 0xFF5050; // the self test's FAIL red

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

// A reading, scaled by its decimals. stepper_format's StepperSpec carries the
// decimals; the rest of it does not apply here.
static void diag_value_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *field = static_cast<const DiagField *>(lv_observer_get_user_data(observer));
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const int32_t raw = lv_subject_get_int(subject);
  if (raw == kValueUnknown) {
    lv_label_set_text(label, "--");
    return;
  }
  const rammp::DiagSpec &spec = rammp::kDiagItems[field->item];
  const StepperSpec format{nullptr, nullptr, 0, 0, 0, spec.decimals[field->field], nullptr};
  char text[24];
  stepper_format(format, raw, text);
  lv_label_set_text(label, text);
}

static lv_opa_t diag_blink_opa() {
  return lv_subject_get_int(&rtps_blink_subject) ? LV_OPA_COVER : LV_OPA_TRANSP;
}

// Every label under `obj` takes LV_STATE_USER_1 while stale, whose style is
// red text at the blink's opacity.
static void diag_mark_stale(lv_obj_t *obj, bool stale, lv_opa_t opa) {
  struct Mark {
    bool stale;
    lv_opa_t opa;
  } mark{stale, opa};
  hmi::ui::for_each_descendant(
      obj,
      [](lv_obj_t *child, void *ctx) {
        const auto *m = static_cast<const Mark *>(ctx);
        if (lv_obj_check_type(child, &lv_label_class)) {
          lv_obj_set_style_text_color(child, lv_color_hex(kDiagStaleColour), kDiagStale);
          lv_obj_set_style_text_opa(child, m->opa, kDiagStale);
          lv_obj_set_state(child, LV_STATE_USER_1, m->stale);
        }
      },
      &mark);
}

// Bound to each row, on the stale and blink subjects both.
static void diag_row_stale_observer(lv_observer_t *observer, lv_subject_t *) {
  diag_mark_stale(lv_observer_get_target_obj(observer),
                  lv_subject_get_int(&diag_stale_subject) != 0, diag_blink_opa());
}

// DiagnosticsFreqLabel, on the rate, stale and blink subjects.
static void diag_freq_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const bool stale = lv_subject_get_int(&diag_stale_subject) != 0;
  if (stale) {
    lv_label_set_text(label, "No data");
  } else {
    char text[32]; // "-214748364.-8 Hz - Live" at the very most
    hmi::format::diag_rate_text(lv_subject_get_int(&diag_rate_subject), text);
    lv_label_set_text(label, text);
  }
  lv_obj_set_style_text_color(label, lv_color_hex(kDiagStaleColour), kDiagStale);
  lv_obj_set_style_text_opa(label, diag_blink_opa(), kDiagStale);
  lv_obj_set_state(label, LV_STATE_USER_1, stale);
}

// Clamped rather than wrapped, for the reason in grid_key_cb.
static void diag_focus(int index) {
  if (diag_row_count == 0) {
    return;
  }
  diag_cursor = std::clamp(index, 0, diag_row_count - 1);
  lv_group_focus_obj(diag_rows[diag_cursor]);
}

// Up/down walk the rows, which scrolls a long table into view.
static void diag_key_cb(lv_event_t *e) {
  switch (lv_event_get_key(e)) {
  case LV_KEY_UP:
    diag_focus(diag_cursor - 1);
    return;
  case LV_KEY_DOWN:
    if (diag_cursor + 1 >= diag_row_count) {
      nav_to_key(); // below the last row is the burger key
      return;
    }
    diag_focus(diag_cursor + 1);
    return;
  default:
    return;
  }
}

// Deleting a row takes its observers and events with it and drops it from
// diag_group; the theme manager forgets it too.
static void diag_rows_clear() {
  for (int i = 0; i < diag_row_count; i++) {
    lv_obj_delete(diag_rows[i]);
  }
  diag_row_count = 0;
  diag_cursor = 0;
}

// Builds the rows and shows the screen. LVGL task (a click).
static void diagnostics_open() {
  diagnostics_screen_ensure(); // built on demand: see "Screens built on demand"
  diag_rows_clear();
  const auto &specs = rammp::kDiagItems;
  for (uint8_t i = 0; i < rammp::kDiagCount; i++) {
    lv_obj_t *row = ui_DiagRow_create(ui_DiagnosticsFlexRows);
    diag_rows[diag_row_count++] = row;
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
      diag_fields[i][f] = {i, f};
      lv_subject_add_observer_obj(&diag_value[i][f], diag_value_observer,
                                  ui_comp_get_child(row, kDiagFieldIds[f].value),
                                  &diag_fields[i][f]);
    }
    lv_subject_add_observer_obj(&diag_stale_subject, diag_row_stale_observer, row, nullptr);
    lv_subject_add_observer_obj(&rtps_blink_subject, diag_row_stale_observer, row, nullptr);
    lv_group_add_obj(diag_group, row);
    lv_obj_add_event_cb(row, diag_key_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(row, setting_focus_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(row, setting_focus_cb, LV_EVENT_DEFOCUSED, nullptr);
    // Exported in its focused preview state, like the actuator rows; the
    // focus callbacks are left as the only writers of LV_STATE_FOCUSED.
    set_focused_recursive(row, false);
    clear_click_focusable_recursive(row);
  }
  _ui_screen_change(&ui_DiagnosticsScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_DiagnosticsScreen_screen_init);
}

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
