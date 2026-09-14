/*
 * rammp_rtps_spec.h - RAMMP RTPS wire spec (C++20), shared by the HMI and the MCB.
 *
 * - Encoding: espp/cdr, XCDR1 (classic little-endian CDR, what DDS / ROS 2 peers speak).
 * - The structs below ARE the wire layout: fields in order, no hand-written codec.
 * - Every enum is scoped with a fixed wire width (`enum class X : uint8_t`), and every
 *   topic carries its message type (Topic<McbStatus>), so the wrong value, message or
 *   handler for a topic does not compile.
 * - IDL mapping: uint8_t / enum : uint8_t = octet, int8_t = int8, int32_t = long,
 *   uint32_t / enum : uint32_t = unsigned long, float = float, std::string = string,
 *   std::vector<T> = sequence<T>.
 * - Roles: the MCB owns the vehicle state; the HMI shows it and asks.
 * - Every topic is best-effort, no durability: state is resent periodically.
 * - scripts/rammp_rtps.py reads this file (topics, enums, constants, table rows) and
 *   mirrors the structs: change both together.
 *
 * Example: an MCB on the same espp / esp-idf stack sending Diagnostics to the HMI
 *
 *   #include "rtps_pubsub.hpp"
 *   #include "rammp_rtps_spec.h"
 *
 *   espp::RtpsParticipant rtps({.interface_address = my_ip});
 *   rtps.start();
 *   espp::Publisher<rammp::Diagnostics> diag_pub(rtps, {.topic = rammp::kMcbDiagnostics.name,
 *                                                       .type_name = rammp::kMcbDiagnostics.type});
 *
 *   // every kDiagPeriod; one item per RAMMP_DIAG_TABLE row, raw integers
 *   // T1 = 30.5 C, 1.50 A, 45.0 deg; T2 = 29.8 C, 1.20 A, -9.0 deg
 *   diag_pub.publish({.seq = seq++,
 *                     .items = {{.values = {305, 150, 450}}, {.values = {298, 120, -90}}}});
 *
 *   // and the other way: the HMI's actuator requests (answer with an ActuatorState)
 *   espp::Subscriber<rammp::ActuatorCommand> cmd_sub(
 *       rtps, {.topic = rammp::kActuatorCommand.name,
 *              .type_name = rammp::kActuatorCommand.type,
 *              .on_message = [](const rammp::ActuatorCommand &cmd) {
 *                move_actuator(cmd.actuator_id, cmd.steps); // actuator_id is an ActuatorId
 *              }});
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rammp {

using std::chrono::milliseconds;

/* ==== Topic and type names: the only place they are spelled ============== */
/* ROS 2 later? Change them here only ("rt/..." topics, "pkg::msg::dds_::Name_" types). */

struct McbStatus;
struct ActuatorState;
struct Diagnostics;
struct AdcXYTwist;
struct ActuatorCommand;
struct UInt32;
struct SelfTestReport;

/// A DDS topic and type name, tied to the message it carries.
template <class Message> struct Topic {
  const char *name; // DDS topic name
  const char *type; // DDS type name
};

// MCB -> HMI
inline constexpr Topic<McbStatus> kMcbStatus{"rammp/mcb/status", "rammp/msg/McbStatus"};
inline constexpr Topic<ActuatorState> kActuatorState{"rammp/actuator/state",
                                                     "rammp/msg/ActuatorState"};
inline constexpr Topic<Diagnostics> kMcbDiagnostics{"rammp/mcb/diagnostics",
                                                    "rammp/msg/Diagnostics"};

// HMI -> MCB
inline constexpr Topic<AdcXYTwist> kJoystickAdc{"rammp/joystick/adc", "rammp/msg/AdcXYTwist"};
inline constexpr Topic<ActuatorCommand> kActuatorCommand{"rammp/actuator/command",
                                                         "rammp/msg/ActuatorCommand"};

