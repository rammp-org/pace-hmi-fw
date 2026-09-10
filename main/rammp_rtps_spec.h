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

/* The joystick publishes RAW millivolts, deliberately: the firmware's own
   calibration is measured from this stream, so it must not arrive pre-cooked.
   A consumer needs these two numbers to interpret it. Centre is the resting
   position of every axis, full scale is the supply, so deflection on any axis
   runs +/- (FULL_SCALE / 2).

   Sign: pushing the stick FORWARD makes the vertical axis read BELOW centre.
   The firmware's range mapper inverts on the way to its own UI, but that
   inversion is not applied to what goes on the wire. */
#define RAMMP_JOYSTICK_CENTER_MV 1650
#define RAMMP_JOYSTICK_FULL_SCALE_MV 3300

/** Bits in rammp_adc_xy_twist_t.buttons. */
#define RAMMP_BUTTON_JOYSTICK 0x00000001u

/* How the chair should interpret stick deflection. Chosen by the user on the
   HMI's drive screen and reported to the MCB, so the MCB never has to guess
   what the person meant by pushing the stick sideways.

   NORMAL  car-like: forward/back drives, left/right steers.
   HOLO    holonomic: the stick's X/Y is a velocity vector, no steering.
   AUTO    reserved; the HMI can select it but nothing implements it yet.

   Twist is independent of all three: it always rotates the chair on the spot. */
enum {
  RAMMP_DRIVE_MODE_NORMAL = 0,
  RAMMP_DRIVE_MODE_HOLO = 1,
  RAMMP_DRIVE_MODE_AUTO = 2,
};

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
 * Error banner body and footer, including the NUL. The body wraps inside a
 * 600 px label at Montserrat 48 and the footer sits under it at 34, so these
 * are sized for a couple of short lines rather than a paragraph. ASCII only,
 * same reason as above.
 */
#define RAMMP_ERROR_TEXT_LEN 64
#define RAMMP_ERROR_FOOTER_LEN 32

/** speed_tenths runs 0..99 and is displayed as N.N, so 0.0 to 9.9. */
#define RAMMP_SPEED_MAX_TENTHS 99

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
  uint8_t speed_tenths;                /**< 0..RAMMP_SPEED_MAX_TENTHS, shown as N.N */
  char drive_text[RAMMP_MCB_TEXT_LEN]; /**< "" = use the enum's name */
  char state_text[RAMMP_MCB_TEXT_LEN]; /**< "" = use the enum's name */
  /* Body and footer of the error banner the HMI raises whenever system_state is
     not RAMMP_STATE_OK. Shown verbatim: the MCB owns the wording of a fault,
     the HMI only decides when to show it. */
  char error_text[RAMMP_ERROR_TEXT_LEN];
  char error_footer[RAMMP_ERROR_FOOTER_LEN];
} rammp_mcb_status_t;

/** 5 scalars + four fixed strings; every field is byte-aligned, so no padding. */
#define RAMMP_MCB_STATUS_PAYLOAD_SIZE                                                              \
  (5 + 2 * RAMMP_MCB_TEXT_LEN + RAMMP_ERROR_TEXT_LEN + RAMMP_ERROR_FOOTER_LEN)
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
  size_t i, offset;
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
  out[8] = status->speed_tenths;
  offset = RAMMP_CDR_HEADER_SIZE + 5;
  for (i = 0; i < RAMMP_MCB_TEXT_LEN; ++i) {
    out[offset + i] = (uint8_t)status->drive_text[i];
    out[offset + RAMMP_MCB_TEXT_LEN + i] = (uint8_t)status->state_text[i];
  }
  offset += 2 * RAMMP_MCB_TEXT_LEN;
  for (i = 0; i < RAMMP_ERROR_TEXT_LEN; ++i) {
    out[offset + i] = (uint8_t)status->error_text[i];
  }
  offset += RAMMP_ERROR_TEXT_LEN;
  for (i = 0; i < RAMMP_ERROR_FOOTER_LEN; ++i) {
    out[offset + i] = (uint8_t)status->error_footer[i];
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
  size_t i, offset;
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
  status->speed_tenths = in[8];
  offset = RAMMP_CDR_HEADER_SIZE + 5;
  for (i = 0; i < RAMMP_MCB_TEXT_LEN; ++i) {
    status->drive_text[i] = (char)in[offset + i];
    status->state_text[i] = (char)in[offset + RAMMP_MCB_TEXT_LEN + i];
  }
  offset += 2 * RAMMP_MCB_TEXT_LEN;
  for (i = 0; i < RAMMP_ERROR_TEXT_LEN; ++i) {
    status->error_text[i] = (char)in[offset + i];
  }
  offset += RAMMP_ERROR_TEXT_LEN;
  for (i = 0; i < RAMMP_ERROR_FOOTER_LEN; ++i) {
    status->error_footer[i] = (char)in[offset + i];
  }
  /* A sender that filled every byte leaves no terminator; force one rather than
     let the consumer walk off the end of the field. */
  status->drive_text[RAMMP_MCB_TEXT_LEN - 1] = '\0';
  status->state_text[RAMMP_MCB_TEXT_LEN - 1] = '\0';
  status->error_text[RAMMP_ERROR_TEXT_LEN - 1] = '\0';
  status->error_footer[RAMMP_ERROR_FOOTER_LEN - 1] = '\0';
  return true;
}

