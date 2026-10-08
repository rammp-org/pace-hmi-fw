// split_main.py fragment of main.cpp (frag_settings_ui: original lines 2152-2781); one TU, see main.cpp.
#include "hmi_format/stepper.hpp"
#include "hmi_ui/settings_view.hpp"
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
/////////////////////////////////////////////////////////////////////////////

// A row's fixed description, from either table, and how it is drawn: the
// formatters live in components/hmi_format (REQ-FMT-02..05).
using hmi::format::seat_format;
using hmi::format::stepper_format;
using hmi::format::StepperSpec;

struct SettingPageText {
  const char *title;
  const char *instructions;
};

static constexpr SettingPageText kActuatorsPageText{"DEBUG ACTUATORS",
                                                    "Up/down to pick, left/right or -/+ to move."};

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
              "every settings_spec.hpp parameter needs its names (or nullptr) here");

static constexpr int kSettingRowsMax = std::max<int>(rammp::kSeatAxisCount, SETTINGS_PARAM_COUNT);
static_assert(kSettingRowsMax <= hmi::ui::SettingsView::ROWS_MAX, "a page has more rows than the view");
static lv_group_t *setting_group = nullptr;
// Which page is up: a SETTINGS_PAGE_* or kActuatorsPage. A subject because the
// warning panel depends on it.
static lv_subject_t setting_page_subject;

// Raw value per actuator, in the table's units: seat_axis_value, defined with
// the seat view (frag_seat). The rows' observers point at them, and the MCB
// keeps them current with no page up.
static uint8_t seat_axis_count; // actuators in the table

// What a value reads before it is known - only ever an actuator the MCB has
// not reported yet. Not zero: zero is a position an actuator can genuinely be
// in, and drawing it would be claiming knowledge we do not have. Both buttons
// stay greyed while a row reads this, because commanding an actuator whose
// position is unknown is exactly what the request/response arrangement avoids.
static constexpr int32_t kValueUnknown = hmi::format::VALUE_UNKNOWN;

static void set_display_flipped(bool on); // screen flip, beside direct_flush_cb

// Applies and saves one of the Settings rows that has no observer of its
// own (brightness and theme do). user_data is its SETTINGS_PARAM_*.
static void setting_store_observer(lv_observer_t *observer, lv_subject_t *subject) {
  const int param =
      static_cast<int>(reinterpret_cast<intptr_t>(lv_observer_get_user_data(observer)));
  const int value = lv_subject_get_int(subject);
  settings_set(param, value);
  if (param == SETTINGS_PARAM_FLIP) {
    set_display_flipped(value != 0);
    return;
  }
  // The stick's and the sounds' atomics; MENU_SLIDE is read where it is used, nothing to
  // apply.
  (void)applied_settings.apply(param, value);
}

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

// The one instance. The pages' contents (the settings tables, the actuator
// rows, setting_page_open) and what a setting does when it changes
// (setting_store_observer) stay in main.
static constinit hmi::ui::SettingsView settings_view{{
    .nav = &ui_nav_port,
    .page = &setting_page_subject,
    .locked = &locked_subject,
    .actuators_page = kActuatorsPage,
    .seat_step = seat_step,
    .refuse = refusal_feedback,
}};
static void setting_focus(int index) { settings_view.focus(index); }
static constexpr lv_event_cb_t setting_focus_cb = hmi::ui::SettingsView::focus_cb;
static void setting_rows_clear() { settings_view.rows_clear(); }

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
      settings_view.row_add({specs[i].short_name, specs[i].label, specs[i].min_value, specs[i].max_value,
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
        settings_view.row_add(spec, setting_subjects.value(i), false);
      }
    }
  }
  lv_subject_set_int(&setting_page_subject, page);
  _ui_screen_change(&ui_SettingsScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0,
                    &ui_SettingsScreen_screen_init);
}

