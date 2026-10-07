// split_main.py fragment of main.cpp (frag_settings_ui: original lines 2152-2781); one TU, see main.cpp.
#include "hmi_format/stepper.hpp"
/////////////////////////////////////////////////////////////////////////////
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
//   settings   settings_spec.h. The step sets the row's subject, clamped to
//              the range; whatever owns that subject applies and saves it.
//   actuators  RAMMP_SEAT_AXIS_TABLE, reached through the PIN (DEBUG
//              ACTUATORS). The HMI owns none of these values: a step publishes a
//              SeatCommand, and a row's number changes only when MibStatus says
//              so. That keeps a limit or an interlock one decision made in one
//              place, instead of two boards disagreeing about where the seat is.
//
// Memory: the screen and its rows exist only while it is up - see "Screens
// built on demand".
/////////////////////////////////////////////////////////////////////////////

// A row's fixed description, from either table, and how it is drawn: the
// formatters live in components/hmi_format (REQ-FMT-02..05).
using hmi::format::seat_format;
using hmi::format::stepper_format;
using hmi::format::StepperSpec;

// Everything needed to drive one row, so a key callback or an observer gets it
// all from one user_data pointer.
struct SettingRow {
  StepperSpec spec;
  lv_subject_t *value; // the row's whole truth; kValueUnknown until known
  int index;           // position on the page; on the actuators page, the actuator id
  lv_obj_t *row;       // the SettingRow root; what takes focus
  lv_obj_t *value_label;
  lv_obj_t *minus;
  lv_obj_t *plus;
};

struct SettingPageText {
  const char *title;
  const char *instructions;
};

static constexpr SettingPageText kActuatorsPageText{"DEBUG ACTUATORS",
                                                    "Up/down to pick, left/right or -/+ to move."};

// The subject each settings parameter steps, in SETTINGS_PARAM_* order. Each
// one's observer applies and saves it.
static lv_subject_t *const kSettingParamValue[] = {
    &brightness_subject,        // SETTINGS_PARAM_BRIGHTNESS
    &theme_subject,             // SETTINGS_PARAM_THEME
    &menu_slide_subject,        // SETTINGS_PARAM_MENU_SLIDE
    &flip_subject,              // SETTINGS_PARAM_FLIP
    &stick_sensitivity_subject, // SETTINGS_PARAM_STICK_SENSITIVITY
    &drive_speed_subject,       // SETTINGS_PARAM_DRIVE_SPEED
    &stick_invert_x_subject,    // SETTINGS_PARAM_STICK_INVERT_X
    &stick_invert_y_subject,    // SETTINGS_PARAM_STICK_INVERT_Y
    &stick_swap_subject,        // SETTINGS_PARAM_STICK_SWAP
    &sounds_subject,            // SETTINGS_PARAM_SOUNDS
    &network_subject,           // SETTINGS_PARAM_NETWORK
};

// What the named rows read, in SETTINGS_PARAM_* order; nullptr = a number.
static constexpr const char *kThemeNames[] = {"Dark", "Day"};
static constexpr const char *kOnOffNames[] = {"Off", "On"};
static constexpr const char *kMirrorNames[] = {"Normal", "Mirror"};
static constexpr const char *kSwapNames[] = {"Normal", "Swap"};
// NetLink order. Not a Settings row: Internet Settings draws it as two buttons.
static constexpr const char *kNetworkNames[] = {"Ethernet", "WiFi"};
static const char *const *const kSettingParamNames[] = {
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
              "every settings_spec.h parameter needs its names (or nullptr) here");
static_assert(std::size(kSettingParamValue) == SETTINGS_PARAM_COUNT,
              "every settings_spec.h parameter needs its subject here");

static constexpr int kSettingRowsMax = std::max<int>(rammp::kSeatAxisCount, SETTINGS_PARAM_COUNT);
static SettingRow setting_rows[kSettingRowsMax];
static int setting_row_count; // rows on the page that is up; 0 while the screen is not
static int setting_cursor;    // which row the joystick is on
static lv_group_t *setting_group = nullptr;
// Which page is up: a SETTINGS_PAGE_* or kActuatorsPage. A subject because the
// warning panel depends on it.
static lv_subject_t setting_page_subject;

