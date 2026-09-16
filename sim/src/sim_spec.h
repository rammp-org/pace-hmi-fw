/*
 * The constants the desktop sim needs from the RTPS spec (messages/joystick_message.hpp
 * and messages/mib_message.hpp in external/rammp-rtps, and main/hmi_rtps_spec.hpp), which is C++
 * and so cannot be included from this C build. A hand-kept mirror, like
 * sim_nav.c is of main.cpp: keep the values in step with the spec.
 */
#ifndef SIM_SPEC_H
#define SIM_SPEC_H

#include <stdint.h>

#define RAMMP_SYSTEM_STATE_PERIOD_MS 500   /* rammp::kMibStatusPeriod */
#define RAMMP_SYSTEM_STATE_TIMEOUT_MS 2000 /* rammp::kMibStatusTimeout */

#define RAMMP_SPEED_MAX_TENTHS 99  /* MIB::kSpeedMaxTenths */
#define RAMMP_MCB_TEXT_LEN 16      /* rammp::kMcbTextLen */
#define RAMMP_ERROR_TEXT_LEN 64    /* rammp::kErrorTextLen */
#define RAMMP_ERROR_FOOTER_LEN 32  /* rammp::kErrorFooterLen */
#define RAMMP_BUTTON_JOYSTICK 0x1u /* rammp::Buttons::JOYSTICK */

/* MIB::MibSystemState - one enum where a drive status and a fault used to be.
   The chair drives only in ENABLED, and manual seat control is IDLE-only. */
#define RAMMP_MIB_STATE_INITIALIZING 0
#define RAMMP_MIB_STATE_IDLE 1
#define RAMMP_MIB_STATE_ENABLED 2
#define RAMMP_MIB_STATE_ERROR 3

/* MIB::DriveProfile - a response strength, not a kinematics mode */
#define RAMMP_DRIVE_PROFILE_LOW 0
#define RAMMP_DRIVE_PROFILE_NORMAL 1
#define RAMMP_DRIVE_PROFILE_HIGH 2

static inline const char *rammp_mib_state_name(int v) {
  return v == RAMMP_MIB_STATE_INITIALIZING ? "INITIALIZING"
         : v == RAMMP_MIB_STATE_IDLE       ? "IDLE"
         : v == RAMMP_MIB_STATE_ENABLED    ? "ENABLED"
         : v == RAMMP_MIB_STATE_ERROR      ? "ERROR"
                                           : "?";
}

#endif /* SIM_SPEC_H */
