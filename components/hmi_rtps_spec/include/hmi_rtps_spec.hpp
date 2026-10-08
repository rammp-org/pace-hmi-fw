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
   The checks themselves: main/selftest_spec.hpp. */

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
// Driving, and then the link or the MCB went: not a refusal -- nothing was
// asked -- so it says what happened.
inline constexpr char kHmiDriveLostLinkTitle[] = "DRIVING STOPPED: RTPS LINK";
inline constexpr char kHmiDriveLostMcbTitle[] = "DRIVING STOPPED: MCB STATE";
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
inline constexpr char kHmiDriveStoppedFooter[] = "Hold the stick button again";
inline constexpr char kHmiExitRefusedTitle[] = "EXIT REFUSED";
inline constexpr char kHmiExitRefusedText[] = "MIB IS STILL DRIVING";
inline constexpr char kHmiExitRefusedFooter[] = "Try the menu or button again";
static_assert(sizeof(kHmiDriveNotGrantedText) <= kErrorTextLen &&
                  sizeof(kHmiDriveStoppedText) <= kErrorTextLen &&
                  sizeof(kHmiExitRefusedText) <= kErrorTextLen,
              "refusal body outgrows the banner it shares with MIB faults");

/* The Drive screen's notice (docs/plans/hazard-c1-spec.md §2.7, §3.3; texts approved by the
   owner, decision F1, 2026-10-08): the user's stop, then why the stick is held. Its own slot,
   not the refusal banner. One table for every hazard fix; each text has one owner spec. */
inline constexpr char kHmiNoticeMcbDidNotStop[] = "MCB did not stop";                      // C1
inline constexpr char kHmiNoticeStopping[] = "Stopping: waiting for the MCB";              // C1
inline constexpr char kHmiNoticeWaitingForMcb[] = "Waiting for the MCB";                   // C4
inline constexpr char kHmiNoticeNotCalibrated[] = "The joystick must be calibrated first"; // C1
inline constexpr char kHmiNoticePostNotRun[] = "Start-up check not run";                   // C1
inline constexpr char kHmiNoticeStickFault[] = "Joystick fault";                // C1, until C2
inline constexpr char kHmiNoticeStickCheck[] = "Checking the joystick";         // C2
inline constexpr char kHmiNoticeCentreFirst[] = "Centre the joystick to drive"; // C1
static_assert(sizeof(kHmiNoticeNotCalibrated) <= kErrorTextLen,
              "a Drive notice outgrows the width its slot is drawn for");
static_assert(sizeof(kHmiDriveNotGrantedFooter) <= kErrorFooterLen &&
                  sizeof(kHmiDriveStoppedFooter) <= kErrorFooterLen &&
                  sizeof(kHmiExitRefusedFooter) <= kErrorFooterLen,
              "refusal footer outgrows the banner it shares with MIB faults");

/* body / footer per link state short of connected; the first two per link (Network in
   Internet Settings: the W5500's Ethernet, or WiFi through the Tab5's ESP32-C6) */
inline constexpr char kHmiEthFailedText[] = "W5500 ETHERNET INIT FAILED AT BOOT";
inline constexpr char kHmiEthFailedFooter[] = "Power-cycle HMI to retry";
inline constexpr char kHmiLinkDownText[] = "NO ETHERNET LINK";
inline constexpr char kHmiLinkDownFooter[] = "Check cable/switch to MCB";
inline constexpr char kHmiWifiFailedText[] = "WIFI (ESP32-C6) INIT FAILED AT BOOT";
inline constexpr char kHmiWifiFailedFooter[] = "Power-cycle HMI to retry";
inline constexpr char kHmiWifiDownText[] = "NOT CONNECTED TO WIFI";
inline constexpr char kHmiWifiDownFooter[] = "Check the WiFi network";
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