// Raw value per actuator, in the table's units. Static: the rows' observers
// point at them, and the MCB keeps them current with no page up.
static lv_subject_t seat_axis_value[rammp::kSeatAxisCount];
static uint8_t seat_axis_count; // actuators in the table

// What a value reads before it is known - only ever an actuator the MCB has
// not reported yet. Not zero: zero is a position an actuator can genuinely be
// in, and drawing it would be claiming knowledge we do not have. Both buttons
// stay greyed while a row reads this, because commanding an actuator whose
// position is unknown is exactly what the request/response arrangement avoids.
static constexpr int32_t kValueUnknown = hmi::format::VALUE_UNKNOWN;

// The rejection flash: which actuator was refused, and why. One subject rather
// than two, because the pair is only ever meaningful together and a single
// lv_subject_set_int cannot tear. -1 = nothing being flashed.
static lv_subject_t actuator_reject_subject;
static constexpr int32_t kActuatorRejectNone = -1;
static constexpr uint32_t kActuatorRejectFlashMs = 900;
static uint8_t actuator_reject_id(int32_t packed) { return static_cast<uint8_t>(packed & 0xFF); }

static void set_display_flipped(bool on); // screen flip, beside direct_flush_cb

// Applies and saves one of the Settings rows that has no observer of its
// own (brightness and theme do). user_data is its SETTINGS_PARAM_*.
static void setting_store_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const int param =
      static_cast<int>(reinterpret_cast<intptr_t>(lv_observer_get_user_data(observer)));
  const int value = lv_subject_get_int(subject);
  settings_set(param, value);
  switch (param) {
  case SETTINGS_PARAM_FLIP:
    set_display_flipped(value != 0);
    break;
  case SETTINGS_PARAM_STICK_SENSITIVITY:
    stick_sensitivity.store(value);
    break;
  case SETTINGS_PARAM_DRIVE_SPEED:
    drive_speed.store(value);
    break;
  case SETTINGS_PARAM_STICK_INVERT_X:
    stick_invert_x.store(value != 0);
    break;
  case SETTINGS_PARAM_STICK_INVERT_Y:
    stick_invert_y.store(value != 0);
    break;
  case SETTINGS_PARAM_STICK_SWAP:
    stick_swap.store(value != 0);
    break;
  case SETTINGS_PARAM_SOUNDS:
    sounds_on.store(value != 0);
    break;
  default:
    break; // MENU_SLIDE: read where it is used, nothing to apply
  }
}

// How each seat axis' number is drawn, filled in at startup from the shared
// table. Static because the observers below hold a pointer into it.
static StepperSpec seat_axis_format[rammp::kSeatAxisCount];

