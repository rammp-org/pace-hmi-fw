/*
 * rammp_rtps_spec.h - RAMMP RTPS wire spec, shared by the HMI and the MCB.
 *
 * - Encoding: espp/cdr, XCDR1 (classic little-endian CDR, what DDS / ROS 2 peers speak).
 * - The C++ structs below ARE the wire layout: fields in order, no hand-written codec.
 * - IDL mapping: uint8_t=octet, int8_t=int8, int32_t=long, uint32_t=unsigned long,
 *   float=float, std::string=string, std::vector<T>=sequence<T>.
 * - Roles: the MCB owns the vehicle state; the HMI shows it and asks.
 * - Every topic is best-effort, no durability: state is resent periodically.
 * - Constants, enums and tables are plain C (the desktop sim includes them too).
 * - scripts/rammp_rtps.py scrapes this file (`#define RAMMP_X value`, `RAMMP_X = n,`)
 *   and mirrors the structs: change both together.
 *
 * Example: an MCB on the same espp / esp-idf stack sending Diagnostics to the HMI
 *
 *   #include "cdr.hpp"
 *   #include "rtps_participant.hpp"
 *   #include "rammp_rtps_spec.h"
 *
 *   using Rtps = espp::RtpsParticipant;
 *   Rtps rtps({.interface_address = my_ip});
 *   rtps.start();
 *   rtps.add_writer({.topic = RAMMP_TOPIC_MCB_DIAGNOSTICS,
 *                    .type_name = RAMMP_TYPE_DIAGNOSTICS,
 *                    .reliability = Rtps::Reliability::BEST_EFFORT});
 *
 *   // every RAMMP_DIAG_PERIOD_MS; one item per RAMMP_DIAG_TABLE row, raw integers
 *   rammp::Diagnostics diag{.seq = seq++,
 *                           .items = {{.values = {305, 150, 450}},    // T1: 30.5 C, 1.50 A, 45.0
 * deg
 *                                     {.values = {298, 120, -90}}}};  // T2
 *   if (auto bytes = cdr::serialize<cdr::xcdr1>(diag))
 *     rtps.publish(RAMMP_TOPIC_MCB_DIAGNOSTICS, rammp::as_u8(*bytes));
 *
 *   // and the other way: the HMI's actuator requests
 *   rtps.add_reader({.topic = RAMMP_TOPIC_ACTUATOR_COMMAND,
 *                    .type_name = RAMMP_TYPE_ACTUATOR_COMMAND,
 *                    .reliability = Rtps::Reliability::BEST_EFFORT,
 *                    .on_sample = [](std::span<const uint8_t> data) {
 *                      auto cmd = cdr::deserialize<rammp::ActuatorCommand>(std::as_bytes(data));
 *                      if (cmd) move_actuator(cmd->actuator_id, cmd->steps); // then send
 * ActuatorState
 *                    }});
 */

#ifndef RAMMP_RTPS_SPEC_H
#define RAMMP_RTPS_SPEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==== Topic and type names: the only place they are spelled ============== */
/* ROS 2 later? Change them here only ("rt/..." topics, "pkg::msg::dds_::Name_" types). */

/* MCB -> HMI */
#define RAMMP_TOPIC_MCB_STATUS "rammp/mcb/status" /* rammp::McbStatus */
#define RAMMP_TYPE_MCB_STATUS "rammp/msg/McbStatus"
#define RAMMP_TOPIC_ACTUATOR_STATE "rammp/actuator/state" /* rammp::ActuatorState */
#define RAMMP_TYPE_ACTUATOR_STATE "rammp/msg/ActuatorState"
#define RAMMP_TOPIC_MCB_DIAGNOSTICS "rammp/mcb/diagnostics" /* rammp::Diagnostics */
#define RAMMP_TYPE_DIAGNOSTICS "rammp/msg/Diagnostics"