/** joystick -> MCB stick position in millivolts, plus button state.
 *  Wire size: 16 bytes. Serialized by reflection (every field is uint32),
 *  not by a hand-written codec like McbStatus. */
typedef struct rammp_adc_xy_twist {
  uint32_t x_mv;
  uint32_t y_mv;
  uint32_t twist_mv;
  uint32_t buttons;    /**< bitfield of RAMMP_BUTTON_*; bit set = pressed */
  uint32_t drive_mode; /**< one of RAMMP_DRIVE_MODE_*, chosen on the HMI */
} rammp_adc_xy_twist_t;
/* NOTE: this message has outgrown the name "AdcXYTwist" — it now carries button
   and drive-mode state as well as the raw axes. Worth renaming to JoystickState
   on the next change that touches both ends anyway; not done here to avoid
   churning the topic name for a field addition. */

/* -------------------------------------------------------------------------
 * Actuators
 *
 * The MCB owns every actuator position. The HMI never moves one itself: it
 * asks ("move actuator 2 down one step") and shows whatever comes back. That
 * is what keeps a limit, an interlock or a stalled motor a single decision
 * made in one place, rather than two boards holding different opinions about
 * where the seat is.
 *
 * The table below is the shared definition of WHICH actuators exist. Both ends
 * index by position in it, so the ids only mean the same thing on both boards
 * because they come from the same list. `scripts/rammp_rtps.py` scrapes the
 * X(...) lines, so the python bench tools build their actuator list from here
 * too - keep new entries on one line in that shape.
 *
 * Values are raw integers in the actuator's own units, scaled by `decimals`
 * for display only: 2500 with decimals=1 shows as "250.0". Nothing on the wire
 * is floating point, so there is no float format for the two boards to agree
 * on, and min/max/step stay exact.
 * ---------------------------------------------------------------------- */

/** joystick -> MCB: a request to move one actuator by a number of steps. */
#define RAMMP_TOPIC_ACTUATOR_COMMAND "rammp/actuator/command"
#define RAMMP_TYPE_ACTUATOR_COMMAND "rammp/msg/ActuatorCommand"

/** MCB -> joystick: every actuator's value, plus the verdict on the last
 *  request. Published on change AND periodically, for the reason below. */
#define RAMMP_TOPIC_ACTUATOR_STATE "rammp/actuator/state"
#define RAMMP_TYPE_ACTUATOR_STATE "rammp/msg/ActuatorState"

/**
 * Republish period for RAMMP_TOPIC_ACTUATOR_STATE.
 *
 * A reply to a command is just an immediate publish of this same message, so
 * there is no separate reply topic to keep matched. The periodic republish is
 * what lets a joystick that just booted, or one that lost the link for a
 * while, learn where the actuators are without pressing anything: the topic is
 * best-effort with no durability, so a state that is only sent on change is a
 * state a late joiner never hears.
 */
#define RAMMP_ACTUATOR_STATE_PERIOD_MS 500

/**
 * Hard ceiling on the actuator count, so the state message is a fixed size and
 * both ends can size an array without allocating. Raising it changes the wire
 * layout: both boards must be rebuilt together.
 */
#define RAMMP_ACTUATOR_MAX 8

/**
 * The actuators themselves.
 *
 *   X(id, NAME, short, label, min, max, step, decimals, unit)
 *
 * `id` must equal the row's position in the list. `short` is the badge on the
 * left of the HMI's row ("M1"), `label` the name beside it. `step` is how far
 * one press of - or + asks the actuator to move, in the same raw units as
 * min/max.
 */
#define RAMMP_ACTUATOR_TABLE(X)                                                                    \
  X(0, ELEVATION, "M1", "Elevation", 0, 2500, 50, 1, "mm")                                         \
  X(1, REAR_TILT, "M2", "Rear Tilt", 0, 900, 25, 1, "deg")                                         \
  X(2, FORWARD_TILT, "M3", "Forward Tilt", 0, 450, 25, 1, "deg")                                   \
  X(3, SIDE_TILT, "M4", "Side Tilt", -300, 300, 25, 1, "deg")

