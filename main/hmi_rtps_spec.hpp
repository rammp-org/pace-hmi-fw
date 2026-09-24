/*
 * hmi_rtps_spec.hpp - what only this HMI (pace-hmi-fw) adds to the shared RTPS spec.
 *
 * - The shared messages, topics, enums and tables (every RAMMP device):
 *   messages/joystick_message.hpp, in the rammp-rtps submodule (external/rammp-rtps).
 * - Here, what no other device needs: names and helpers, this HMI's timing and
 *   display limits, its bench-only self-test topics and the warning texts it raises
 *   itself. Same rules: scoped enums, typed topics, C++20.
 * - scripts/rammp_rtps.py reads both files.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "messages/joystick_message.hpp"
#include "messages/mib_message.hpp"

/* Bench PC <-> HMI topics and types (a production MCB can ignore these) */
#define RAMMP_TOPIC_HMI_COUNTER "rammp/hmi/counter"         /* HMI -> PC: heartbeat, ping */
#define RAMMP_TOPIC_HMI_COMMAND "rammp/hmi/command"         /* PC -> HMI: self-test run, pong */
#define RAMMP_TOPIC_HMI_BRIGHTNESS "rammp/hmi/brightness"   /* PC -> HMI: backlight % */
#define RAMMP_TYPE_UINT32 "std_msgs/msg/UInt32"             /* the three above */
#define RAMMP_TOPIC_SELFTEST_REPORT "rammp/selftest/report" /* HMI -> PC */
#define RAMMP_TYPE_SELFTEST_REPORT "rammp/msg/SelfTestReport"