/* HMI -> MCB */
#define RAMMP_TOPIC_JOYSTICK_ADC "rammp/joystick/adc" /* rammp::AdcXYTwist, ~30 Hz */
#define RAMMP_TYPE_ADC_XY_TWIST "rammp/msg/AdcXYTwist"
#define RAMMP_TOPIC_ACTUATOR_COMMAND "rammp/actuator/command" /* rammp::ActuatorCommand */
#define RAMMP_TYPE_ACTUATOR_COMMAND "rammp/msg/ActuatorCommand"

/* Bench PC <-> HMI. A production MCB can ignore these. */
#define RAMMP_TOPIC_HMI_COUNTER "rammp/hmi/counter"       /* HMI -> PC: heartbeat, self-test ping */
#define RAMMP_TOPIC_HMI_COMMAND "rammp/hmi/command"       /* PC -> HMI: self-test run and pong */
#define RAMMP_TOPIC_HMI_BRIGHTNESS "rammp/hmi/brightness" /* PC -> HMI: backlight %, 5..100 */
#define RAMMP_TYPE_UINT32 "std_msgs/msg/UInt32"           /* rammp::UInt32, the three above */
#define RAMMP_TOPIC_SELFTEST_REPORT "rammp/selftest/report" /* HMI -> PC: rammp::SelfTestReport */
#define RAMMP_TYPE_SELFTEST_REPORT "rammp/msg/SelfTestReport"

/* ==== Timing ============================================================ */

#define RAMMP_MCB_STATUS_PERIOD_MS 500     /* MCB sends McbStatus this often, changed or not */
#define RAMMP_MCB_STATUS_TIMEOUT_MS 2000   /* HMI: no McbStatus this long = link lost */
#define RAMMP_ACTUATOR_STATE_PERIOD_MS 500 /* MCB resends ActuatorState this often */
#define RAMMP_DIAG_PERIOD_MS 500           /* MCB sends Diagnostics this often */
#define RAMMP_DIAG_TIMEOUT_MS 2000         /* HMI: none this long = stale, shown red and blinking */

/* ==== McbStatus ========================================================= */

enum {
  RAMMP_DRIVE_STATUS_INACTIVE = 0, /* chair ignores the stick */
  RAMMP_DRIVE_STATUS_ACTIVE = 1,   /* chair drives on the stick */
};

enum {
  RAMMP_STATE_OK = 0,
  RAMMP_STATE_ERROR = 1, /* HMI blocks drive/seat and shows error_text */
};

#define RAMMP_SPEED_MAX_TENTHS 99 /* speed_tenths 0..99, shown as 0.0..9.9 */
#define RAMMP_MCB_TEXT_LEN 16     /* HMI shows up to 15 chars of drive_text / state_text */
#define RAMMP_ERROR_TEXT_LEN 64   /* HMI shows up to 63 chars of error_text */
#define RAMMP_ERROR_FOOTER_LEN 32 /* HMI shows up to 31 chars of error_footer */

/* ==== Joystick ========================================================== */

#define RAMMP_BUTTON_JOYSTICK 0x00000001u /* buttons bit: the stick's button */

enum {
  RAMMP_DRIVE_MODE_NORMAL = 0, /* car-like: Y drives, X steers */
  RAMMP_DRIVE_MODE_HOLO = 1,   /* holonomic: X/Y is the velocity vector */
  RAMMP_DRIVE_MODE_AUTO = 2,   /* reserved, not implemented */
};

/* ==== Actuators ========================================================= */

/* The MCB owns every position; the HMI sends step requests and shows what comes back.
   Values are raw integers; `decimals` is display only (2500 with 1 shows "250.0"). */

/* X(id, NAME, short, label, min, max, step, decimals, unit); id = row index */
#define RAMMP_ACTUATOR_TABLE(X)                                                                    \
  X(0, ELEVATION, "M1", "Elevation", 0, 2500, 50, 1, "mm")                                         \
  X(1, REAR_TILT, "M2", "Rear Tilt", 0, 900, 25, 1, "deg")                                         \
  X(2, FORWARD_TILT, "M3", "Forward Tilt", 0, 450, 25, 1, "deg")                                   \
  X(3, SIDE_TILT, "M4", "Side Tilt", -300, 300, 25, 1, "deg")

