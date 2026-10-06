// L1 characterisation of the RTPS messages on the wire (cases RTPS-030..RTPS-052): every
// message struct of the rammp-rtps headers and hmi_rtps_spec.hpp through espp/cdr exactly
// as the firmware's Publisher/Subscriber call it (rtps_test_util.hpp), round trips first,
// then hostile bytes (TS-UNIT-07): empty, truncated, lengths at / above / far above the
// buffer, out-of-range values, relabelled headers, one-byte mutations and seeded random
// bytes. Each must return - an error or a value - with no sanitizer report and no hang.
// The cases named "hazard" pin input that decodes *successfully* into a value nothing has
// checked: today any consumer that uses such a field unchecked is exposed (CS-SAF-03).

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

#include "rtps_test_util.hpp"
#include "test_case.hpp"

using namespace rtps_test;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kMax = std::numeric_limits<float>::max();
constexpr uint64_t kSeed = 20261006; // TS-DET-02: the same bytes on every run

std::vector<rammp::XYTwist> samples(const rammp::XYTwist *) {
  return {{0.0f, 0.0f, 0.0f, rammp::Buttons::NONE},
          {1.0f, -1.0f, 0.5f, rammp::Buttons::JOYSTICK},
          {kNaN, kInf, -0.0f, static_cast<rammp::Buttons>(0xFFFFFFFFU)},
          {-kMax, kMax, std::numeric_limits<float>::denorm_min(), rammp::Buttons::NONE}};
}
std::vector<rammp::DriveCommand> samples(const rammp::DriveCommand *) {
  return {{rammp::DriveRequest::DISABLE, MIB::DriveProfile::LOW},
          {rammp::DriveRequest::ENABLE, MIB::DriveProfile::HIGH},
          {static_cast<rammp::DriveRequest>(255), static_cast<MIB::DriveProfile>(255)}};
}
std::vector<rammp::SeatCommand> samples(const rammp::SeatCommand *) {
  return {{rammp::SeatAxis::FRONT_BACK_TILT, -45.0f},
          {rammp::SeatAxis::ELEVATION, 250.0f},
          {rammp::SeatAxis::TRANSLATION, 0.1f},
          {static_cast<rammp::SeatAxis>(200), kNaN},
          {rammp::SeatAxis::LATERAL_TILT, -kInf}};
}
std::vector<MIB::MibStatus> samples(const MIB::MibStatus *) {
  MIB::MibStatus full{MIB::MibSystemState::ERROR,
                      MIB::DriveProfile::HIGH,
                      {-12.5f, 7.5f, 125.0f, 25.0f},
                      "MOTOR FAULT",
                      INT64_MIN,
                      1.25f,
                      INT16_MIN,
                      255,
                      "FAULT",
                      "Power-cycle"};
  MIB::MibStatus odd{};
  odd.currentSeatState = {kNaN, kInf, 1e30f, -1e30f};
  odd.error_message = std::string("a\0b\xff", 4);
  odd.status_text = std::string(1000, 'S');
  odd.epoch_s = INT64_MAX;
  return {MIB::MibStatus{}, full, odd};
}
std::vector<rammp::Diagnostics> samples(const rammp::Diagnostics *) {
  return {{0, {}},
          {1, {{{215, 123, -45}}, {{216, 0, 900}}, {{0, -1, 1}}}},
          {255, {{{}}, {{INT32_MIN, INT32_MAX, 0, 5, 6}}}}};
}
std::vector<rammp::UInt32> samples(const rammp::UInt32 *) {
  return {{0}, {0xC0000005U}, {0xFFFFFFFFU}};
}
std::vector<rammp::SelfTestReport> samples(const rammp::SelfTestReport *) {
  return {{3, rammp::SelfTestKind::STARTED, 0, 12, rammp::SelfTestResult::PASS, 0, INT32_MIN,
           INT32_MAX, "", "", "v1.2.3"},
          {3, rammp::SelfTestKind::RESULT, 4, 12, rammp::SelfTestResult::FAIL, 51, 64, 9999,
           "mem.int_free", "KB", "below limit"}};
}
std::vector<rammp::MotorCommand> samples(const rammp::MotorCommand *) {
  return {{0, rammp::RequestedState::DISARMED, rammp::ControlMode::COAST, 0, 0, 0, 0, 0, 0, 0},
          {255, rammp::RequestedState::ARMED, rammp::ControlMode::VELOCITY, 7, 1.5f, -1.2f, 0.3f,
           20.0f, 10.0f, 0.0f},
          {1, static_cast<rammp::RequestedState>(9), static_cast<rammp::ControlMode>(9), 1, kNaN,
           kInf, -kInf, -0.0f, kMax, -kMax}};
}
std::vector<rammp::MotorState> samples(const rammp::MotorState *) {
  rammp::MotorState s{};
  s.api_version = RAMMP_MOTOR_API_VERSION;
  s.axis_id = rammp::AxisId::DRIVE_RIGHT;
  s.seq = 9;
  s.state = rammp::BoardState::FAULT;
  s.fault_code = rammp::FaultCode::SAMPLER;
  s.cmd_age_ms = 65535;
  s.drv_status = 0xBEEF;
  s.uptime_ms = 0xFFFFFFFFU;
  s.fw_version = 0x01020003U;
  s.position = -1e6f;
  s.vbus = 24.5f;
  s.temps = {25.0f, kNaN, -40.0f, 125.0f};
  return {rammp::MotorState{}, s};
}

