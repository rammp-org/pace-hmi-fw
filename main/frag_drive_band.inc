// split_main.py fragment of main.cpp (frag_drive_band: original lines 567-638); one TU, see main.cpp.
#include "hmi_format/speed.hpp"
#include "hmi_ui/drive_band_view.hpp"
/////////////////////////////////////////////////////////////////////////////
// DriveScreen: drive-mode selection and the speed readout (hmi::ui::DriveBandView)
//
// Three plain LVGL buttons, so they are already touch-clickable; all this adds
// is what a tap means and which one looks selected. Deliberately touch-only:
// the joystick is busy driving on this screen, and stealing left/right for menu
// navigation is exactly the class of bug that made pulling back exit the
// screen.
/////////////////////////////////////////////////////////////////////////////

// The drive session's input, defined in frag_drive.inc.
static bool drive_session_input(hmi::drive_session::Input input);

// The one instance; bound from app_main.
static constinit hmi::ui::DriveBandView drive_band_view{{
    .profiles = {static_cast<int32_t>(MIB::DriveProfile::HIGH),
                 static_cast<int32_t>(MIB::DriveProfile::NORMAL),
                 static_cast<int32_t>(MIB::DriveProfile::LOW)},
    // The ADC task cannot take the LVGL lock, so the profile reaches it through an atomic.
    .store_profile =
        [](int32_t profile) {
          drive_profile_published.store(static_cast<MIB::DriveProfile>(profile));
        },
    // PUBLISH_DRIVE: the drive request as it stands, with the new profile (rows 29-34).
    .profile_clicked = [] { (void)drive_session_input(hmi::drive_session::Input::PROFILE_CLICK); },
    .nav = &ui_nav_port,
}};

// MibStatus.speed is metres per second; the label shows mph to one decimal.
// Any task: pure arithmetic, no LVGL (components/hmi_format, REQ-FMT-01).
using hmi::format::speed_display_tenths;
static_assert(hmi::format::MPH_PER_MPS == rammp::kMphPerMps,
              "hmi_format's mph per m/s must be the shared spec's");
static_assert(hmi::format::SPEED_MAX_TENTHS == rammp::kSpeedMaxTenths,
              "hmi_format's speed clamp must be the shared spec's");