#define RAMMP_ACTUATOR_COUNT_ONE(...) +1
#define RAMMP_ACTUATOR_COUNT (0 RAMMP_ACTUATOR_TABLE(RAMMP_ACTUATOR_COUNT_ONE))

/* RAMMP_ACTUATOR_ELEVATION = 0, ... */
enum {
#define RAMMP_ACTUATOR_ENUM(id_, name_, short_, label_, min_, max_, step_, dec_, unit_)            \
  RAMMP_ACTUATOR_##name_ = id_,
  RAMMP_ACTUATOR_TABLE(RAMMP_ACTUATOR_ENUM)
#undef RAMMP_ACTUATOR_ENUM
};

/* The MCB's verdict on a command. Anything but OK = value unchanged. */
enum {
  RAMMP_ACTUATOR_RESULT_OK = 0,
  RAMMP_ACTUATOR_RESULT_AT_MIN = 1,     /* already at its low end */
  RAMMP_ACTUATOR_RESULT_AT_MAX = 2,     /* already at its high end */
  RAMMP_ACTUATOR_RESULT_INHIBITED = 3,  /* refused now: interlock, fault, driving */
  RAMMP_ACTUATOR_RESULT_UNKNOWN_ID = 4, /* no such actuator */
};

typedef struct rammp_actuator_spec {
  uint8_t id;             /* row index; what goes on the wire */
  const char *short_name; /* "M1" */
  const char *label;      /* "Elevation" */
  int32_t min_value;      /* raw units */
  int32_t max_value;      /* raw units */
  int32_t step;           /* raw units per press */
  uint8_t decimals;       /* display only */
  const char *unit;       /* "mm" */
} rammp_actuator_spec_t;

/* The table as an array; `count` gets its length (may be NULL). */
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

/* ==== Diagnostics ======================================================= */

/* Live readings from the actuators and anything else worth watching. Values are
   raw integers; `decN` is display only (2345 with 2 shows "23.45"). */

#define RAMMP_DIAG_FIELDS 3 /* readings per item the HMI shows */

/* D(id, NAME, short, label, unit1, dec1, unit2, dec2, unit3, dec3); id = row index.
   A unit of "" leaves that reading out. */
#define RAMMP_DIAG_TABLE(D)                                                                        \
  D(0, TEST_1, "T1", "Test actuator 1", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)           \
  D(1, TEST_2, "T2", "Test actuator 2", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)           \
  D(2, TEST_3, "T3", "Test actuator 3", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)

#define RAMMP_DIAG_COUNT_ONE(...) +1
#define RAMMP_DIAG_COUNT (0 RAMMP_DIAG_TABLE(RAMMP_DIAG_COUNT_ONE))

typedef struct rammp_diag_spec {
  uint8_t id;                          /* row index; position in items */
  const char *short_name;              /* "T1" */
  const char *label;                   /* "Test actuator 1" */
  const char *unit[RAMMP_DIAG_FIELDS]; /* "Temp [C]"; "" = reading unused */
  uint8_t decimals[RAMMP_DIAG_FIELDS]; /* display only */
} rammp_diag_spec_t;

/* The table as an array; `count` gets its length (may be NULL). */
static inline const rammp_diag_spec_t *rammp_diag_table(uint8_t *count) {
  static const rammp_diag_spec_t table[] = {
#define RAMMP_DIAG_ROW(id_, name_, short_, label_, u1_, d1_, u2_, d2_, u3_, d3_)                   \
  {(uint8_t)(id_),                                                                                 \
   short_,                                                                                         \
   label_,                                                                                         \
   {u1_, u2_, u3_},                                                                                \
   {(uint8_t)(d1_), (uint8_t)(d2_), (uint8_t)(d3_)}},
      RAMMP_DIAG_TABLE(RAMMP_DIAG_ROW)
#undef RAMMP_DIAG_ROW
  };
  if (count != NULL) {
    *count = (uint8_t)(sizeof(table) / sizeof(table[0]));
  }
  return table;
}

/* ==== Self test (bench only; checks in main/selftest_spec.h) ============ */