// The adjustment page's two numbers: where the selected axis is, and how far it
// goes. Written here rather than bound, because which subject they show changes
// with the function button - see seat_angle_observer.
//
// Spec 04b: the big one is the number, with "°" on an angle -- the word "deg"
// at 264 px was most of the screen -- and the small one says "of 25°", or
// "of 50.0 mm" on a length, where the unit has no symbol to shrink to.
static void seat_angle_refresh() {
  if (seat_selected_axis < 0) {
    return;
  }
  const StepperSpec &spec = seat_axis_format[seat_selected_axis];
  const int32_t raw = lv_subject_get_int(&seat_axis_value[seat_selected_axis]);
  char text[32];
  hmi::format::seat_reading_text(spec, raw, text);
  lv_label_set_text(ui_AngleLabel, text);

  char footer[40];
  hmi::format::seat_range_text(spec, footer);
  lv_label_set_text(ui_MaxAngleLabel, footer);

  // A long reading ("-42.5°") at 264 px runs into the "of": shrink it, about
  // its bottom-left corner, to fit what the "of" leaves.
  const lv_font_t *font = lv_obj_get_style_text_font(ui_AngleLabel, LV_PART_MAIN);
  const lv_font_t *small = lv_obj_get_style_text_font(ui_MaxAngleLabel, LV_PART_MAIN);
  auto width_of = [](const char *s, const lv_font_t *f) {
    lv_point_t size;
    lv_text_get_size(&size, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
  };
  const int32_t wide = width_of(text, font);
  const int32_t room = 660 - width_of(footer, small) - 24;
  const int32_t scale =
      wide > room && wide > 0 ? LV_SCALE_NONE * std::max<int32_t>(room, 1) / wide : LV_SCALE_NONE;
  lv_obj_set_style_transform_pivot_x(ui_AngleLabel, 0, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(ui_AngleLabel, lv_pct(100), LV_PART_MAIN);
  lv_obj_set_style_transform_scale(ui_AngleLabel, scale, LV_PART_MAIN);

  // The preset the motion sits at is the selected one: the negative (spec
  // 04b, "15°" filled).
  for (lv_obj_t *preset :
       {ui_SeatAdjustmentButton3, ui_SeatAdjustmentButton4, ui_SeatAdjustmentButton5}) {
    const lv_obj_t *label = preset == ui_SeatAdjustmentButton3   ? ui_SeatAdjustmentButtonLabel3
                            : preset == ui_SeatAdjustmentButton4 ? ui_SeatAdjustmentButtonLabel4
                                                                 : ui_SeatAdjustmentButtonLabel5;
    lv_obj_set_state(preset, LV_STATE_CHECKED,
                     raw != kValueUnknown &&
                         seat_preset_target(static_cast<size_t>(seat_selected_axis), label) == raw);
  }
}

// One per axis, all pointed at ui_AngleLabel: whichever axis moves, the page
// redraws the one it is showing. Cheaper than it looks - a sample only arrives
// twice a second, and refreshing costs two lv_label_set_text.
static void seat_angle_observer(lv_observer_t *, lv_subject_t *) { seat_angle_refresh(); }

// The number under a function button. An observer for the same reason
// setting_value_observer is one: the value is scaled by its decimals.
static void seat_button_value_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *spec = static_cast<const StepperSpec *>(lv_observer_get_user_data(observer));
  char text[32];
  seat_format(*spec, lv_subject_get_int(subject), text);
  lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

// The row's number. An observer rather than lv_label_bind_text because the
// value is scaled: bind_text's format string is handed the raw integer.
static void setting_value_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *row = static_cast<const SettingRow *>(lv_observer_get_user_data(observer));
  const int32_t raw = lv_subject_get_int(subject);
  if (raw == kValueUnknown) {
    lv_label_set_text(lv_observer_get_target_obj(observer), "--");
    return;
  }
  char text[24];
  stepper_format(row->spec, raw, text);
  lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

// Greys each step button when its end of the range is reached, so the limit is
// visible before the user leans on it. One observer setting both buttons
// rather than two lv_obj_bind_state_if_* bindings: the unknown case has to
// grey BOTH, and two bindings writing the same state would undo each other.
static void setting_limits_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *row = static_cast<const SettingRow *>(lv_observer_get_user_data(observer));
  const int32_t raw = lv_subject_get_int(subject);
  const bool known = raw != kValueUnknown;
  lv_obj_set_state(row->minus, LV_STATE_DISABLED, !known || raw <= row->spec.min_value);
  lv_obj_set_state(row->plus, LV_STATE_DISABLED, !known || raw >= row->spec.max_value);
}

// The rejection flash: a red wash behind the refused row's number. The
// BACKGROUND, not the text colour: the label's text colour is themed for both
// DEFAULT and FOCUSED, so a red on DEFAULT would lose to the FOCUSED style on
// the very row the user just pressed. Nothing themes this label's background.
static void actuator_reject_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const auto *row = static_cast<const SettingRow *>(lv_observer_get_user_data(observer));
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const int32_t packed = lv_subject_get_int(subject);
  const bool rejected = packed != kActuatorRejectNone && actuator_reject_id(packed) == row->index;
  lv_obj_set_style_bg_color(label, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(label, rejected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
}

static void actuator_reject_clear_cb(lv_timer_t *timer) {
  lv_timer_pause(timer);
  lv_subject_set_int(&actuator_reject_subject, kActuatorRejectNone);
}

static lv_timer_t *actuator_reject_timer = nullptr;

// Seat values come from the MIB and nowhere else: a press publishes a request, and the
// number on screen moves only when the next MibStatus says the seat moved. The wire
// carries whole units, the screens raw integers, so each field is converted here.
static void seat_apply_state(const MIB::seatState &seat) {
  for (uint8_t i = 0; i < seat_axis_count; i++) {
    const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[i];
    lv_subject_set_int(&seat_axis_value[i],
                       rammp::seat_raw(spec, rammp::seat_field(seat, spec.id)));
  }
}

// One seat request: an absolute target, clamped to the axis' range, so a lost or repeated
// message cannot drift the seat.
static void seat_request(rammp::SeatAxis axis, int32_t target) {
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[rammp::index_of(axis)];
  rtps_comms_publish_seat(
      axis, rammp::seat_units(spec, std::clamp(target, spec.min_value, spec.max_value)));
}

// One step up or down from where the MCB last said the axis is.
static void seat_step(size_t row, int direction) {
  const rammp::SeatAxisSpec &spec = rammp::kSeatAxes[row];
  const int32_t now = lv_subject_get_int(&seat_axis_value[row]);
  const int32_t from = now == kValueUnknown ? spec.min_value : now;
  seat_request(spec.id, from + direction * spec.step);
}

// Puts the joystick cursor on `index`, clamped. Clamping rather than wrapping,
// for the reason in grid_key_cb.
static void setting_focus(int index) {
  if (setting_row_count == 0) {
    return;
  }
  setting_cursor = std::clamp(index, 0, setting_row_count - 1);
  lv_group_focus_obj(setting_rows[setting_cursor].row);
}

// How long a joystick press shows on a step button. Comfortably shorter than
// the 250 ms key repeat, so holding the stick reads as one blink per step.
//
// The pressed state has to be faked because nothing here is a real touch:
// LVGL only sets it from an indev acting on the object itself, and the
// joystick's indev is acting on the row. One timer and one button, rather than
// a timer per press: a press ends the previous one early, and deleting the
// rows only has to forget one pointer for no timer to outlive its button.
static constexpr uint32_t kPressFlashMs = 120;
static lv_timer_t *press_flash_timer = nullptr;
static lv_obj_t *press_flash_button = nullptr;

static void press_flash_end() {
  if (press_flash_button != nullptr) {
    lv_obj_remove_state(press_flash_button, LV_STATE_PRESSED);
    press_flash_button = nullptr;
  }
  lv_timer_pause(press_flash_timer);
}

static void press_flash_cb(lv_timer_t *) { press_flash_end(); }

static void press_flash(lv_obj_t *button) {
  press_flash_end();
  lv_obj_add_state(button, LV_STATE_PRESSED);
  press_flash_button = button;
  lv_timer_reset(press_flash_timer);
  lv_timer_resume(press_flash_timer);
}

// Steps `row` one step in `direction`, and shows the press.
//
// The DISABLED check is not belt-and-braces: LVGL suppresses clicks on a
// disabled object inside the indev, which covers touch, but the joystick path
// reaches the button through lv_obj_send_event and never passes that check.
static void setting_step(const SettingRow *row, int direction) {
  lv_obj_t *button = direction < 0 ? row->minus : row->plus;
  if (lv_obj_has_state(button, LV_STATE_DISABLED)) {
    refusal_feedback(); // at its limit
    return;
  }
  if (row->spec.locked_only && lv_subject_get_int(&locked_subject) == 0) {
    refusal_feedback(); // not while driving
    return;
  }
  press_flash(button);
  if (lv_subject_get_int(&setting_page_subject) == kActuatorsPage) {
    // A request: the value moves only when the MCB's state sample says so.
    seat_step(static_cast<size_t>(row->index), direction < 0 ? -1 : 1);
    return;
  }
  const int32_t now = lv_subject_get_int(row->value);
  lv_subject_set_int(row->value, std::clamp(now + direction * row->spec.step, row->spec.min_value,
                                            row->spec.max_value));
}

// Up/down walk the rows, left/right step the focused one. Holding the stick
// repeats: lv_indev raises LV_EVENT_KEY again every long_press_repeat_time.
static void setting_key_cb(lv_event_t *e) {
  const auto *row = static_cast<const SettingRow *>(lv_event_get_user_data(e));
  switch (lv_event_get_key(e)) {
  case LV_KEY_LEFT:
    setting_step(row, -1);
    return;
  case LV_KEY_RIGHT:
    setting_step(row, +1);
    return;
  case LV_KEY_UP:
    setting_focus(setting_cursor - 1);
    return;
  case LV_KEY_DOWN:
    if (setting_cursor + 1 >= setting_row_count) {
      nav_to_key(); // below the last row is the burger key
      return;
    }
    setting_focus(setting_cursor + 1);
    return;
  default:
    return;
  }
}

// A tap on a step button. Moves the cursor to that row first, so touch and the
// joystick never disagree about which row left/right would act on.
static void setting_step_click_cb(lv_event_t *e) {
  const auto *row = static_cast<const SettingRow *>(lv_event_get_user_data(e));
  setting_focus(row->index);
  setting_step(row, lv_event_get_target_obj(e) == row->minus ? -1 : +1);
}

// A tap on the row itself: focus, nothing else.
static void setting_row_click_cb(lv_event_t *e) {
  setting_focus(static_cast<const SettingRow *>(lv_event_get_user_data(e))->index);
}

// LVGL's click-focus (indev_click_focus in lv_indev.c) remembers the last
// CLICK_FOCUSABLE object a pointer pressed and DEFOCUSES it when the next one
// is pressed. Every lv_obj carries that flag by default, so the step buttons
// took part in it despite being in no group: tapping - and then + sent
// LV_EVENT_DEFOCUSED to -, and lv_obj_event strips LV_STATE_FOCUSED on that
// event, so a button inside a still-focused row dropped out of the focused
// theme. Nothing inside a row should have a focus life of its own, so the flag
// comes off every descendant and setting_focus_cb is left as the only writer
// of LV_STATE_FOCUSED in there. Descendants only: the row itself has to stay
// click-focusable, because that is how a tap on it reaches its group.
static void clear_click_focusable_recursive(lv_obj_t *obj) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    lv_obj_remove_flag(child, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    clear_click_focusable_recursive(child);
  }
}

// A focused row has to look focused all the way down. LVGL only puts
// LV_STATE_FOCUSED on the object the group focused - the row itself - but the
// export gives every descendant its own MAIN|FOCUSED theme style, so without
// this the row's background would invert while the labels and the -/+ buttons
// on it stayed in the unfocused theme and became unreadable.
static void set_focused_recursive(lv_obj_t *obj, bool focused) {
  if (focused) {
    lv_obj_add_state(obj, LV_STATE_FOCUSED);
  } else {
    lv_obj_remove_state(obj, LV_STATE_FOCUSED);
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    set_focused_recursive(lv_obj_get_child(obj, i), focused);
  }
}

// Registered for LV_EVENT_FOCUSED and LV_EVENT_DEFOCUSED on each row. The step
// buttons keep their DISABLED state through this: DISABLED and FOCUSED are
// different bits, and only setting_limits_observer writes the one.
static void setting_focus_cb(lv_event_t *e) {
  set_focused_recursive(lv_event_get_target_obj(e), lv_event_get_code(e) == LV_EVENT_FOCUSED);
}

// ErrorBanner6. Raised only on a page that needs the MCB - the
// actuators - and then exactly as on the drive and seat screens: while the
// link is down or the MCB's state is not OK.
static void setting_warning_observer(lv_observer_t *observer, lv_subject_t *) {
  lv_obj_t *panel = lv_observer_get_target_obj(observer);
  if (lv_subject_get_int(&setting_page_subject) != kActuatorsPage || seat_ready()) {
    banner_show(panel, false);
    return;
  }
  fill_drive_blocked_panel(panel, rammp::kHmiLinkLostTitle, rammp::kHmiMcbFaultTitle);
  banner_show(panel, true);
}

// Deleting a row takes its observers and events with it (all bound to the
// object) and drops it from setting_group; the theme manager forgets it too.
static void setting_rows_clear() {
  press_flash_end(); // before its button goes
  for (int i = 0; i < setting_row_count; i++) {
    lv_obj_delete(setting_rows[i].row);
  }
  setting_row_count = 0;
  setting_cursor = 0;
}

static void setting_row_add(const StepperSpec &spec, lv_subject_t *value, bool actuator) {
  SettingRow &entry = setting_rows[setting_row_count];
  entry.spec = spec;
  entry.value = value;
  entry.index = setting_row_count++;
  entry.row = ui_SettingRow_create(ui_SpecificSettingsRows);
  entry.value_label = ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_VALUEBOX_VALUE);
  entry.minus = ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_MINUS);
  entry.plus = ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_PLUS);

  // Fixed for the life of the row - they come from a build-time table - so
  // set once rather than driven through a subject.
  lv_label_set_text(ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_HEAD_NAMES_SHORT),
                    spec.short_name);
  lv_label_set_text(ui_comp_get_child(entry.row, UI_COMP_SETTINGROW_HEAD_NAMES_LABEL), spec.label);

  lv_subject_add_observer_obj(value, setting_value_observer, entry.value_label, &entry);
  lv_subject_add_observer_obj(value, setting_limits_observer, entry.row, &entry);
  if (actuator) {
    lv_subject_add_observer_obj(&actuator_reject_subject, actuator_reject_observer,
                                entry.value_label, &entry);
  }

  lv_group_add_obj(setting_group, entry.row);
  lv_obj_add_event_cb(entry.row, setting_key_cb, LV_EVENT_KEY, &entry);
  lv_obj_add_event_cb(entry.row, setting_row_click_cb, LV_EVENT_CLICKED, &entry);
  lv_obj_add_event_cb(entry.row, setting_focus_cb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(entry.row, setting_focus_cb, LV_EVENT_DEFOCUSED, nullptr);
  lv_obj_add_event_cb(entry.minus, setting_step_click_cb, LV_EVENT_CLICKED, &entry);
  lv_obj_add_event_cb(entry.plus, setting_step_click_cb, LV_EVENT_CLICKED, &entry);

  // SquareLine exports the state each object was previewed in, and the
  // component was drawn focused throughout - so every row would come up
  // focused at once. Clearing it leaves setting_focus_cb as its only writer.
  set_focused_recursive(entry.row, false);
  clear_click_focusable_recursive(entry.row);
}

