// SettingsView: the SettingsScreen's rows (moved from main/frag_settings_ui.inc). What the pages
// hold and what a setting does when it changes stay in main.

#include "hmi_ui/settings_view.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "hmi_format/stepper.hpp"
#include "hmi_ui/widget_tree.hpp"
#include "messages/joystick_message.hpp"
#include "settings_spec.hpp"
#include "ui.h"

#include "components/ui_comp_settingrow.h"

namespace {

struct SettingPageText {
  const char *title;
  const char *instructions;
};

constexpr SettingPageText kActuatorsPageText{"DEBUG ACTUATORS",
                                             "Up/down to pick, left/right or -/+ to move."};

// What the named rows read, in SETTINGS_PARAM_* order; nullptr = a number.
constexpr const char *kThemeNames[] = {"Dark", "Day"};
constexpr const char *kOnOffNames[] = {"Off", "On"};
constexpr const char *kMirrorNames[] = {"Normal", "Mirror"};
constexpr const char *kSwapNames[] = {"Normal", "Swap"};
// NetLink order. Not a Settings row: Internet Settings draws it as two buttons.
constexpr const char *kNetworkNames[] = {"Ethernet", "WiFi"};
constexpr const char *const *kSettingParamNames[] = {
    nullptr,       // SETTINGS_PARAM_BRIGHTNESS
    kThemeNames,   // SETTINGS_PARAM_THEME
    kOnOffNames,   // SETTINGS_PARAM_MENU_SLIDE
    kOnOffNames,   // SETTINGS_PARAM_FLIP
    nullptr,       // SETTINGS_PARAM_STICK_SENSITIVITY
    nullptr,       // SETTINGS_PARAM_DRIVE_SPEED
    kMirrorNames,  // SETTINGS_PARAM_STICK_INVERT_X
    kMirrorNames,  // SETTINGS_PARAM_STICK_INVERT_Y
    kSwapNames,    // SETTINGS_PARAM_STICK_SWAP
    kOnOffNames,   // SETTINGS_PARAM_SOUNDS
    kNetworkNames, // SETTINGS_PARAM_NETWORK
};
static_assert(std::size(kSettingParamNames) == SETTINGS_PARAM_COUNT,
              "every settings_spec.hpp parameter needs its names (or nullptr) here");

constexpr int kSettingRowsMax = std::max<int>(rammp::kSeatAxisCount, SETTINGS_PARAM_COUNT);
static_assert(kSettingRowsMax <= hmi::ui::SettingsView::ROWS_MAX,
              "a page has more rows than the view");

} // namespace

void hmi::ui::SettingsView::open_page(int32_t page) {
  config_.screen_ensure(); // built on demand: see "Screens built on demand"
  rows_clear();
  const SettingPageText text =
      page == config_.actuators_page
          ? kActuatorsPageText
          : SettingPageText{SETTINGS_PAGES[static_cast<size_t>(page)].title,
                            SETTINGS_PAGES[static_cast<size_t>(page)].instructions};
  lv_label_set_text(ui_SettingsTitle, text.title);
  // text.instructions has nowhere to go yet: the spec-V2 body is a title and
  // the rows, with no line between them. Phase 5 adds one when it lays this
  // screen out properly.
  if (page == config_.actuators_page) {
    const auto &specs = rammp::kSeatAxes;
    for (size_t i = 0; i < rammp::kSeatAxisCount; i++) {
      row_add({specs[i].short_name, specs[i].label, specs[i].min_value, specs[i].max_value,
               specs[i].step, specs[i].decimals, nullptr},
              &config_.seat_values[i], true);
    }
  } else {
    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++) {
      const SettingsParamSpec &param = SETTINGS_PARAMS[static_cast<size_t>(i)];
      if (param.page == page) {
        format::StepperSpec spec{param.short_name, param.label,    param.min_value, param.max_value,
                                 param.step,       param.decimals, param.unit};
        spec.names = kSettingParamNames[i];
        // Remapping the stick changes which way a push drives the chair, and
        // the speed how far, so neither is done mid-drive.
        spec.locked_only = i == SETTINGS_PARAM_STICK_INVERT_X ||
                           i == SETTINGS_PARAM_STICK_INVERT_Y || i == SETTINGS_PARAM_STICK_SWAP ||
                           i == SETTINGS_PARAM_DRIVE_SPEED;
        row_add(spec, config_.subjects->value(i), false);
      }
    }
  }
  lv_subject_set_int(&page_, page);
  _ui_screen_change(&ui_SettingsScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_SettingsScreen_screen_init);
}