// Bench PC <-> HMI. A production MCB can ignore these.
inline constexpr Topic<UInt32> kHmiCounter{"rammp/hmi/counter", "std_msgs/msg/UInt32"};
inline constexpr Topic<UInt32> kHmiCommand{"rammp/hmi/command", "std_msgs/msg/UInt32"};
inline constexpr Topic<UInt32> kHmiBrightness{"rammp/hmi/brightness", "std_msgs/msg/UInt32"};
inline constexpr Topic<SelfTestReport> kSelfTestReport{"rammp/selftest/report",
                                                       "rammp/msg/SelfTestReport"};

/* ==== Timing ============================================================ */

inline constexpr milliseconds kMcbStatusPeriod{500};     // MCB sends McbStatus this often
inline constexpr milliseconds kMcbStatusTimeout{2000};   // HMI: none this long = link lost
inline constexpr milliseconds kActuatorStatePeriod{500}; // MCB resends ActuatorState
inline constexpr milliseconds kDiagPeriod{500};          // MCB sends Diagnostics
inline constexpr milliseconds kDiagTimeout{2000};        // HMI: none this long = stale, red

/* ==== McbStatus ========================================================= */

enum class DriveStatus : uint8_t {
  INACTIVE = 0, // chair ignores the stick
  ACTIVE = 1,   // chair drives on the stick
};

enum class SystemState : uint8_t {
  OK = 0,
  ERROR = 1, // HMI blocks drive/seat and shows error_text
};

inline constexpr uint8_t kSpeedMaxTenths = 99; // speed_tenths 0..99, shown as 0.0..9.9
inline constexpr size_t kMcbTextLen = 16;      // HMI shows up to 15 chars of drive/state_text
inline constexpr size_t kErrorTextLen = 64;    // HMI shows up to 63 chars of error_text
inline constexpr size_t kErrorFooterLen = 32;  // HMI shows up to 31 chars of error_footer

/* ==== Joystick ========================================================== */