// Fills the screen for `page` and shows it. LVGL task (a click handler).
static void setting_page_open(int32_t page) {
  settings_screen_ensure(); // built on demand: see "Screens built on demand"
  setting_rows_clear();
  const SettingPageText text =
      page == kActuatorsPage
          ? kActuatorsPageText
          : SettingPageText{SETTINGS_PAGES[static_cast<size_t>(page)].title,
                            SETTINGS_PAGES[static_cast<size_t>(page)].instructions};
  lv_label_set_text(ui_SettingsTitle, text.title);
  // text.instructions has nowhere to go yet: the spec-V2 body is a title and
  // the rows, with no line between them. Phase 5 adds one when it lays this
  // screen out properly.
  if (page == kActuatorsPage) {
    const auto &specs = rammp::kSeatAxes;
    for (uint8_t i = 0; i < seat_axis_count; i++) {
      setting_row_add({specs[i].short_name, specs[i].label, specs[i].min_value, specs[i].max_value,
                       specs[i].step, specs[i].decimals, nullptr},
                      &seat_axis_value[i], true);
    }
  } else {
    for (int i = 0; i < SETTINGS_PARAM_COUNT; i++) {
      const SettingsParamSpec &param = SETTINGS_PARAMS[static_cast<size_t>(i)];
      if (param.page == page) {
        StepperSpec spec{param.short_name, param.label, param.min_value, param.max_value,
                         param.step,       param.decimals, param.unit};
        spec.names = kSettingParamNames[i];
        // Remapping the stick changes which way a push drives the chair, and
        // the speed how far, so neither is done mid-drive.
        spec.locked_only = i == SETTINGS_PARAM_STICK_INVERT_X ||
                           i == SETTINGS_PARAM_STICK_INVERT_Y || i == SETTINGS_PARAM_STICK_SWAP ||
                           i == SETTINGS_PARAM_DRIVE_SPEED;
        setting_row_add(spec, kSettingParamValue[i], false);
      }
    }
  }
  lv_subject_set_int(&setting_page_subject, page);
  _ui_screen_change(&ui_SettingsScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_SettingsScreen_screen_init);
}