/* Rides the UInt32 bench topics, tagged in the top nibble:
     run   PC  -> HMI  HMI_COMMAND  TAG_RUN  | run_id[7:0]     1..255, a repeat is ignored
     ping  HMI -> PC   HMI_COUNTER  TAG_PING | seq[15:0]
     pong  PC  -> HMI  HMI_COMMAND  TAG_PONG | peer_rx[27:16] | seq[15:0]
   Untagged values are the plain heartbeat. The report is STARTED, one RESULT
   per check, FINISHED, then all of it again: dedupe on (run_id, kind, index). */

#define RAMMP_SELFTEST_TAG_MASK 0xF0000000u
#define RAMMP_SELFTEST_TAG_PING 0x50000000u
#define RAMMP_SELFTEST_TAG_PONG 0xA0000000u
#define RAMMP_SELFTEST_TAG_RUN 0xC0000000u

enum {
  RAMMP_SELFTEST_RESULT_PASS = 0,
  RAMMP_SELFTEST_RESULT_FAIL = 1,
  RAMMP_SELFTEST_RESULT_SKIP = 2, /* not measurable, and not required */
};

enum {
  RAMMP_SELFTEST_KIND_STARTED = 0,  /* count = checks to come, detail = firmware version */
  RAMMP_SELFTEST_KIND_RESULT = 1,   /* one check */
  RAMMP_SELFTEST_KIND_FINISHED = 2, /* value/lo/hi = pass/fail/skip totals */
};

/* ==== Names, for logs and labels ======================================== */

static inline const char *rammp_drive_status_name(uint8_t v) {
  return v == RAMMP_DRIVE_STATUS_INACTIVE ? "INACTIVE"
         : v == RAMMP_DRIVE_STATUS_ACTIVE ? "ACTIVE"
                                          : "?";
}

static inline const char *rammp_state_name(uint8_t v) {
  return v == RAMMP_STATE_OK ? "OK" : v == RAMMP_STATE_ERROR ? "ERROR" : "?";
}