template <class T> std::vector<T> samples_of() { return samples(static_cast<const T *>(nullptr)); }

using AllMessages = std::tuple<rammp::XYTwist, rammp::DriveCommand, rammp::SeatCommand,
                               MIB::MibStatus, rammp::Diagnostics, rammp::UInt32,
                               rammp::SelfTestReport, rammp::MotorCommand, rammp::MotorState>;

template <class Fn> void for_each_message(Fn &&fn) {
  std::apply([&fn](auto... tag) { (fn(tag), ...); }, AllMessages{});
}

template <class T> void check_round_trip(size_t expected_size) {
  for (const T &value : samples_of<T>()) {
    const Bytes wire = encode(value);
    TEST_ASSERT_TRUE(wire.size() >= 4);
    TEST_ASSERT_EQUAL_HEX8(0x00, wire[0]); // CDR_LE, the header espp writes
    TEST_ASSERT_EQUAL_HEX8(0x01, wire[1]);
    if (expected_size != 0) {
      TEST_ASSERT_EQUAL_size_t(expected_size, wire.size());
    }
    const auto back = decode<T>(wire);
    TEST_ASSERT_TRUE(back.has_value());
    TEST_ASSERT_TRUE(eq(value, *back));
  }
}

/// A valid encoding to corrupt: the richest sample.
template <class T> Bytes base_of() {
  const auto all = samples_of<T>();
  return encode(all[all.size() > 1 ? 1 : 0]);
}

/// Decodes and, when it worked, re-encodes: a successful decode must still be encodable.
template <class T> bool decodes(const Bytes &data) {
  const auto back = decode<T>(data);
  if (back) {
    TEST_ASSERT_TRUE(encode(*back).size() >= 4);
  }
  return back.has_value();
}

Bytes with_header(uint8_t b0, uint8_t b1, const Bytes &wire) {
  Bytes out = wire;
  out[0] = b0;
  out[1] = b1;
  return out;
}

void put_u32(Bytes &data, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) {
    data[at + i] = static_cast<uint8_t>(value >> (8U * i));
  }
}

} // namespace

TEST_CASE("RTPS-030 XYTwist round-trips through espp/cdr in 20 bytes", "[rtps_cdr][safety]") {
  check_round_trip<rammp::XYTwist>(20);
}

TEST_CASE("RTPS-031 DriveCommand round-trips through espp/cdr in 6 bytes", "[rtps_cdr][safety]") {
  check_round_trip<rammp::DriveCommand>(6);
}

TEST_CASE("RTPS-032 SeatCommand round-trips through espp/cdr in 12 bytes", "[rtps_cdr][safety]") {
  check_round_trip<rammp::SeatCommand>(12);
}

TEST_CASE("RTPS-033 MibStatus round-trips through espp/cdr, texts of any length included",
          "[rtps_cdr][safety]") {
  check_round_trip<MIB::MibStatus>(0);
  TEST_ASSERT_EQUAL_size_t(73, encode(MIB::MibStatus{}).size());
}

