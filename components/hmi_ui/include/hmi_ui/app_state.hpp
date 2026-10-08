#pragma once
/// @file app_state.hpp
/// @brief The state the parts of the main unit share across tasks and screens, declared once so
///        a part can leave main.cpp and still reach it (from main/frag_state.inc).
///
/// Each piece is waiting for its owner (app-main-shrink.md S7): the view instances and subjects
/// for their views, the atomics for topology channels (T-H4a). Until then they are these global
/// names, which the code that reads them uses as it always has. Not here: the locked, MIB state
/// and link subjects and the tables over them (SharedSubjects, NavPort), which are UiApp's.
///
/// Every variable is constant-initialised (no global constructor, G4).

#include <atomic>
#include <cstdint>

#include "lvgl.h"

#include "hmi_ui/joystick_view.hpp"
#include "hmi_ui/setting_subjects.hpp"
#include "messages/mib_message.hpp"
#include "settings_applied.hpp"

/// The Joystick test screen's readouts (axis bars, button count): their subjects are its members.
/// The ADC task sets the bars (under a tried lvgl_mutex), the GPIO48 callback the count.
extern hmi::ui::JoystickView joystick_view;

/// 0/1 blink phase for the TopBar's RTPS indicator, flipped by the 250 ms poll (UiPoll). LVGL task.
extern lv_subject_t rtps_blink_subject;

/// The stick button's level, mirrored out of the LVGL world so the ADC task can publish it with
/// the stick position without the LVGL lock. Written by the GPIO48 callback (and the remote UI).
extern std::atomic<bool> joy_button_pressed;

/// The drive mode the user picked on the DriveScreen, reported to the MCB with every DriveCommand.
extern std::atomic<MIB::DriveProfile> drive_profile_published;

/// Joystick -> LVGL keypad: the LV_KEY_* direction the stick is held toward, 0 when centred. A
/// held key, so LVGL's own key repeat walks a list while the stick stays pushed. ADC task writes,
/// the keypad read (LVGL task) reads.
extern std::atomic<uint32_t> joy_key;

/// The stick button's select, edge-latched (a repeated ENTER would re-click the focused row).
/// Consumed by the keypad read.
extern std::atomic<bool> select_key;

/// A direction held by the remote UI channel; 0 = none. Outlasts the ADC task's rewrite of joy_key.
extern std::atomic<uint32_t> remote_key;

/// The last direction the stick engaged, kept until the keypad has read it (a quick flick).
extern std::atomic<uint32_t> joy_flick;

/// The Settings rows' subjects, one per SETTINGS_PARAM_*. Each owner's observer applies and saves
/// its value. LVGL task.
extern hmi::ui::SettingSubjects setting_subjects;

/// What the ADC task and the click sound read of the settings, as atomics (components/settings).
extern hmi::settings::AppliedSettings applied_settings;

/// The burger menu's overlay while it is up, else null. LVGL task.
extern lv_obj_t *nav_menu_open;

/// Whether the stick may drive the chair right now: Drive screen up, menu shut, drive active.
/// Written on the LVGL task (nav_update_stick_gate), read by the ADC task.
extern std::atomic<bool> stick_drives;

/// Open the menu on the next screen to load (the burger key on Drive). LVGL task.
extern bool nav_menu_on_arrival;