static inline const char *rammp_actuator_result_name(uint8_t v) {
  switch (v) {
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

/* ==== HMI-raised warnings =============================================== */

/* The HMI's own text about the link, shown in the same banner as an MCB fault,
   so each fits RAMMP_ERROR_TEXT_LEN / RAMMP_ERROR_FOOTER_LEN. */

#define RAMMP_STR_(x) #x
#define RAMMP_STR(x) RAMMP_STR_(x)

#define RAMMP_HMI_LINK_REFUSED_TITLE "DRIVE REFUSED: RTPS LINK" /* drive entry refused */
#define RAMMP_HMI_MCB_REFUSED_TITLE "DRIVE REFUSED: MCB STATE"
#define RAMMP_HMI_SEAT_LINK_REFUSED_TITLE "SEAT REFUSED: RTPS LINK" /* seat entry refused */
#define RAMMP_HMI_SEAT_MCB_REFUSED_TITLE "SEAT REFUSED: MCB STATE"
#define RAMMP_HMI_LINK_LOST_TITLE "RTPS LINK LOST" /* lost on drive/seat screen */
#define RAMMP_HMI_MCB_FAULT_TITLE "MCB STATE FAULT"

/* body / footer per link state short of connected */
#define RAMMP_HMI_ETH_FAILED_TEXT "W5500 ETHERNET INIT FAILED AT BOOT"
#define RAMMP_HMI_ETH_FAILED_FOOTER "Power-cycle HMI to retry"
#define RAMMP_HMI_LINK_DOWN_TEXT "NO ETHERNET LINK"
#define RAMMP_HMI_LINK_DOWN_FOOTER "Check cable/switch to MCB"
#define RAMMP_HMI_NO_IP_TEXT "LINK UP, NO DHCP LEASE"
#define RAMMP_HMI_NO_IP_FOOTER "Check DHCP server"
#define RAMMP_HMI_NO_PEER_TEXT "NO McbStatus IN " RAMMP_STR(RAMMP_MCB_STATUS_TIMEOUT_MS) " MS"
#define RAMMP_HMI_NO_PEER_FOOTER "topic " RAMMP_TOPIC_MCB_STATUS

/* state != OK with an empty error_text: printf(state, rammp_state_name(state)) */
#define RAMMP_HMI_MCB_NO_TEXT_FMT "system_state=%u (%s), error_text empty"

#ifdef __cplusplus
} /* extern "C" */

/* ==== Messages (C++, serialized by espp/cdr) ============================= */

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace rammp {

/* MCB -> HMI, every RAMMP_MCB_STATUS_PERIOD_MS */
struct McbStatus {
  uint8_t drive_status;         // RAMMP_DRIVE_STATUS_*
  uint8_t system_state;         // RAMMP_STATE_*
  uint8_t flags;                // reserved, send 0
  uint8_t seq;                  // +1 per message, wraps
  uint8_t speed_tenths;         // 0..RAMMP_SPEED_MAX_TENTHS
  uint8_t hour, minute, second; // MCB local time
  uint8_t day, month, year;     // month 0 = time unknown (HMI ignores it); year since 2000
  std::string drive_text;       // "" = show the drive_status name
  std::string state_text;       // "" = show the system_state name
  std::string error_text;       // banner body while state != OK
  std::string error_footer;     // banner footer
};

/* HMI -> MCB. Calibrated: 0 at rest, deadzones applied, X/Y within the unit circle. */
struct AdcXYTwist {
  float x;             // -1 left .. +1 right
  float y;             // -1 back .. +1 forward
  float twist;         // -1 counter-clockwise .. +1 clockwise (always rotates in place)
  uint32_t buttons;    // RAMMP_BUTTON_* bits, 1 = pressed
  uint32_t drive_mode; // RAMMP_DRIVE_MODE_*, chosen on the HMI
};

/* HMI -> MCB: move one actuator by `steps` of its table step */
struct ActuatorCommand {
  uint8_t req_id;      // +1 per command, wraps; echoed back in ActuatorState
  uint8_t actuator_id; // RAMMP_ACTUATOR_TABLE row
  int8_t steps;        // -1 = one "-" press, +1 = one "+" press
};

/* MCB -> HMI, on change and every RAMMP_ACTUATOR_STATE_PERIOD_MS */
struct ActuatorState {
  uint8_t req_id;              // command this answers; 0 = none yet
  uint8_t result;              // RAMMP_ACTUATOR_RESULT_*
  uint8_t seq;                 // +1 per message, wraps
  std::vector<int32_t> values; // one per table row, raw units
};

/* MCB -> HMI, every RAMMP_DIAG_PERIOD_MS */
struct DiagItem {
  std::vector<int32_t> values; // raw readings, in RAMMP_DIAG_TABLE unit order
};
struct Diagnostics {
  uint8_t seq;                 // +1 per message, wraps
  std::vector<DiagItem> items; // one per RAMMP_DIAG_TABLE row
};

/* Bench topics (counter, command, brightness): std_msgs/UInt32 */
struct UInt32 {
  uint32_t data;
};

/* HMI -> PC, one per self-test check plus the start/summary markers */
struct SelfTestReport {
  uint8_t run_id;     // from the run command; 0 = started on the HMI
  uint8_t kind;       // RAMMP_SELFTEST_KIND_*
  uint8_t index;      // check position, 0-based
  uint8_t count;      // checks in the run
  uint8_t result;     // RAMMP_SELFTEST_RESULT_*
  int32_t value;      // meaningless on SKIP
  int32_t lo;         // inclusive; INT32_MIN = no limit
  int32_t hi;         // inclusive; INT32_MAX = no limit
  std::string name;   // "mem.int_free"
  std::string unit;   // "KB"
  std::string detail; // context, or why it failed
};

/* cdr::serialize() gives std::byte; RtpsParticipant::publish() takes uint8_t */
inline std::span<const uint8_t> as_u8(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()};
}

} // namespace rammp

#endif /* __cplusplus */

/* ==== Legacy hand-written codecs: replaced by espp/cdr, NOT the wire format ====
   Kept commented out for reference only (fixed-size C arrays, no CDR library). */
#if 0