TEST_CASE("RTPS-034 Diagnostics round-trips through espp/cdr, ragged items included",
          "[rtps_cdr]") {
  check_round_trip<rammp::Diagnostics>(0);
  TEST_ASSERT_EQUAL_size_t(12, encode(rammp::Diagnostics{}).size());
}

TEST_CASE("RTPS-035 UInt32 round-trips through espp/cdr in 8 bytes", "[rtps_cdr]") {
  check_round_trip<rammp::UInt32>(8);
}

TEST_CASE("RTPS-036 SelfTestReport round-trips through espp/cdr", "[rtps_cdr]") {
  check_round_trip<rammp::SelfTestReport>(0);
}

TEST_CASE("RTPS-037 MotorCommand round-trips through espp/cdr in 4 + 28 bytes",
          "[rtps_cdr][safety]") {
  check_round_trip<rammp::MotorCommand>(4 + 28); // motor_message.hpp: "28 bytes on the wire"
}

TEST_CASE("RTPS-038 MotorState round-trips through espp/cdr in 4 + 80 bytes",
          "[rtps_cdr][safety]") {
  check_round_trip<rammp::MotorState>(4 + 80); // motor_message.hpp: "80 bytes on the wire"
}

TEST_CASE("RTPS-039 the wire layout is XCDR1 little-endian, fields in declaration order",
          "[rtps_cdr][safety]") {
  const Bytes seat = encode(rammp::SeatCommand{rammp::SeatAxis::ELEVATION, 12.5f});
  const Bytes expected_seat{0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41};
  TEST_ASSERT_TRUE(seat == expected_seat);
  const Bytes drive =
      encode(rammp::DriveCommand{rammp::DriveRequest::ENABLE, MIB::DriveProfile::HIGH});
  const Bytes expected_drive{0x00, 0x01, 0x00, 0x00, 0x01, 0x02};
  TEST_ASSERT_TRUE(drive == expected_drive);
  // MibStatus: epoch_s (int64) is aligned to 8 from the first payload byte
  MIB::MibStatus status{};
  status.error_message = "";
  status.epoch_s = 0x0102030405060708;
  const Bytes s = encode(status);
  // 2 enums + 2 pad + 16 seat + 4 len + 1 NUL = 25, pad to 32, + 4 header = 36
  TEST_ASSERT_EQUAL_HEX8(0x08, s[36]);
  TEST_ASSERT_EQUAL_HEX8(0x01, s[43]);
}

TEST_CASE("RTPS-040 empty input and short headers are rejected for every message",
          "[rtps_cdr][hostile]") {
  for_each_message([](auto tag) {
    using T = decltype(tag);
    for (size_t n = 0; n < 4; ++n) {
      const Bytes data(n, 0x00);
      const auto r = decode<T>(data);
      TEST_ASSERT_FALSE(r.has_value());
      TEST_ASSERT_TRUE(r.error().code == cdr::errc::bad_encapsulation);
    }
    const Bytes header_only{0x00, 0x01, 0x00, 0x00};
    // a header alone is a valid encoding only of a message with nothing in it: none here
    TEST_ASSERT_FALSE(decodes<T>(header_only));
  });
}

TEST_CASE("RTPS-041 every truncation of every valid encoding is rejected as out of bounds",
          "[rtps_cdr][hostile]") {
  for_each_message([](auto tag) {
    using T = decltype(tag);
    for (const T &value : samples_of<T>()) {
      const Bytes wire = encode(value);
      for (size_t n = 4; n < wire.size(); ++n) {
        const Bytes cut(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(n));
        const auto r = decode<T>(cut);
        TEST_ASSERT_FALSE(r.has_value());
        TEST_ASSERT_TRUE(r.error().code == cdr::errc::out_of_bounds);
      }
    }
  });
}

