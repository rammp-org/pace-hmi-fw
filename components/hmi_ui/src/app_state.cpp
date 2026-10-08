/// @file app_state.cpp
/// @brief The main unit's shared state (app_state.hpp), moved verbatim from main/frag_state.inc.
///
/// Legacy debt carried by ratchet transfer grants (mutable_globals): each variable moves on to
/// its owner later (app-main-shrink.md S7, T-H4a). All constant-initialised: no global
/// constructor (G4).

#include "hmi_ui/app_state.hpp"

// The Joystick test screen's readouts (axis bars, button count) own their subjects.
constinit hmi::ui::JoystickView joystick_view;

// Joystick button, mirrored out of the LVGL world so the ADC task can publish
// it alongside the stick position without taking the LVGL lock.
std::atomic<bool> joy_button_pressed{false};

// Which drive mode the user picked on the DriveScreen (DriveBandView's profile
// subject shows what the MIB took). The HMI reports it to the MCB with every
// DriveCommand; an atomic for the same reason as the button: the ADC task must
// not take the LVGL lock to read it.
std::atomic<MIB::DriveProfile> drive_profile_published{MIB::DriveProfile::NORMAL};

// 0/1 blink phase for the indicator, flipped by the same poll timer
lv_subject_t rtps_blink_subject;

// Joystick -> LVGL keypad. Held at the *level* the stick is at: whichever
// LV_KEY_* direction it is deflected toward, or 0 when centered. Reporting a
// held key rather than a one-shot latch is what lets LVGL's own key repeat
// ([lv_indev.c] re-sends the key every long_press_repeat_time) walk the
// settings list while the stick stays pushed.
std::atomic<uint32_t> joy_key{0};

// GPIO48 "select" stays edge-latched, unlike the directions: repeating ENTER
// would re-click the focused row every repeat period, which for the theme-toggle
// row means it strobes. Consumed by the indev read.
std::atomic<bool> select_key{false};
// A direction held by the remote UI channel (remote_ui.cpp); 0 = none. The ADC
// task recomputes joy_key from the real stick every cycle, so a direction
// written straight into joy_key lasted at most 33 ms and a keypad read could
// miss it. This one outlasts that until the channel lets go.
std::atomic<uint32_t> remote_key{0};
// The last direction the stick engaged, kept until the keypad has read it. A
// quick flick can engage and release inside one ADC period plus one keypad read,
// and the read would then only ever see joy_key at rest -- a flick the stick
// felt and the UI never did.
std::atomic<uint32_t> joy_flick{0};

// The Settings rows' subjects, one per SETTINGS_PARAM_* (hmi_ui). Up here because
// the backlight, rtps_poll_cb (the theme row), the menu (the slide) and the
// Internet screen (the network) read them. Each owner's observer applies and
// saves its value.
constinit hmi::ui::SettingSubjects setting_subjects;

// What the ADC task and the click sound need of those, as atomics they can read
// without the LVGL lock (components/settings). Written by setting_store_observer.
constinit hmi::settings::AppliedSettings applied_settings;

// The burger menu's overlay while it is up, else null. Up here because the
// hold gestures ask whether it is open before they apply.
lv_obj_t *nav_menu_open = nullptr;

// Whether the stick may drive the chair right now: the Drive screen is up, the
// menu is shut and drive is active. Written on the LVGL task by
// nav_update_stick_gate, read by the ADC task, which sends the MCB a centred
// stick whenever it is false.
std::atomic<bool> stick_drives{false};

// Open the menu on the next screen to load: the burger key on Drive asks the
// MIB to stop first, and the menu follows once it has, over the Locked screen.
bool nav_menu_on_arrival = false;