#define RAMMP_ACTUATOR_MAX 8
#define RAMMP_DIAG_MAX 8
#define RAMMP_SELFTEST_NAME_LEN 24
#define RAMMP_SELFTEST_UNIT_LEN 8
#define RAMMP_SELFTEST_DETAIL_LEN 48

typedef struct rammp_mcb_status {
  uint8_t drive_status, system_state, flags, seq, speed_tenths;
  uint8_t hour, minute, second, day, month, year;
  char drive_text[RAMMP_MCB_TEXT_LEN];
  char state_text[RAMMP_MCB_TEXT_LEN];
  char error_text[RAMMP_ERROR_TEXT_LEN];
  char error_footer[RAMMP_ERROR_FOOTER_LEN];
} rammp_mcb_status_t;

typedef struct rammp_actuator_command {
  uint8_t req_id, actuator_id;
  int8_t steps;
  uint8_t reserved;
} rammp_actuator_command_t;

typedef struct rammp_actuator_state {
  uint8_t req_id, result, count, seq;
  int32_t values[RAMMP_ACTUATOR_MAX];
} rammp_actuator_state_t;

typedef struct rammp_diagnostics {
  uint8_t seq, count, reserved[2];
  int32_t values[RAMMP_DIAG_MAX][RAMMP_DIAG_FIELDS];
} rammp_diagnostics_t;

typedef struct rammp_selftest_report {
  uint8_t run_id, kind, index, count, result, reserved[3];
  int32_t value, lo, hi;
  char name[RAMMP_SELFTEST_NAME_LEN];
  char unit[RAMMP_SELFTEST_UNIT_LEN];
  char detail[RAMMP_SELFTEST_DETAIL_LEN];
} rammp_selftest_report_t;

#define RAMMP_CDR_HEADER_SIZE 4
#define RAMMP_MCB_STATUS_CDR_SIZE                                                                  \
  (RAMMP_CDR_HEADER_SIZE + 11 + 2 * RAMMP_MCB_TEXT_LEN + RAMMP_ERROR_TEXT_LEN +                    \
   RAMMP_ERROR_FOOTER_LEN)
#define RAMMP_ACTUATOR_COMMAND_CDR_SIZE (RAMMP_CDR_HEADER_SIZE + 4)
#define RAMMP_ACTUATOR_STATE_CDR_SIZE (RAMMP_CDR_HEADER_SIZE + 4 + 4 * RAMMP_ACTUATOR_MAX)
#define RAMMP_DIAG_CDR_SIZE (RAMMP_CDR_HEADER_SIZE + 4 + 4 * RAMMP_DIAG_MAX * RAMMP_DIAG_FIELDS)
#define RAMMP_SELFTEST_REPORT_CDR_SIZE                                                             \
  (RAMMP_CDR_HEADER_SIZE + 8 + 3 * 4 + RAMMP_SELFTEST_NAME_LEN + RAMMP_SELFTEST_UNIT_LEN +         \
   RAMMP_SELFTEST_DETAIL_LEN)

static inline void rammp_cdr_header(uint8_t *out) {
  out[0] = 0x00;
  out[1] = 0x01;
  out[2] = 0x00;
  out[3] = 0x00;
}

static inline bool rammp_cdr_ok(const uint8_t *in) { return in[0] == 0x00 && in[1] == 0x01; }

static inline void rammp_copy(uint8_t *dst, const uint8_t *src, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    dst[i] = src[i];
  }
}

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