TEST_CASE("RTPS-042 string lengths at, above and far above the buffer: only what fits decodes",
          "[rtps_cdr][hostile]") {
  MIB::MibStatus status{};
  status.error_message = "abc";
  const Bytes wire = encode(status);
  constexpr size_t kLenAt = 4 + 20;               // error_message's length field
  const size_t fits = wire.size() - (kLenAt + 4); // every byte after the length field
  struct Case {
    uint32_t len;
    bool decodes;
    cdr::errc code;
  };
  const std::array<Case, 6> cases{{
      {4, true, cdr::errc::ok}, // as encoded
      // at: the text takes every byte left (the buffer ends on a NUL); the next field runs out
      {static_cast<uint32_t>(fits), false, cdr::errc::out_of_bounds},
      {static_cast<uint32_t>(fits + 1), false, cdr::errc::out_of_bounds}, // just above
      {0x7FFFFFFFU, false, cdr::errc::out_of_bounds},                     // far above
      {0xFFFFFFFFU, false, cdr::errc::out_of_bounds},
      {3, false, cdr::errc::invalid_string}, // cuts before the NUL
  }};
  for (const Case &c : cases) {
    Bytes data = wire;
    put_u32(data, kLenAt, c.len);
    const auto r = decode<MIB::MibStatus>(data);
    TEST_ASSERT_EQUAL(c.decodes, r.has_value());
    if (!c.decodes) {
      TEST_ASSERT_TRUE(r.error().code == c.code);
    }
  }
}

TEST_CASE("RTPS-043 hazard: a zero string length is accepted and shifts every later field",
          "[rtps_cdr][hostile][hazard]") {
  MIB::MibStatus status{};
  status.error_message = "abcdefg"; // 8 bytes with the NUL: the following int64 stays aligned
  status.epoch_s = 1;
  Bytes data = encode(status);
  put_u32(data, 4 + 20, 0); // "tolerate 0 from lax peers" (codec.hpp read_string)
  const auto r = decode<MIB::MibStatus>(data);
  // today: decodes; epoch_s is read from the text bytes "abcdefg\0", the rest slides along
  TEST_ASSERT_TRUE(r.has_value());
  TEST_ASSERT_EQUAL_STRING("", r->error_message.c_str());
  TEST_ASSERT_TRUE(r->epoch_s == 0x0067666564636261);
}

TEST_CASE("RTPS-044 sequence counts above the buffer are rejected before any allocation",
          "[rtps_cdr][hostile]") {
  rammp::Diagnostics diag{7, {{{1, 2, 3}}}};
  const Bytes wire = encode(diag);
  constexpr size_t kCountAt = 4 + 4; // items count, after seq and 3 pad bytes
  for (const uint32_t count : {0xFFFFFFFFU, 0x7FFFFFFFU, 0x00010000U}) {
    Bytes data = wire;
    put_u32(data, kCountAt, count);
    const auto r = decode<rammp::Diagnostics>(data);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error().code == cdr::errc::out_of_bounds);
    TEST_ASSERT_EQUAL_size_t(8, r.error().offset); // right after the count: nothing allocated
  }
  // the inner values count, far above
  Bytes inner = wire;
  put_u32(inner, kCountAt + 4, 0xFFFFFFFFU);
  TEST_ASSERT_FALSE(decodes<rammp::Diagnostics>(inner));
}

TEST_CASE("RTPS-045 hazard: an item count up to the bytes left passes the guard, then fails",
          "[rtps_cdr][hostile][hazard]") {
  // espp/cdr bounds a count by the bytes left (>= 1 byte per element) and resizes before
  // reading: a DiagItem is sizeof(std::vector) bytes in RAM per wire byte, so a 64 KB
  // payload can make the decoder allocate count * sizeof(DiagItem) before it fails.
  Bytes data{0x00, 0x01, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00};
  const size_t padding = 4096;
  data.resize(8 + 4 + padding, 0xFF);
  put_u32(data, 8, static_cast<uint32_t>(padding)); // = the bytes left after the count
  const auto r = decode<rammp::Diagnostics>(data);
  TEST_ASSERT_FALSE(r.has_value());
  TEST_ASSERT_TRUE(r.error().code == cdr::errc::out_of_bounds);
  TEST_ASSERT_TRUE(r.error().offset > 8); // past the count: the guard let it through
  std::printf("RTPS-045 count %zu passed the guard; sizeof(DiagItem) here %zu (12 on a 32-bit "
              "target): up to %zu bytes allocated before the error\n",
              padding, sizeof(rammp::DiagItem), padding * sizeof(rammp::DiagItem));
}

