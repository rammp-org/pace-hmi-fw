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

#include <stdbool.h>
#include <stddef.h>
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
 * Length of a label override, including the NUL terminator: 15 usable
 * characters. Printable ASCII only — the HMI draws these with LVGL's built-in
 * Montserrat faces, which carry no glyphs outside ASCII.
 */
#define RAMMP_MCB_TEXT_LEN 16

/**
 * MCB -> joystick status, published periodically (best-effort, no durability,
 * so a late-joining or rebooted joystick converges on the next period rather
 * than on the next change).
 *
 * The two text fields are OVERRIDES, not the normal path: leave them empty and
 * the HMI shows the enum's own name, which is what a production MCB should do —
 * it sends what the chair is doing and lets the HMI choose the wording. A
 * non-empty string replaces the displayed text only; the colour still follows
 * the enum, so drive_status=ACTIVE with drive_text="CHARGING" reads as green
 * "CHARGING". They exist so a bench tool can drive the panel into states the
 * enums do not (yet) name.
 *
 * Wire size: RAMMP_MCB_STATUS_PAYLOAD_SIZE bytes, or
 * RAMMP_MCB_STATUS_CDR_SIZE including the encapsulation header.
 */
typedef struct rammp_mcb_status {
  uint8_t drive_status;                /**< one of RAMMP_DRIVE_STATUS_* */
  uint8_t system_state;                /**< one of RAMMP_STATE_* */
  uint8_t flags;                       /**< reserved: drive inhibit, e-stop, ... (send 0) */
  uint8_t seq;                         /**< free-running, wraps; for staleness and debug */
  char drive_text[RAMMP_MCB_TEXT_LEN]; /**< "" = use the enum's name */
  char state_text[RAMMP_MCB_TEXT_LEN]; /**< "" = use the enum's name */
} rammp_mcb_status_t;

/** 4 scalars + two fixed strings; every field is byte-aligned, so no padding. */
#define RAMMP_MCB_STATUS_PAYLOAD_SIZE (4 + 2 * RAMMP_MCB_TEXT_LEN)
/** Classic CDR encapsulation header, little-endian (xcdr1). */
#define RAMMP_CDR_HEADER_SIZE 4
#define RAMMP_MCB_STATUS_CDR_SIZE (RAMMP_CDR_HEADER_SIZE + RAMMP_MCB_STATUS_PAYLOAD_SIZE)

/**
 * Serialize `status` into `out` as a CDR-encapsulated sample.
 *
 * Written out by hand rather than derived by reflection: this struct is the
 * contract between two independently built boards, so the byte order deserves
 * to be stated here rather than implied by whichever serializer each side
 * happens to link against. The MCB may not be C++ and may not use espp/cdr at
 * all — it only needs this header.
 *
 * @return bytes written, or 0 if `out` is too small.
 */
static inline size_t rammp_mcb_status_encode(const rammp_mcb_status_t *status, uint8_t *out,
                                             size_t out_size) {
  size_t i;
  if (status == NULL || out == NULL || out_size < RAMMP_MCB_STATUS_CDR_SIZE) {
    return 0;
  }
  out[0] = 0x00; /* CDR_LE */
  out[1] = 0x01;
  out[2] = 0x00; /* options */
  out[3] = 0x00;
  out[4] = status->drive_status;
  out[5] = status->system_state;
  out[6] = status->flags;
  out[7] = status->seq;
  for (i = 0; i < RAMMP_MCB_TEXT_LEN; ++i) {
    out[8 + i] = (uint8_t)status->drive_text[i];
    out[8 + RAMMP_MCB_TEXT_LEN + i] = (uint8_t)status->state_text[i];
  }
  return RAMMP_MCB_STATUS_CDR_SIZE;
}

/**
 * Parse a CDR-encapsulated sample into `status`.
 *
 * @return false if the buffer is short or is not little-endian CDR.
 */
static inline bool rammp_mcb_status_decode(const uint8_t *in, size_t in_size,
                                           rammp_mcb_status_t *status) {
  size_t i;
  if (in == NULL || status == NULL || in_size < RAMMP_MCB_STATUS_CDR_SIZE) {
    return false;
  }
  if (in[0] != 0x00 || in[1] != 0x01) {
    return false; /* big-endian CDR or not CDR at all; nobody here emits that */
  }
  status->drive_status = in[4];
  status->system_state = in[5];
  status->flags = in[6];
  status->seq = in[7];
  for (i = 0; i < RAMMP_MCB_TEXT_LEN; ++i) {
    status->drive_text[i] = (char)in[8 + i];
    status->state_text[i] = (char)in[8 + RAMMP_MCB_TEXT_LEN + i];
  }
  /* A sender that filled every byte leaves no terminator; force one rather than
     let the consumer walk off the end of the field. */
  status->drive_text[RAMMP_MCB_TEXT_LEN - 1] = '\0';
  status->state_text[RAMMP_MCB_TEXT_LEN - 1] = '\0';
  return true;
}

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