static inline size_t rammp_mcb_status_encode(const rammp_mcb_status_t *s, uint8_t *out,
                                             size_t out_size) {
  uint8_t *p;
  if (s == NULL || out == NULL || out_size < RAMMP_MCB_STATUS_CDR_SIZE) {
    return 0;
  }
  rammp_cdr_header(out);
  p = out + RAMMP_CDR_HEADER_SIZE;
  p[0] = s->drive_status;
  p[1] = s->system_state;
  p[2] = s->flags;
  p[3] = s->seq;
  p[4] = s->speed_tenths;
  p[5] = s->hour;
  p[6] = s->minute;
  p[7] = s->second;
  p[8] = s->day;
  p[9] = s->month;
  p[10] = s->year;
  p += 11;
  rammp_copy(p, (const uint8_t *)s->drive_text, RAMMP_MCB_TEXT_LEN);
  p += RAMMP_MCB_TEXT_LEN;
  rammp_copy(p, (const uint8_t *)s->state_text, RAMMP_MCB_TEXT_LEN);
  p += RAMMP_MCB_TEXT_LEN;
  rammp_copy(p, (const uint8_t *)s->error_text, RAMMP_ERROR_TEXT_LEN);
  p += RAMMP_ERROR_TEXT_LEN;
  rammp_copy(p, (const uint8_t *)s->error_footer, RAMMP_ERROR_FOOTER_LEN);
  return RAMMP_MCB_STATUS_CDR_SIZE;
}

static inline bool rammp_mcb_status_decode(const uint8_t *in, size_t in_size,
                                           rammp_mcb_status_t *s) {
  const uint8_t *p;
  if (in == NULL || s == NULL || in_size < RAMMP_MCB_STATUS_CDR_SIZE || !rammp_cdr_ok(in)) {
    return false;
  }
  p = in + RAMMP_CDR_HEADER_SIZE;
  s->drive_status = p[0];
  s->system_state = p[1];
  s->flags = p[2];
  s->seq = p[3];
  s->speed_tenths = p[4];
  s->hour = p[5];
  s->minute = p[6];
  s->second = p[7];
  s->day = p[8];
  s->month = p[9];
  s->year = p[10];
  p += 11;
  rammp_copy((uint8_t *)s->drive_text, p, RAMMP_MCB_TEXT_LEN);
  p += RAMMP_MCB_TEXT_LEN;
  rammp_copy((uint8_t *)s->state_text, p, RAMMP_MCB_TEXT_LEN);
  p += RAMMP_MCB_TEXT_LEN;
  rammp_copy((uint8_t *)s->error_text, p, RAMMP_ERROR_TEXT_LEN);
  p += RAMMP_ERROR_TEXT_LEN;
  rammp_copy((uint8_t *)s->error_footer, p, RAMMP_ERROR_FOOTER_LEN);
  s->drive_text[RAMMP_MCB_TEXT_LEN - 1] = '\0';
  s->state_text[RAMMP_MCB_TEXT_LEN - 1] = '\0';
  s->error_text[RAMMP_ERROR_TEXT_LEN - 1] = '\0';
  s->error_footer[RAMMP_ERROR_FOOTER_LEN - 1] = '\0';
  return true;
}

static inline size_t rammp_actuator_command_encode(const rammp_actuator_command_t *c, uint8_t *out,
                                                   size_t out_size) {
  if (c == NULL || out == NULL || out_size < RAMMP_ACTUATOR_COMMAND_CDR_SIZE) {
    return 0;
  }
  rammp_cdr_header(out);
  out[4] = c->req_id;
  out[5] = c->actuator_id;
  out[6] = (uint8_t)c->steps;
  out[7] = c->reserved;
  return RAMMP_ACTUATOR_COMMAND_CDR_SIZE;
}

static inline bool rammp_actuator_command_decode(const uint8_t *in, size_t in_size,
                                                 rammp_actuator_command_t *c) {
  if (in == NULL || c == NULL || in_size < RAMMP_ACTUATOR_COMMAND_CDR_SIZE || !rammp_cdr_ok(in)) {
    return false;
  }
  c->req_id = in[4];
  c->actuator_id = in[5];
  c->steps = (int8_t)in[6];
  c->reserved = in[7];
  return true;
}

static inline size_t rammp_actuator_state_encode(const rammp_actuator_state_t *s, uint8_t *out,
                                                 size_t out_size) {
  size_t i;
  if (s == NULL || out == NULL || out_size < RAMMP_ACTUATOR_STATE_CDR_SIZE) {
    return 0;
  }
  rammp_cdr_header(out);
  out[4] = s->req_id;
  out[5] = s->result;
  out[6] = s->count;
  out[7] = s->seq;
  for (i = 0; i < RAMMP_ACTUATOR_MAX; ++i) {
    rammp_write_u32_le(out + 8 + 4 * i, (uint32_t)s->values[i]);
  }
  return RAMMP_ACTUATOR_STATE_CDR_SIZE;
}