TEST_CASE("RTPS-046 hazard: enum values outside every declared value decode successfully",
          "[rtps_cdr][hostile][hazard][safety]") {
  // today: espp/cdr casts the wire integer to the enum; no message validates its enums
  const auto seat = decode<rammp::SeatCommand>(
      encode(rammp::SeatCommand{static_cast<rammp::SeatAxis>(200), 1.0f}));
  TEST_ASSERT_TRUE(seat.has_value());
  TEST_ASSERT_EQUAL_UINT8(200, static_cast<uint8_t>(seat->axis));
  Bytes drive = encode(rammp::DriveCommand{});
  drive[4] = 5; // DriveRequest: only 0 and 1 exist
  drive[5] = 9; // DriveProfile: only 0..2 exist
  const auto d = decode<rammp::DriveCommand>(drive);
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_EQUAL_UINT8(5, static_cast<uint8_t>(d->request));
  TEST_ASSERT_EQUAL_UINT8(9, static_cast<uint8_t>(d->profile));
  Bytes status = encode(MIB::MibStatus{});
  status[4] = 77;  // systemState
  status[5] = 255; // activeProfile
  const auto s = decode<MIB::MibStatus>(status);
  TEST_ASSERT_TRUE(s.has_value());
  TEST_ASSERT_EQUAL_STRING("?", rammp::to_string(s->systemState));
  TEST_ASSERT_EQUAL_STRING("?", rammp::to_string(s->activeProfile));
  Bytes twist = encode(rammp::XYTwist{});
  put_u32(twist, 4 + 12, 0xFFFFFFFFU); // buttons: only bit 0 is defined
  TEST_ASSERT_TRUE(decodes<rammp::XYTwist>(twist));
  Bytes motor = encode(rammp::MotorCommand{});
  motor[5] = 9; // RequestedState: only 0..3 exist
  motor[6] = 9; // ControlMode: only 0..4 exist
  TEST_ASSERT_TRUE(decodes<rammp::MotorCommand>(motor));
}

TEST_CASE("RTPS-047 hazard: NaN, infinite and huge seat values from the MIB decode successfully",
          "[rtps_cdr][hostile][hazard][safety]") {
  MIB::MibStatus status{};
  status.currentSeatState = {kNaN, kInf, -kInf, 1e30f};
  status.speed = kNaN;
  const auto s = decode<MIB::MibStatus>(encode(status));
  TEST_ASSERT_TRUE(s.has_value());
  // today: these reach seat_raw, which is undefined for all four (RTPS-009..011, plan H8)
  TEST_ASSERT_TRUE(
      std::isnan(rammp::seat_field(s->currentSeatState, rammp::SeatAxis::FRONT_BACK_TILT)));
  TEST_ASSERT_TRUE(
      std::isinf(rammp::seat_field(s->currentSeatState, rammp::SeatAxis::LATERAL_TILT)));
  TEST_ASSERT_EQUAL_FLOAT(1e30f,
                          rammp::seat_field(s->currentSeatState, rammp::SeatAxis::TRANSLATION));
  TEST_ASSERT_TRUE(std::isnan(s->speed));
  const auto x = decode<rammp::XYTwist>(encode(rammp::XYTwist{kNaN, 1e30f, -kInf, {}}));
  TEST_ASSERT_TRUE(x.has_value()); // outside -1..+1 and not finite: accepted
}

TEST_CASE("RTPS-048 hazard: texts far above the display limits, NUL and non-ASCII decode whole",
          "[rtps_cdr][hostile][hazard]") {
  MIB::MibStatus status{};
  status.error_message = std::string(10000, 'E');
  status.status_text = std::string("ID\0LE", 5);
  status.error_footer = "\xff\xfe";
  const auto s = decode<MIB::MibStatus>(encode(status));
  TEST_ASSERT_TRUE(s.has_value());
  // today: no bound from the codec; the HMI shows kErrorTextLen - 1 = 63 chars at most
  TEST_ASSERT_EQUAL_size_t(10000, s->error_message.size());
  TEST_ASSERT_TRUE(s->error_message.size() > rammp::kErrorTextLen);
  TEST_ASSERT_EQUAL_size_t(5, s->status_text.size());
  TEST_ASSERT_EQUAL_size_t(2, s->error_footer.size());
}