lv_group_t *hmi::ui::SettingsView::init() {
  // The rejection flash: which actuator was refused, and why. One subject rather
  // than two, because the pair is only ever meaningful together and a single
  // lv_subject_set_int cannot tear. -1 = nothing being flashed.
  lv_subject_init_int(&reject_subject_, REJECT_NONE);
  reject_timer_ = lv_timer_create(reject_clear_cb, REJECT_FLASH_MS, this);
  lv_timer_pause(reject_timer_);
  press_flash_timer_ = lv_timer_create(press_flash_cb, PRESS_FLASH_MS, this);
  lv_timer_pause(press_flash_timer_);
  group_ = lv_group_create();
  return group_;
}

// The row's number. An observer rather than lv_label_bind_text because the
// value is scaled: bind_text's format string is handed the raw integer.
void hmi::ui::SettingsView::value_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *row = static_cast<const Row *>(lv_observer_get_user_data(observer));
  const int32_t raw = lv_subject_get_int(subject);
  if (raw == format::VALUE_UNKNOWN) {
    lv_label_set_text(lv_observer_get_target_obj(observer), "--");
    return;
  }
  char text[24];
  format::stepper_format(row->spec, raw, text);
  lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

// Greys each step button when its end of the range is reached, so the limit is
// visible before the user leans on it. One observer setting both buttons
// rather than two lv_obj_bind_state_if_* bindings: the unknown case has to
// grey BOTH, and two bindings writing the same state would undo each other.
void hmi::ui::SettingsView::limits_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *row = static_cast<const Row *>(lv_observer_get_user_data(observer));
  const int32_t raw = lv_subject_get_int(subject);
  const bool known = raw != format::VALUE_UNKNOWN;
  lv_obj_set_state(row->minus, LV_STATE_DISABLED, !known || raw <= row->spec.min_value);
  lv_obj_set_state(row->plus, LV_STATE_DISABLED, !known || raw >= row->spec.max_value);
}

