// split_main.py fragment of main.cpp (frag_drive_band: original lines 567-638); one TU, see main.cpp.
#include "hmi_format/speed.hpp"
/////////////////////////////////////////////////////////////////////////////
// DriveScreen: drive-mode selection
//
// Three plain LVGL buttons, so they are already touch-clickable; all this adds
// is what a tap means and which one looks selected. Deliberately touch-only:
// the joystick is busy driving on this screen, and stealing left/right for menu
// navigation is exactly the class of bug that made pulling back exit the
// screen.
/////////////////////////////////////////////////////////////////////////////

// Paired with the button that selects it, so one callback serves all three.
static MIB::DriveProfile kProfileHigh = MIB::DriveProfile::HIGH;
static MIB::DriveProfile kProfileNormal = MIB::DriveProfile::NORMAL;
static MIB::DriveProfile kProfileLow = MIB::DriveProfile::LOW;

// The drive session's input, defined in frag_drive.inc.
static bool drive_session_input(hmi::drive_session::Input input);

static void drive_profile_click_cb(lv_event_t *e) {
  const auto *mode = static_cast<const MIB::DriveProfile *>(lv_event_get_user_data(e));
  // Ask, do not assume: drive_profile_subject is what the MIB reports, and it is fed
  // from MibStatus.activeProfile. Setting it here would highlight a button the chair
  // may refuse, which is the one thing these three must not do.
  drive_profile_published.store(*mode);
  // PUBLISH_DRIVE: the drive request as it stands, with the new profile (rows 29-34).
  (void)drive_timed(TimedPath::PROFILE_CLICK, drive_session_input,
                    hmi::drive_session::Input::PROFILE_CLICK);
}

// Highlights the button whose mode the MIB reports: CHECKED, which the design
// draws as the negative of the resting button (spec: Button states), themed
// both ways. All three are drawn at rest, so nothing is lit until the MIB says.
static void drive_mode_button_observer(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *button = lv_observer_get_target_obj(observer);
  const auto mine = *static_cast<const MIB::DriveProfile *>(lv_observer_get_user_data(observer));
  const bool selected = static_cast<MIB::DriveProfile>(lv_subject_get_int(subject)) == mine;
  lv_obj_set_state(button, LV_STATE_CHECKED, selected);
}

static void bind_drive_profile_button(lv_obj_t *button, MIB::DriveProfile *mode) {
  if (button == nullptr) {
    return;
  }
  lv_obj_add_event_cb(button, drive_profile_click_cb, LV_EVENT_CLICKED, mode);
  // The label takes the inverted colour with the button (pressed and checked).
  nav_mirror_states(button);
  lv_subject_add_observer_obj(&drive_profile_subject, drive_mode_button_observer, button, mode);
}

// Mirrors what the MIB reports out to the ADC task, which cannot take the LVGL lock,
// so the next DriveCommand carries the profile the chair actually took. It does not
// publish: the subject is fed from MibStatus now, and republishing on every sample
// would put a DriveCommand on the wire twice a second.
static void drive_mode_publish_observer(lv_observer_t *, lv_subject_t *subject) {
  drive_profile_published.store(static_cast<MIB::DriveProfile>(lv_subject_get_int(subject)));
}

// MibStatus.speed is metres per second; the label shows mph to one decimal.
// Any task: pure arithmetic, no LVGL (components/hmi_format, REQ-FMT-01).
using hmi::format::speed_display_tenths;
static_assert(hmi::format::MPH_PER_MPS == rammp::kMphPerMps,
              "hmi_format's mph per m/s must be the shared spec's");
static_assert(hmi::format::SPEED_MAX_TENTHS == rammp::kSpeedMaxTenths,
              "hmi_format's speed clamp must be the shared spec's");

// The speed reaches the subject as tenths; the label wants "N.N". No built-in
// binding formats an integer that way, so hmi_format does the divide.
static void speed_label_observer(lv_observer_t *observer, lv_subject_t *subject) {
  char text[8]; // "9.9" at most: the value is clamped to 0..SPEED_MAX_TENTHS
  hmi::format::speed_text(lv_subject_get_int(subject), text);
  lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

