/*
 * The constants the desktop sim needs from main/rammp_rtps_spec.h, which is C++
 * and so cannot be included from this C build. A hand-kept mirror, like
 * sim_nav.c is of main.cpp: keep the values in step with the spec.
 */
#ifndef SIM_SPEC_H
#define SIM_SPEC_H

#include <stdint.h>

#define RAMMP_MCB_STATUS_PERIOD_MS 500   /* rammp::kMcbStatusPeriod */
#define RAMMP_MCB_STATUS_TIMEOUT_MS 2000 /* rammp::kMcbStatusTimeout */

#define RAMMP_SPEED_MAX_TENTHS 99  /* rammp::kSpeedMaxTenths */
#define RAMMP_MCB_TEXT_LEN 16      /* rammp::kMcbTextLen */
#define RAMMP_ERROR_TEXT_LEN 64    /* rammp::kErrorTextLen */
#define RAMMP_ERROR_FOOTER_LEN 32  /* rammp::kErrorFooterLen */
#define RAMMP_BUTTON_JOYSTICK 0x1u /* rammp::Buttons::JOYSTICK */

/* rammp::DriveStatus */
#define RAMMP_DRIVE_STATUS_INACTIVE 0
#define RAMMP_DRIVE_STATUS_ACTIVE 1

/* rammp::SystemState */
#define RAMMP_STATE_OK 0
#define RAMMP_STATE_ERROR 1

/* rammp::DriveMode */
#define RAMMP_DRIVE_MODE_NORMAL 0
#define RAMMP_DRIVE_MODE_HOLO 1
#define RAMMP_DRIVE_MODE_AUTO 2

static inline const char *rammp_drive_status_name(int v) {
  return v == RAMMP_DRIVE_STATUS_INACTIVE ? "INACTIVE"
         : v == RAMMP_DRIVE_STATUS_ACTIVE ? "ACTIVE"
                                          : "?";
}

static inline const char *rammp_state_name(int v) {
  return v == RAMMP_STATE_OK ? "OK" : v == RAMMP_STATE_ERROR ? "ERROR" : "?";
}

#endif /* SIM_SPEC_H */