// The rejection flash: a red wash behind the refused row's number. The
// BACKGROUND, not the text colour: the label's text colour is themed for both
// DEFAULT and FOCUSED, so a red on DEFAULT would lose to the FOCUSED style on
// the very row the user just pressed. Nothing themes this label's background.
void hmi::ui::SettingsView::reject_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *row = static_cast<const Row *>(lv_observer_get_user_data(observer));
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const int32_t packed = lv_subject_get_int(subject);
  const bool rejected = packed != REJECT_NONE && reject_id(packed) == row->index;
  lv_obj_set_style_bg_color(label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(label, rejected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

void hmi::ui::SettingsView::reject_clear_cb(lv_timer_t *timer) {
  lv_timer_pause(timer);
  auto *view = static_cast<SettingsView *>(lv_timer_get_user_data(timer));
  lv_subject_set_int(&view->reject_subject_, REJECT_NONE);
}

// Puts the joystick cursor on `index`, clamped. Clamping rather than wrapping,
// for the reason in grid_key_cb.
void hmi::ui::SettingsView::focus(int index) {
  if (row_count_ == 0) {
    return;
  }
  cursor_ = std::clamp(index, 0, row_count_ - 1);
  lv_group_focus_obj(rows_[cursor_].row);
}

// How long a joystick press shows on a step button: PRESS_FLASH_MS.
//
// The pressed state has to be faked because nothing here is a real touch:
// LVGL only sets it from an indev acting on the object itself, and the
// joystick's indev is acting on the row. One timer and one button, rather than
// a timer per press: a press ends the previous one early, and deleting the
// rows only has to forget one pointer for no timer to outlive its button.
void hmi::ui::SettingsView::press_flash_end() {
  if (press_flash_button_ != nullptr) {
    lv_obj_remove_state(press_flash_button_, LV_STATE_PRESSED);
    press_flash_button_ = nullptr;
  }
  lv_timer_pause(press_flash_timer_);
}

void hmi::ui::SettingsView::press_flash_cb(lv_timer_t *timer) {
  static_cast<SettingsView *>(lv_timer_get_user_data(timer))->press_flash_end();
}

void hmi::ui::SettingsView::press_flash(lv_obj_t *button) {
  press_flash_end();
  lv_obj_add_state(button, LV_STATE_PRESSED);
  press_flash_button_ = button;
  lv_timer_reset(press_flash_timer_);
  lv_timer_resume(press_flash_timer_);
}

// Steps `row` one step in `direction`, and shows the press.
//
// The DISABLED check is not belt-and-braces: LVGL suppresses clicks on a
// disabled object inside the indev, which covers touch, but the joystick path
// reaches the button through lv_obj_send_event and never passes that check.
void hmi::ui::SettingsView::step(const Row *row, int direction) {
  lv_obj_t *button = direction < 0 ? row->minus : row->plus;
  if (lv_obj_has_state(button, LV_STATE_DISABLED)) {
    config_.refuse(); // at its limit
    return;
  }
  if (row->spec.locked_only && lv_subject_get_int(config_.locked) == 0) {
    config_.refuse(); // not while driving
    return;
  }
  press_flash(button);
  if (lv_subject_get_int(&page_) == config_.actuators_page) {
    // A request: the value moves only when the MCB's state sample says so.
    config_.seat_step(static_cast<size_t>(row->index), direction < 0 ? -1 : 1);
    return;
  }
  const int32_t now = lv_subject_get_int(row->value);
  lv_subject_set_int(row->value, std::clamp(now + direction * row->spec.step, row->spec.min_value,
                                            row->spec.max_value));
}

// Up/down walk the rows, left/right step the focused one. Holding the stick
// repeats: lv_indev raises LV_EVENT_KEY again every long_press_repeat_time.
void hmi::ui::SettingsView::key_cb(lv_event_t *e) {
  const auto *row = static_cast<const Row *>(lv_event_get_user_data(e));
  SettingsView *view = row->view;
  switch (lv_event_get_key(e)) {
  case LV_KEY_LEFT:
    view->step(row, -1);
    return;
  case LV_KEY_RIGHT:
    view->step(row, +1);
    return;
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

// A tap on a step button. Moves the cursor to that row first, so touch and the
// joystick never disagree about which row left/right would act on.
void hmi::ui::SettingsView::step_click_cb(lv_event_t *e) {
  const auto *row = static_cast<const Row *>(lv_event_get_user_data(e));
  row->view->focus(row->index);
  row->view->step(row, lv_event_get_target_obj(e) == row->minus ? -1 : +1);
}

// A tap on the row itself: focus, nothing else.
void hmi::ui::SettingsView::row_click_cb(lv_event_t *e) {
  const auto *row = static_cast<const Row *>(lv_event_get_user_data(e));
  row->view->focus(row->index);
}

// Registered for LV_EVENT_FOCUSED and LV_EVENT_DEFOCUSED on each row. The step
// buttons keep their DISABLED state through this: DISABLED and FOCUSED are
// different bits, and only limits_observer writes the one.
void hmi::ui::SettingsView::focus_cb(lv_event_t *e) {
  set_focused_recursive(lv_event_get_target_obj(e), lv_event_get_code(e) == LV_EVENT_FOCUSED);
}

// Deleting a row takes its observers and events with it (all bound to the
// object) and drops it from the group; the theme manager forgets it too.
void hmi::ui::SettingsView::rows_clear() {
  press_flash_end(); // before its button goes
  for (int i = 0; i < row_count_; i++) {
    lv_obj_delete(rows_[i].row);
  }
  row_count_ = 0;
  cursor_ = 0;
}

void hmi::ui::SettingsView::row_add(const format::StepperSpec &spec, lv_subject_t *value,
                                    bool actuator) {
  Row &entry = rows_[row_count_];
  entry.spec = spec;
  entry.value = value;
  entry.index = row_count_++;
  entry.view = this;
  entry.row = ui_SettingRow_create(ui_SpecificSettingsRows);
  entry.value_label = ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_VALUEBOX_VALUE);
  entry.minus = ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_MINUS);
  entry.plus = ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_PLUS);

  // Fixed for the life of the row - they come from a build-time table - so
  // set once rather than driven through a subject.
  lv_label_set_text(ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_HEAD_NAMES_SHORT),
                    spec.short_name);
  lv_label_set_text(ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_HEAD_NAMES_LABEL), spec.label);

  lv_subject_add_observer_obj(value, value_observer, entry.value_label, &entry);
  lv_subject_add_observer_obj(value, limits_observer, entry.row, &entry);
  if (actuator) {
    lv_subject_add_observer_obj(&reject_subject_, reject_observer, entry.value_label, &entry);
  }

  lv_group_add_obj(group_, entry.row);
  lv_obj_add_event_cb(entry.row, key_cb, LV_EVENT_KEY, &entry);
  lv_obj_add_event_cb(entry.row, row_click_cb, LV_EVENT_CLICKED, &entry);
  lv_obj_add_event_cb(entry.row, focus_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(entry.row, focus_cb, LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(entry.minus, step_click_cb, LV_EVENT_CLICKED, &entry);
  lv_obj_add_event_cb(entry.plus, step_click_cb, LV_EVENT_CLICKED, &entry);

  // SquareLine exports the state each object was previewed in, and the
  // component was drawn focused throughout - so every row would come up
  // focused at once. Clearing it leaves focus_cb as its only writer.
  set_focused_recursive(entry.row, false);
  clear_click_focusable_recursive(entry.row);
}