TEST_CASE("RTPS-049 headers: trailing bytes and option bits are ignored, PL_CDR is rejected",
          "[rtps_cdr][hostile]") {
  for_each_message([](auto tag) {
    using T = decltype(tag);
    const Bytes wire = base_of<T>();
    Bytes trailing = wire;
    trailing.insert(trailing.end(), 64, 0xAA);
    const auto t = decode<T>(trailing);
    TEST_ASSERT_TRUE(t.has_value()); // today: junk after a message is ignored
    TEST_ASSERT_TRUE(eq(*t, *decode<T>(wire)));
    Bytes options = wire;
    options[2] = 0xFF;
    options[3] = 0xFF;
    TEST_ASSERT_TRUE(decodes<T>(options));
    for (const uint8_t id :
         std::array<uint8_t, 8>{0x02, 0x03, 0x0a, 0x0b, 0x04, 0x05, 0x0c, 0xff}) {
      TEST_ASSERT_FALSE(decodes<T>(with_header(0x00, id, wire)));
    }
    TEST_ASSERT_FALSE(decodes<T>(with_header(0x01, 0x01, wire)));
  });
}

TEST_CASE("RTPS-050 hazard: little-endian bytes relabelled big-endian decode into swapped values",
          "[rtps_cdr][hostile][hazard]") {
  const Bytes wire = encode(rammp::SeatCommand{rammp::SeatAxis::ELEVATION, 12.5f});
  const auto r = decode<rammp::SeatCommand>(with_header(0x00, 0x00, wire));
  TEST_ASSERT_TRUE(r.has_value());
  TEST_ASSERT_EQUAL_HEX32(0x00004841U, bits(r->target)); // 12.5f byte-swapped: a denormal
  // a big-endian peer is legitimate: its own encoding decodes to the right values
  const auto be = cdr::serialize<cdr::xcdr1>(rammp::SeatCommand{rammp::SeatAxis::ELEVATION, 12.5f},
                                             std::endian::big);
  TEST_ASSERT_TRUE(be.has_value());
  const auto good = cdr::deserialize<rammp::SeatCommand>(std::span(*be));
  TEST_ASSERT_TRUE(good.has_value());
  TEST_ASSERT_EQUAL_FLOAT(12.5f, good->target);
}

TEST_CASE("RTPS-051 XCDR1 bytes relabelled XCDR2: what each message does today",
          "[rtps_cdr][hostile]") {
  size_t accepted = 0;
  size_t total = 0;
  for_each_message([&](auto tag) {
    using T = decltype(tag);
    for (const uint8_t id : std::array<uint8_t, 2>{0x07, 0x09}) { // CDR2_LE, D_CDR2_LE
      ++total;
      accepted += decodes<T>(with_header(0x00, id, base_of<T>())) ? 1U : 0U;
    }
  });
  std::printf("RTPS-051 XCDR2 relabel: %zu of %zu decoded\n", accepted, total);
  TEST_ASSERT_EQUAL_size_t(18, total);
  TEST_ASSERT_EQUAL_size_t(8, accepted); // today: decoded with XCDR2's rules, into what fits
}

TEST_CASE("RTPS-052 one byte forced to 0x00, 0x80 or 0xFF anywhere, and seeded random bytes, "
          "all return",
          "[rtps_cdr][hostile]") {
  uint64_t state = kSeed;
  size_t runs = 0;
  size_t accepted = 0;
  for_each_message([&](auto tag) {
    using T = decltype(tag);
    const Bytes wire = base_of<T>();
    for (size_t i = 4; i < wire.size(); ++i) {
      for (const uint8_t b : std::array<uint8_t, 3>{0x00, 0x80, 0xFF}) {
        Bytes data = wire;
        data[i] = b;
        ++runs;
        accepted += decodes<T>(data) ? 1U : 0U;
      }
    }
    for (int i = 0; i < 2000; ++i) {
      const auto n = static_cast<size_t>(next_random(state) % (2 * wire.size() + 16));
      Bytes data(n);
      for (uint8_t &b : data) {
        b = static_cast<uint8_t>(next_random(state));
      }
      if (n >= 4 && (i % 2) == 0) { // half keep a valid header, so the body parser is reached
        data[0] = 0x00;
        data[1] = 0x01;
      }
      ++runs;
      accepted += decodes<T>(data) ? 1U : 0U;
    }
  });
  std::printf("RTPS-052 %zu hostile inputs, %zu decoded into a value\n", runs, accepted);
  TEST_ASSERT_EQUAL_size_t(19026, runs);
  TEST_ASSERT_EQUAL_size_t(4927, accepted); // today's count for kSeed: none checked further
}