static inline bool rammp_actuator_state_decode(const uint8_t *in, size_t in_size,
                                               rammp_actuator_state_t *s) {
  size_t i;
  if (in == NULL || s == NULL || in_size < RAMMP_ACTUATOR_STATE_CDR_SIZE || !rammp_cdr_ok(in)) {
    return false;
  }
  s->req_id = in[4];
  s->result = in[5];
  s->count = in[6] > RAMMP_ACTUATOR_MAX ? RAMMP_ACTUATOR_MAX : in[6];
  s->seq = in[7];
  for (i = 0; i < RAMMP_ACTUATOR_MAX; ++i) {
    s->values[i] = (int32_t)rammp_read_u32_le(in + 8 + 4 * i);
  }
  return true;
}

static inline size_t rammp_selftest_report_encode(const rammp_selftest_report_t *r, uint8_t *out,
                                                  size_t out_size) {
  if (r == NULL || out == NULL || out_size < RAMMP_SELFTEST_REPORT_CDR_SIZE) {
    return 0;
  }
  rammp_cdr_header(out);
  out[4] = r->run_id;
  out[5] = r->kind;
  out[6] = r->index;
  out[7] = r->count;
  out[8] = r->result;
  out[9] = out[10] = out[11] = 0;
  rammp_write_u32_le(out + 12, (uint32_t)r->value);
  rammp_write_u32_le(out + 16, (uint32_t)r->lo);
  rammp_write_u32_le(out + 20, (uint32_t)r->hi);
  rammp_copy(out + 24, (const uint8_t *)r->name, RAMMP_SELFTEST_NAME_LEN);
  rammp_copy(out + 24 + RAMMP_SELFTEST_NAME_LEN, (const uint8_t *)r->unit, RAMMP_SELFTEST_UNIT_LEN);
  rammp_copy(out + 24 + RAMMP_SELFTEST_NAME_LEN + RAMMP_SELFTEST_UNIT_LEN,
             (const uint8_t *)r->detail, RAMMP_SELFTEST_DETAIL_LEN);
  return RAMMP_SELFTEST_REPORT_CDR_SIZE;
}

static inline size_t rammp_diagnostics_encode(const rammp_diagnostics_t *d, uint8_t *out,
                                              size_t out_size) {
  size_t i, f;
  if (d == NULL || out == NULL || out_size < RAMMP_DIAG_CDR_SIZE) {
    return 0;
  }
  rammp_cdr_header(out);
  out[4] = d->seq;
  out[5] = d->count;
  out[6] = out[7] = 0;
  for (i = 0; i < RAMMP_DIAG_MAX; ++i) {
    for (f = 0; f < RAMMP_DIAG_FIELDS; ++f) {
      rammp_write_u32_le(out + 8 + 4 * (i * RAMMP_DIAG_FIELDS + f), (uint32_t)d->values[i][f]);
    }
  }
  return RAMMP_DIAG_CDR_SIZE;
}

static inline bool rammp_diagnostics_decode(const uint8_t *in, size_t in_size,
                                            rammp_diagnostics_t *d) {
  size_t i, f;
  if (in == NULL || d == NULL || in_size < RAMMP_DIAG_CDR_SIZE || !rammp_cdr_ok(in)) {
    return false;
  }
  d->seq = in[4];
  d->count = in[5] > RAMMP_DIAG_MAX ? RAMMP_DIAG_MAX : in[5];
  d->reserved[0] = d->reserved[1] = 0;
  for (i = 0; i < RAMMP_DIAG_MAX; ++i) {
    for (f = 0; f < RAMMP_DIAG_FIELDS; ++f) {
      d->values[i][f] = (int32_t)rammp_read_u32_le(in + 8 + 4 * (i * RAMMP_DIAG_FIELDS + f));
    }
  }
  return true;
}

#endif /* legacy codecs */

#endif /* RAMMP_RTPS_SPEC_H */