enum class Buttons : uint32_t { // a bit set; 1 = pressed
  NONE = 0x0,
  JOYSTICK = 0x1, // the stick's button
};
constexpr Buttons operator|(Buttons a, Buttons b) {
  return static_cast<Buttons>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool has(Buttons set, Buttons button) {
  return (static_cast<uint32_t>(set) & static_cast<uint32_t>(button)) != 0;
}

enum class DriveMode : uint32_t {
  NORMAL = 0, // car-like: Y drives, X steers
  HOLO = 1,   // holonomic: X/Y is the velocity vector
  AUTO = 2,   // reserved, not implemented
};

/* ==== Actuators ========================================================= */

/* The MCB owns every position; the HMI sends step requests and shows what comes back.
   Values are raw integers; `decimals` is display only (2500 with 1 shows "250.0"). */

/* To add an actuator, add ONE X(...) row: next id, and a trailing `\` on every row but the
   last. ActuatorId, the DEBUG ACTUATORS page and the python tools pick it up; the MCB then
   sends one more value in ActuatorState.values and accepts the new id in ActuatorCommand. */

/* X(id, NAME, short, label, min, max, step, decimals, unit); id = row index */
#define RAMMP_ACTUATOR_TABLE(X)                                                                    \
  X(0, ELEVATION, "M1", "Elevation", 0, 2500, 50, 1, "mm")                                         \
  X(1, REAR_TILT, "M2", "Rear Tilt", 0, 900, 25, 1, "deg")                                         \
  X(2, FORWARD_TILT, "M3", "Forward Tilt", 0, 450, 25, 1, "deg")                                   \
  X(3, SIDE_TILT, "M4", "Side Tilt", -300, 300, 25, 1, "deg")

enum class ActuatorId : uint8_t { // ELEVATION = 0, ... (the table's rows)
#define RAMMP_ACTUATOR_ID(id_, name_, ...) name_ = id_,
  RAMMP_ACTUATOR_TABLE(RAMMP_ACTUATOR_ID)
#undef RAMMP_ACTUATOR_ID
};

struct ActuatorSpec {
  ActuatorId id;
  const char *short_name; // "M1"
  const char *label;      // "Elevation"
  int32_t min_value;      // raw units
  int32_t max_value;      // raw units
  int32_t step;           // raw units per press
  uint8_t decimals;       // display only
  const char *unit;       // "mm"
};

inline constexpr std::array kActuators{
#define RAMMP_ACTUATOR_ROW(id_, name_, short_, label_, min_, max_, step_, dec_, unit_)             \
  ActuatorSpec{ActuatorId::name_, short_, label_, min_, max_, step_, dec_, unit_},
    RAMMP_ACTUATOR_TABLE(RAMMP_ACTUATOR_ROW)
#undef RAMMP_ACTUATOR_ROW
};
inline constexpr size_t kActuatorCount = kActuators.size();

constexpr size_t index_of(ActuatorId id) { return static_cast<size_t>(id); }

/* The MCB's verdict on a command. Anything but OK = value unchanged. */
enum class ActuatorResult : uint8_t {
  OK = 0,
  AT_MIN = 1,     // already at its low end
  AT_MAX = 2,     // already at its high end
  INHIBITED = 3,  // refused now: interlock, fault, driving
  UNKNOWN_ID = 4, // no such actuator
};

/* ==== Diagnostics ======================================================= */

/* Live readings from the actuators and anything else worth watching. Values are
   raw integers; `decN` is display only (2345 with 2 shows "23.45"). */

inline constexpr size_t kDiagFields = 3; // readings per item the HMI shows

/* To add a diagnostics item, add ONE D(...) row: next id, same `\` rule. The
   DiagnosticsScreen and the python tools pick it up; the MCB then sends one more DiagItem. */

/* D(id, NAME, short, label, unit1, dec1, unit2, dec2, unit3, dec3); id = row index.
   A unit of "" leaves that reading out. */
#define RAMMP_DIAG_TABLE(D)                                                                        \
  D(0, TEST_1, "T1", "Test actuator 1", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)           \
  D(1, TEST_2, "T2", "Test actuator 2", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)           \
  D(2, TEST_3, "T3", "Test actuator 3", "Temp [C]", 1, "Current [A]", 2, "Pos [deg]", 1)

enum class DiagId : uint8_t { // TEST_1 = 0, ... (the table's rows)
#define RAMMP_DIAG_ID(id_, name_, ...) name_ = id_,
  RAMMP_DIAG_TABLE(RAMMP_DIAG_ID)
#undef RAMMP_DIAG_ID
};

struct DiagSpec {
  DiagId id;
  const char *short_name;                     // "T1"
  const char *label;                          // "Test actuator 1"
  std::array<const char *, kDiagFields> unit; // "Temp [C]"; "" = reading unused
  std::array<uint8_t, kDiagFields> decimals;  // display only
};

inline constexpr std::array kDiagItems{
#define RAMMP_DIAG_ROW(id_, name_, short_, label_, u1_, d1_, u2_, d2_, u3_, d3_)                   \
  DiagSpec{DiagId::name_, short_, label_, {u1_, u2_, u3_}, {d1_, d2_, d3_}},
    RAMMP_DIAG_TABLE(RAMMP_DIAG_ROW)
#undef RAMMP_DIAG_ROW
};
inline constexpr size_t kDiagCount = kDiagItems.size();

/* ==== Self test (bench only; checks in main/selftest_spec.h) ============ */

/* Rides the UInt32 bench topics, tagged in the top nibble:
     run   PC  -> HMI  kHmiCommand  RUN  | run_id[7:0]     1..255, a repeat is ignored
     ping  HMI -> PC   kHmiCounter  PING | seq[15:0]
     pong  PC  -> HMI  kHmiCommand  PONG | peer_rx[27:16] | seq[15:0]
   Untagged values are the plain heartbeat. The report is STARTED, one RESULT
   per check, FINISHED, then all of it again: dedupe on (run_id, kind, index). */

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

/* ==== Messages (serialized by espp/cdr) ================================= */

/* MCB -> HMI, every kMcbStatusPeriod */
struct McbStatus {
  DriveStatus drive_status;     // what the chair does with the stick
  SystemState system_state;     // OK, or a fault that blocks drive/seat
  uint8_t flags;                // reserved, send 0
  uint8_t seq;                  // +1 per message, wraps
  uint8_t speed_tenths;         // 0..kSpeedMaxTenths
  uint8_t hour, minute, second; // MCB local time
  uint8_t day, month, year;     // month 0 = time unknown (HMI ignores it); year since 2000
  std::string drive_text;       // "" = show the drive_status name
  std::string state_text;       // "" = show the system_state name
  std::string error_text;       // banner body while state != OK
  std::string error_footer;     // banner footer
};

/* HMI -> MCB. Calibrated: 0 at rest, deadzones applied, X/Y within the unit circle. */
struct AdcXYTwist {
  float x;              // -1 left .. +1 right
  float y;              // -1 back .. +1 forward
  float twist;          // -1 counter-clockwise .. +1 clockwise (always rotates in place)
  Buttons buttons;      // pressed buttons
  DriveMode drive_mode; // chosen on the HMI
};

/* HMI -> MCB: move one actuator by `steps` of its table step */
struct ActuatorCommand {
  uint8_t req_id;         // +1 per command, wraps; echoed back in ActuatorState
  ActuatorId actuator_id; // RAMMP_ACTUATOR_TABLE row
  int8_t steps;           // -1 = one "-" press, +1 = one "+" press
};

/* MCB -> HMI, on change and every kActuatorStatePeriod */
struct ActuatorState {
  uint8_t req_id;              // command this answers; 0 = none yet
  ActuatorResult result;       // verdict on that command
  uint8_t seq;                 // +1 per message, wraps
  std::vector<int32_t> values; // one per table row, raw units
};

/* MCB -> HMI, every kDiagPeriod */
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

/* ==== Names, for logs and labels ======================================== */

constexpr const char *to_string(DriveStatus v) {
  switch (v) {
  case DriveStatus::INACTIVE:
    return "INACTIVE";
  case DriveStatus::ACTIVE:
    return "ACTIVE";
  }
  return "?";
}

constexpr const char *to_string(SystemState v) {
  switch (v) {
  case SystemState::OK:
    return "OK";
  case SystemState::ERROR:
    return "ERROR";
  }
  return "?";
}

constexpr const char *to_string(ActuatorResult v) {
  switch (v) {
  case ActuatorResult::OK:
    return "OK";
  case ActuatorResult::AT_MIN:
    return "AT_MIN";
  case ActuatorResult::AT_MAX:
    return "AT_MAX";
  case ActuatorResult::INHIBITED:
    return "INHIBITED";
  case ActuatorResult::UNKNOWN_ID:
    return "UNKNOWN_ID";
  }
  return "?";
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

/* body / footer per link state short of connected */
inline constexpr char kHmiEthFailedText[] = "W5500 ETHERNET INIT FAILED AT BOOT";
inline constexpr char kHmiEthFailedFooter[] = "Power-cycle HMI to retry";
inline constexpr char kHmiLinkDownText[] = "NO ETHERNET LINK";
inline constexpr char kHmiLinkDownFooter[] = "Check cable/switch to MCB";
inline constexpr char kHmiNoIpText[] = "LINK UP, NO DHCP LEASE";
inline constexpr char kHmiNoIpFooter[] = "Check DHCP server";
inline constexpr char kHmiNoPeerText[] = "NO McbStatus IN 2000 MS";
inline constexpr char kHmiNoPeerFooter[] = "topic rammp/mcb/status";
static_assert(kMcbStatusTimeout == milliseconds{2000}, "update kHmiNoPeerText");
static_assert(std::string_view(kHmiNoPeerFooter).ends_with(kMcbStatus.name),
              "update kHmiNoPeerFooter");

/* state != OK with an empty error_text: printf(state number, to_string(state)) */
inline constexpr char kHmiMcbNoTextFmt[] = "system_state=%u (%s), error_text empty";

/* cdr::serialize() gives std::byte; RtpsParticipant::publish() takes uint8_t */
inline std::span<const uint8_t> as_u8(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()};
}

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
