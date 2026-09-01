/**
 * @file rammp_rtps_spec.h
 * @brief RAMMP RTPS wire spec: topics, type names, enums and message layouts.
 *
 * SINGLE SOURCE OF TRUTH for everything that crosses the wire between the
 * joystick HMI (this firmware) and the Main Control Board. The MCB firmware is
 * meant to include this very file, so keep it plain C: no C++ keywords, no
 * includes beyond <stdint.h>, no dependency on ESP-IDF or LVGL. It will move
 * into a shared component once the first exchange is proven on the bench.
 *
 * `scripts/rammp_rtps.py` scrapes this file for the topic/type strings and the
 * enum values, so the python test tools cannot drift from the firmware. That
 * scraper matches `#define RAMMP_TOPIC_*` / `#define RAMMP_TYPE_*` lines and
 * `RAMMP_<GROUP>_<NAME> = <int>` enumerators — keep new entries in that shape.
 *
 * Roles: the MCB is the master and owns the vehicle state; the joystick is a
 * slave that displays what it is told and asks for what the user wants.
 */

#ifndef RAMMP_RTPS_SPEC_H
#define RAMMP_RTPS_SPEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Topics and type names
 *
 * espp/rtps emits these names verbatim (no ROS 2 mangling), so both ends just
 * have to agree. A writer has no send destination until a remote reader on the
 * same topic name is discovered, which is why a typo shows up as silence
 * rather than an error.
 * ---------------------------------------------------------------------- */

/** MCB -> joystick: drive status, system state and safety flags. */
#define RAMMP_TOPIC_MCB_STATUS "rammp/mcb/status"
#define RAMMP_TYPE_MCB_STATUS "rammp/msg/McbStatus"

/** joystick -> MCB: raw stick position, one sample per ADC cycle (~30 Hz). */
#define RAMMP_TOPIC_JOYSTICK_ADC "rammp/joystick/adc"
#define RAMMP_TYPE_ADC_XY_TWIST "rammp/msg/AdcXYTwist"

/** Bench/bring-up topics: heartbeat counter, its echo, remote LCD brightness. */
#define RAMMP_TOPIC_HMI_COUNTER "rammp/hmi/counter"
#define RAMMP_TOPIC_HMI_COMMAND "rammp/hmi/command"
#define RAMMP_TOPIC_HMI_BRIGHTNESS "rammp/hmi/brightness"

/** Stock ROS 2 type for the single-uint32 bench topics above. */
#define RAMMP_TYPE_UINT32 "std_msgs/msg/UInt32"

/* -------------------------------------------------------------------------
 * Timing contract for RAMMP_TOPIC_MCB_STATUS
 *
 * A publisher of McbStatus must republish at least every
 * RAMMP_MCB_STATUS_TIMEOUT_MS, even when nothing has changed. The topic is
 * best-effort with no durability, so silence is indistinguishable from an
 * absent publisher: a consumer that has heard nothing for that long treats the
 * link as lost, and the HMI greys out its drive-status and state labels rather
 * than keep showing a value it can no longer vouch for.
 *
 * PERIOD is the recommended send rate, four times inside the timeout so three
 * consecutive drops still do not trip it.
 * ---------------------------------------------------------------------- */
#define RAMMP_MCB_STATUS_PERIOD_MS 500
#define RAMMP_MCB_STATUS_TIMEOUT_MS 2000

/* -------------------------------------------------------------------------
 * Enumerations
 *
 * Deliberately minimal: only the states the HMI can show today. Additions go
 * here and both boards rebuild against them — that is the whole point of the
 * shared header. Values are explicit because the python scraper reads them.
 * ---------------------------------------------------------------------- */

/** Whether the chair is currently accepting drive commands from the stick. */
enum {
  RAMMP_DRIVE_STATUS_INACTIVE = 0,
  RAMMP_DRIVE_STATUS_ACTIVE = 1,
};

/** Overall health as judged by the MCB. */
enum {
  RAMMP_STATE_OK = 0,
  RAMMP_STATE_ERROR = 1,
};

/* -------------------------------------------------------------------------
 * Messages
 *
 * Serialized as classic CDR (xcdr1): a 4-byte encapsulation header
 * (00 01 00 00 = little-endian CDR) followed by the fields in declaration
 * order, each aligned to its own size. Field types are chosen so no padding
 * is ever inserted; keep it that way when adding fields.
 * ---------------------------------------------------------------------- */

/**
 * MCB -> joystick status, published periodically (best-effort, no durability,
 * so a late-joining or rebooted joystick converges on the next period rather
 * than on the next change).
 *
 * Wire size: 4 bytes, 8 including the CDR encapsulation header.
 */
typedef struct rammp_mcb_status {
  uint8_t drive_status; /**< one of RAMMP_DRIVE_STATUS_* */
  uint8_t system_state; /**< one of RAMMP_STATE_* */
  uint8_t flags;        /**< reserved: drive inhibit, e-stop, ... (send 0) */
  uint8_t seq;          /**< free-running, wraps; for staleness and debug */
} rammp_mcb_status_t;

/** joystick -> MCB stick position, in millivolts. Wire size: 12 bytes. */
typedef struct rammp_adc_xy_twist {
  uint32_t x_mv;
  uint32_t y_mv;
  uint32_t twist_mv;
} rammp_adc_xy_twist_t;

/* -------------------------------------------------------------------------
 * Display names
 *
 * Shared so the MCB's logs and the HMI's labels use the same words for the
 * same state. Colours are a UI concern and stay in the firmware.
 * ---------------------------------------------------------------------- */

static inline const char *rammp_drive_status_name(uint8_t drive_status) {
  switch (drive_status) {
  case RAMMP_DRIVE_STATUS_INACTIVE:
    return "INACTIVE";
  case RAMMP_DRIVE_STATUS_ACTIVE:
    return "ACTIVE";
  default:
    return "?";
  }
}

static inline const char *rammp_state_name(uint8_t system_state) {
  switch (system_state) {
  case RAMMP_STATE_OK:
    return "OK";
  case RAMMP_STATE_ERROR:
    return "ERROR";
  default:
    return "?";
  }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RAMMP_RTPS_SPEC_H */