namespace rammp {

using std::chrono::milliseconds;

/* ==== Names and helpers ================================================= */

// SeatAxis -> its row in kSeatAxes and SeatState.values: to range-check the MCB's
// answer and find the row a refused request flashes.
constexpr size_t index_of(SeatAxis axis) { return static_cast<size_t>(axis); }

// Names for the status labels, the fault banner (when the MCB sends no text) and the
// logs. A value this firmware does not know reads "?".
// Names for the status labels, the fault banner (when the MIB sends no text) and the
// logs. A value this firmware does not know reads "?".
constexpr const char *to_string(MIB::MibSystemState v) {
  return v == MIB::MibSystemState::INITIALIZING ? "INITIALIZING"
         : v == MIB::MibSystemState::IDLE       ? "IDLE"
         : v == MIB::MibSystemState::ENABLED    ? "ENABLED"
         : v == MIB::MibSystemState::ERROR      ? "ERROR"
                                                : "?";
}

constexpr const char *to_string(MIB::DriveProfile v) {
  return v == MIB::DriveProfile::LOW      ? "LOW"
         : v == MIB::DriveProfile::NORMAL ? "NORMAL"
         : v == MIB::DriveProfile::HIGH   ? "HIGH"
                                          : "?";
}

/* ==== Seat: the MIB's fields, and its units ============================= */

// One MIB::seatState field per RAMMP_SEAT_AXIS_TABLE row, in the table's order, so the
// table's id is the index for both. The only place the two are tied together: everything
// else works from the table.
constexpr float seat_field(const MIB::seatState &seat, SeatAxis axis) {
  switch (axis) {
  case SeatAxis::FRONT_BACK_TILT:
    return seat.front_back_tilt;
  case SeatAxis::LATERAL_TILT:
    return seat.lateral_tilt;
  case SeatAxis::ELEVATION:
    return seat.elevation;
  case SeatAxis::TRANSLATION:
    return seat.translation;
  }
  return 0.0f;
}

// The wire carries whole units (12.5 deg); the screens step and draw raw integers (125,
// with the row's decimals). One conversion each way, at the wire boundary and nowhere
// else, so the UI stays integer - which is what lv_subject_t holds.
constexpr int32_t scale_of(const SeatAxisSpec &spec) {
  int32_t scale = 1;
  for (uint8_t i = 0; i < spec.decimals; i++) {
    scale *= 10;
  }
  return scale;
}

constexpr int32_t seat_raw(const SeatAxisSpec &spec, float value) {
  const float scaled = value * static_cast<float>(scale_of(spec));
  // Rounded away from zero by hand: std::lround is not constexpr, and a plain cast
  // truncates, which would drift a value down one step every time it round-trips.
  return static_cast<int32_t>(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

constexpr float seat_units(const SeatAxisSpec &spec, int32_t raw) {
  return static_cast<float>(raw) / static_cast<float>(scale_of(spec));
}

/* ==== Timing (what this HMI expects of the MCB) ========================= */

inline constexpr milliseconds kMibStatusPeriod{500};   // the MIB sends MibStatus this often
inline constexpr milliseconds kMibStatusTimeout{2000}; // none this long = link lost
inline constexpr milliseconds kDiagPeriod{500};        // the MIB sends Diagnostics
inline constexpr milliseconds kDiagTimeout{2000};      // none this long = stale, red

/* ==== Display limits ==================================================== */

inline constexpr size_t kMcbTextLen = 16;     // shows up to 15 chars of status_text
inline constexpr size_t kErrorTextLen = 64;   // shows up to 63 chars of error_message
inline constexpr size_t kErrorFooterLen = 32; // shows up to 31 chars of error_footer

/* The speed readout: MibStatus.speed is metres per second, the DriveScreen's
   UnitLabel says "mph", and SpeedNumber has room for one digit either side of the
   point. Both live here rather than in the shared spec, which carries the real
   quantity and leaves the unit on the dial to whoever draws it. */
inline constexpr float kMphPerMps = 2.236936f;
inline constexpr int32_t kSpeedMaxTenths = 99; // 9.9 mph, the widest the label fits

/* ==== Bench PC <-> HMI (a production MCB can ignore these) ============== */

/* std_msgs/UInt32: the counter, command and brightness topics */
struct UInt32 {
  uint32_t data;
};

enum class SelfTestResult : uint8_t {
  PASS = 0,
  FAIL = 1,
  SKIP = 2, // not measurable, and not required
};

enum class SelfTestKind : uint8_t {
  STARTED = 0,  // count = checks to come, detail = firmware version
  RESULT = 1,   // one check
  FINISHED = 2, // value/lo/hi = pass/fail/skip totals
};

/* HMI -> PC, one per self-test check plus the start/summary markers */
struct SelfTestReport {
  uint8_t run_id;        // from the run command; 0 = started on the HMI
  SelfTestKind kind;     // STARTED, RESULT or FINISHED
  uint8_t index;         // check position, 0-based
  uint8_t count;         // checks in the run
  SelfTestResult result; // PASS, FAIL or SKIP
  int32_t value;         // meaningless on SKIP
  int32_t lo;            // inclusive; INT32_MIN = no limit
  int32_t hi;            // inclusive; INT32_MAX = no limit
  std::string name;      // "mem.int_free"
  std::string unit;      // "KB"
  std::string detail;    // context, or why it failed
};

inline constexpr Topic<UInt32> kHmiCounter{RAMMP_TOPIC_HMI_COUNTER, RAMMP_TYPE_UINT32};
inline constexpr Topic<UInt32> kHmiCommand{RAMMP_TOPIC_HMI_COMMAND, RAMMP_TYPE_UINT32};
inline constexpr Topic<UInt32> kHmiBrightness{RAMMP_TOPIC_HMI_BRIGHTNESS, RAMMP_TYPE_UINT32};
inline constexpr Topic<SelfTestReport> kSelfTestReport{RAMMP_TOPIC_SELFTEST_REPORT,
                                                       RAMMP_TYPE_SELFTEST_REPORT};

/* The self test rides the UInt32 bench topics, tagged in the top nibble:
     run   PC  -> HMI  kHmiCommand  RUN  | run_id[7:0]     1..255, a repeat is ignored
     ping  HMI -> PC   kHmiCounter  PING | seq[15:0]
     pong  PC  -> HMI  kHmiCommand  PONG | peer_rx[27:16] | seq[15:0]
   Untagged values are the plain heartbeat. The report is STARTED, one RESULT
   per check, FINISHED, then all of it again: dedupe on (run_id, kind, index).
   The checks themselves: main/selftest_spec.h. */

inline constexpr uint32_t kSelfTestTagMask = 0xF0000000;

enum class SelfTestTag : uint32_t {
  NONE = 0x00000000, // the plain heartbeat
  PING = 0x50000000,
  PONG = 0xA0000000,
  RUN = 0xC0000000,
};
constexpr SelfTestTag tag_of(uint32_t value) {
  return static_cast<SelfTestTag>(value & kSelfTestTagMask);
}
constexpr uint32_t tagged(SelfTestTag tag, uint32_t payload) {
  return static_cast<uint32_t>(tag) | (payload & ~kSelfTestTagMask);
}

/* ==== HMI-raised warnings =============================================== */

/* The HMI's own text about the link, shown in the same banner as an MCB fault,
   so each fits kErrorTextLen / kErrorFooterLen. */

inline constexpr char kHmiLinkRefusedTitle[] = "DRIVE REFUSED: RTPS LINK"; // drive entry refused
inline constexpr char kHmiMcbRefusedTitle[] = "DRIVE REFUSED: MCB STATE";
inline constexpr char kHmiSeatLinkRefusedTitle[] = "SEAT REFUSED: RTPS LINK"; // seat refused
inline constexpr char kHmiSeatMcbRefusedTitle[] = "SEAT REFUSED: MCB STATE";
inline constexpr char kHmiLinkLostTitle[] = "RTPS LINK LOST"; // lost on drive/seat screen
inline constexpr char kHmiMcbFaultTitle[] = "MCB STATE FAULT";

/* The MIB answered, or did not: a request that went out and did not get what it
   asked for. The body is the MIB's own error_message when it sent one, so these are
   the fallback wording for when it did not. */
inline constexpr char kHmiDriveNotGrantedTitle[] = "DRIVE REFUSED: NOT GRANTED";
inline constexpr char kHmiDriveNotGrantedText[] = "MIB DID NOT ENABLE DRIVING";
inline constexpr char kHmiDriveNotGrantedFooter[] = "Request timed out";
inline constexpr char kHmiDriveStoppedTitle[] = "DRIVING STOPPED";
inline constexpr char kHmiDriveStoppedText[] = "MIB DISABLED DRIVING";
inline constexpr char kHmiDriveStoppedFooter[] = "ACTIVATE DRIVE to ask again";
inline constexpr char kHmiExitRefusedTitle[] = "EXIT REFUSED";
inline constexpr char kHmiExitRefusedText[] = "MIB IS STILL DRIVING";
inline constexpr char kHmiExitRefusedFooter[] = "Hold the button again";
static_assert(sizeof(kHmiDriveNotGrantedText) <= kErrorTextLen &&
                  sizeof(kHmiDriveStoppedText) <= kErrorTextLen &&
                  sizeof(kHmiExitRefusedText) <= kErrorTextLen,
              "refusal body outgrows the banner it shares with MIB faults");
static_assert(sizeof(kHmiDriveNotGrantedFooter) <= kErrorFooterLen &&
                  sizeof(kHmiDriveStoppedFooter) <= kErrorFooterLen &&
                  sizeof(kHmiExitRefusedFooter) <= kErrorFooterLen,
              "refusal footer outgrows the banner it shares with MIB faults");

/* body / footer per link state short of connected */
inline constexpr char kHmiEthFailedText[] = "W5500 ETHERNET INIT FAILED AT BOOT";
inline constexpr char kHmiEthFailedFooter[] = "Power-cycle HMI to retry";
inline constexpr char kHmiLinkDownText[] = "NO ETHERNET LINK";
inline constexpr char kHmiLinkDownFooter[] = "Check cable/switch to MCB";
inline constexpr char kHmiNoIpText[] = "LINK UP, NO DHCP LEASE";
inline constexpr char kHmiNoIpFooter[] = "Check DHCP server";
inline constexpr char kHmiNoPeerText[] = "NO MibStatus IN 2000 MS";
inline constexpr char kHmiNoPeerFooter[] = "topic rammp/mib/status";
static_assert(kMibStatusTimeout == milliseconds{2000}, "update kHmiNoPeerText");
static_assert(std::string_view(kHmiNoPeerFooter).ends_with(MIB::kMibStatus.name),
              "update kHmiNoPeerFooter");

/* ERROR with an empty error_message: printf(state number, to_string(state)) */
inline constexpr char kHmiMcbNoTextFmt[] = "systemState=%u (%s), error_message empty";

} // namespace rammp

/* ==== Legacy hand-written codecs: replaced by espp/cdr, NOT the wire format ====
   Kept commented out for reference only (plain C, fixed-size arrays, no CDR library). */
#if 0

#define RAMMP_MCB_TEXT_LEN 16
#define RAMMP_ERROR_TEXT_LEN 64
#define RAMMP_ERROR_FOOTER_LEN 32
#define RAMMP_ACTUATOR_MAX 8
#define RAMMP_DIAG_MAX 8
#define RAMMP_DIAG_FIELDS 3
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