/**
 * How many actuators the table names, as a constant expression, so a table
 * that outgrows RAMMP_ACTUATOR_MAX is a build error rather than something a
 * board discovers at boot.
 */
#define RAMMP_ACTUATOR_COUNT_ONE(...) +1
#define RAMMP_ACTUATOR_COUNT (0 RAMMP_ACTUATOR_TABLE(RAMMP_ACTUATOR_COUNT_ONE))

/** RAMMP_ACTUATOR_ELEVATION, RAMMP_ACTUATOR_REAR_TILT, ... */
enum {
#define RAMMP_ACTUATOR_ENUM(id_, name_, short_, label_, min_, max_, step_, dec_, unit_)            \
  RAMMP_ACTUATOR_##name_ = id_,
  RAMMP_ACTUATOR_TABLE(RAMMP_ACTUATOR_ENUM)
#undef RAMMP_ACTUATOR_ENUM
};

/**
 * The MCB's verdict on a request. Anything but OK means the value did not
 * change; the state message still carries the true values either way, so a
 * rejected request and a lost one look the same to the HMI's display path.
 */
enum {
  RAMMP_ACTUATOR_RESULT_OK = 0,
  RAMMP_ACTUATOR_RESULT_AT_MIN = 1,     /**< already at the low end of its range */
  RAMMP_ACTUATOR_RESULT_AT_MAX = 2,     /**< already at the high end */
  RAMMP_ACTUATOR_RESULT_INHIBITED = 3,  /**< refused right now: interlock, fault, driving */
  RAMMP_ACTUATOR_RESULT_UNKNOWN_ID = 4, /**< no such actuator on this chair */
};

typedef struct rammp_actuator_spec {
  uint8_t id;             /**< index into the table; what goes on the wire */
  const char *short_name; /**< "M1" */
  const char *label;      /**< "Elevation" */
  int32_t min_value;      /**< raw units */
  int32_t max_value;      /**< raw units */
  int32_t step;           /**< raw units moved by one - or + press */
  uint8_t decimals;       /**< digits after the point when displayed */
  const char *unit;       /**< "mm", "deg" */
} rammp_actuator_spec_t;

/**
 * The table as an array. An accessor rather than a bare `static const` array
 * at file scope, so a translation unit that includes this header without
 * touching the actuators does not carry an unused-variable warning for it.
 *
 * @param count  filled with the number of entries, if not NULL
 */
static inline const rammp_actuator_spec_t *rammp_actuator_table(uint8_t *count) {
  static const rammp_actuator_spec_t table[] = {
#define RAMMP_ACTUATOR_ROW(id_, name_, short_, label_, min_, max_, step_, dec_, unit_)             \
  {(uint8_t)(id_),  short_,           label_,          (int32_t)(min_),                            \
   (int32_t)(max_), (int32_t)(step_), (uint8_t)(dec_), unit_},
      RAMMP_ACTUATOR_TABLE(RAMMP_ACTUATOR_ROW)
#undef RAMMP_ACTUATOR_ROW
  };
  if (count != NULL) {
    *count = (uint8_t)(sizeof(table) / sizeof(table[0]));
  }
  return table;
}

/**
 * joystick -> MCB. One press of - or + is steps = -1 or +1; the field is wider
 * than that so a coarse or repeat-collapsed step can be asked for later
 * without another wire change.
 *
 * `req_id` is free-running and wraps. It comes back in the state message so a
 * verdict can be attributed to the press that caused it rather than to
 * whatever the user did next.
 *
 * Wire size: 8 bytes including the encapsulation header.
 */
typedef struct rammp_actuator_command {
  uint8_t req_id;      /**< free-running, wraps; echoed in the state reply */
  uint8_t actuator_id; /**< index into the actuator table */
  int8_t steps;        /**< signed; -1 = one press of "-", +1 = one press of "+" */
  uint8_t reserved;    /**< send 0 */
} rammp_actuator_command_t;

#define RAMMP_ACTUATOR_COMMAND_PAYLOAD_SIZE 4
#define RAMMP_ACTUATOR_COMMAND_CDR_SIZE                                                            \
  (RAMMP_CDR_HEADER_SIZE + RAMMP_ACTUATOR_COMMAND_PAYLOAD_SIZE)

/**
 * MCB -> joystick. Carries EVERY actuator's value, not just the one that
 * moved: it doubles as the periodic broadcast, and a whole-state message means
 * a joystick can never end up with some rows fresh and others stale.
 *
 * `count` is how many entries of values[] are meaningful. It lets an MCB with
 * fewer actuators than the table talk to this HMI without the extra rows
 * showing a zero they never earned.
 *
 * Wire size: 40 bytes including the encapsulation header.
 */
