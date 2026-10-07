// DiagnosticsView: the DiagnosticsScreen's rows (moved from main/frag_diag.inc).

#include "hmi_ui/diagnostics_view.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "hmi_format/diag.hpp"
#include "hmi_format/stepper.hpp"
#include "hmi_ui/widget_tree.hpp"
#include "ui.h"

#include "components/ui_comp_diagrow.h"

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
  const format::StepperSpec format{nullptr, nullptr, 0, 0, 0, spec.decimals[field->field], nullptr};
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