typedef struct rammp_actuator_state {
  uint8_t req_id;                     /**< the command this answers; 0 = none since boot */
  uint8_t result;                     /**< one of RAMMP_ACTUATOR_RESULT_* */
  uint8_t count;                      /**< meaningful entries in values[], <= RAMMP_ACTUATOR_MAX */
  uint8_t seq;                        /**< free-running, wraps; for staleness and debug */
  int32_t values[RAMMP_ACTUATOR_MAX]; /**< raw units, as in the table */
} rammp_actuator_state_t;

#define RAMMP_ACTUATOR_STATE_PAYLOAD_SIZE (4 + 4 * RAMMP_ACTUATOR_MAX)
#define RAMMP_ACTUATOR_STATE_CDR_SIZE (RAMMP_CDR_HEADER_SIZE + RAMMP_ACTUATOR_STATE_PAYLOAD_SIZE)

/* The four leading bytes put the int32 array on a 4-byte boundary, so classic
   CDR inserts no padding and the payload is exactly the size above. Keep any
   new field a multiple of four, or in that leading group. */

static inline void rammp_write_u32_le(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)(value & 0xFFu);
  out[1] = (uint8_t)((value >> 8) & 0xFFu);
  out[2] = (uint8_t)((value >> 16) & 0xFFu);
  out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static inline uint32_t rammp_read_u32_le(const uint8_t *in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}

static inline size_t rammp_actuator_command_encode(const rammp_actuator_command_t *command,
                                                   uint8_t *out, size_t out_size) {
  if (command == NULL || out == NULL || out_size < RAMMP_ACTUATOR_COMMAND_CDR_SIZE) {
    return 0;
  }
  out[0] = 0x00; /* CDR_LE */
  out[1] = 0x01;
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = command->req_id;
  out[5] = command->actuator_id;
  out[6] = (uint8_t)command->steps;
  out[7] = command->reserved;
  return RAMMP_ACTUATOR_COMMAND_CDR_SIZE;
}

static inline bool rammp_actuator_command_decode(const uint8_t *in, size_t in_size,
                                                 rammp_actuator_command_t *command) {
  if (in == NULL || command == NULL || in_size < RAMMP_ACTUATOR_COMMAND_CDR_SIZE) {
    return false;
  }
  if (in[0] != 0x00 || in[1] != 0x01) {
    return false;
  }
  command->req_id = in[4];
  command->actuator_id = in[5];
  command->steps = (int8_t)in[6];
  command->reserved = in[7];
  return true;
}

static inline size_t rammp_actuator_state_encode(const rammp_actuator_state_t *state, uint8_t *out,
                                                 size_t out_size) {
  size_t i;
  if (state == NULL || out == NULL || out_size < RAMMP_ACTUATOR_STATE_CDR_SIZE) {
    return 0;
  }
  out[0] = 0x00; /* CDR_LE */
  out[1] = 0x01;
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = state->req_id;
  out[5] = state->result;
  out[6] = state->count;
  out[7] = state->seq;
  for (i = 0; i < RAMMP_ACTUATOR_MAX; ++i) {
    rammp_write_u32_le(out + 8 + 4 * i, (uint32_t)state->values[i]);
  }
  return RAMMP_ACTUATOR_STATE_CDR_SIZE;
}

static inline bool rammp_actuator_state_decode(const uint8_t *in, size_t in_size,
                                               rammp_actuator_state_t *state) {
  size_t i;
  if (in == NULL || state == NULL || in_size < RAMMP_ACTUATOR_STATE_CDR_SIZE) {
    return false;
  }
  if (in[0] != 0x00 || in[1] != 0x01) {
    return false;
  }
  state->req_id = in[4];
  state->result = in[5];
  state->count = in[6];
  state->seq = in[7];
  for (i = 0; i < RAMMP_ACTUATOR_MAX; ++i) {
    state->values[i] = (int32_t)rammp_read_u32_le(in + 8 + 4 * i);
  }
  /* A sender that claims more actuators than this build knows about would walk
     a consumer off the end of values[]; clamp rather than trust it. */
  if (state->count > RAMMP_ACTUATOR_MAX) {
    state->count = RAMMP_ACTUATOR_MAX;
  }
  return true;
}

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

static inline const char *rammp_actuator_result_name(uint8_t result) {
  switch (result) {
  case RAMMP_ACTUATOR_RESULT_OK:
    return "OK";
  case RAMMP_ACTUATOR_RESULT_AT_MIN:
    return "AT_MIN";
  case RAMMP_ACTUATOR_RESULT_AT_MAX:
    return "AT_MAX";
  case RAMMP_ACTUATOR_RESULT_INHIBITED:
    return "INHIBITED";
  case RAMMP_ACTUATOR_RESULT_UNKNOWN_ID:
    return "UNKNOWN_ID";
  default:
    return "?";
  }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RAMMP_RTPS_SPEC_H */
