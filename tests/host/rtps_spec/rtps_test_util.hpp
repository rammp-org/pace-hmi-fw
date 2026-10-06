#pragma once
// Shared helpers for the RTPS spec app (tests/host/rtps_spec): the wire codec exactly as the
// firmware calls it (espp's Publisher/Subscriber: cdr::serialize_into<xcdr1> and
// cdr::deserialize<T>, managed_components/espp__rtps/include/rtps_pubsub.hpp:88-102, 182),
// and field-by-field equality with floats compared bit for bit (NaN == NaN, -0 != +0).

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include <cdr/cdr.hpp>

#include "hmi_rtps_spec.hpp"
#include "messages/motor_message.hpp"

namespace rtps_test {

using Bytes = std::vector<uint8_t>;

/// The float with these IEEE-754 bits.
inline float F(uint32_t bits) { return std::bit_cast<float>(bits); }
inline uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }

/// What espp's Publisher puts on the wire: header + XCDR1, little-endian.
template <class T> Bytes encode(const T &value) {
  Bytes out(cdr::serialized_size<cdr::xcdr1>(value));
  const auto written =
      cdr::serialize_into<cdr::xcdr1>(value, std::as_writable_bytes(std::span(out)));
  if (!written) {
    return {};
  }
  out.resize(*written);
  return out;
}

/// What espp's Subscriber does with a received payload.
template <class T> std::expected<T, cdr::error> decode(const Bytes &data) {
  return cdr::deserialize<T>(std::as_bytes(std::span(data)));
}

inline bool same(float a, float b) { return bits(a) == bits(b); }

inline bool eq(const rammp::XYTwist &a, const rammp::XYTwist &b) {
  return same(a.x, b.x) && same(a.y, b.y) && same(a.twist, b.twist) && a.buttons == b.buttons;
}
inline bool eq(const rammp::DriveCommand &a, const rammp::DriveCommand &b) {
  return a.request == b.request && a.profile == b.profile;
}
inline bool eq(const rammp::SeatCommand &a, const rammp::SeatCommand &b) {
  return a.axis == b.axis && same(a.target, b.target);
}
inline bool eq(const MIB::seatState &a, const MIB::seatState &b) {
  return same(a.front_back_tilt, b.front_back_tilt) && same(a.lateral_tilt, b.lateral_tilt) &&
         same(a.elevation, b.elevation) && same(a.translation, b.translation);
}
inline bool eq(const MIB::MibStatus &a, const MIB::MibStatus &b) {
  return a.systemState == b.systemState && a.activeProfile == b.activeProfile &&
         eq(a.currentSeatState, b.currentSeatState) && a.error_message == b.error_message &&
         a.epoch_s == b.epoch_s && same(a.speed, b.speed) && a.utc_offset_min == b.utc_offset_min &&
         a.seq == b.seq && a.status_text == b.status_text && a.error_footer == b.error_footer;
}
inline bool eq(const rammp::Diagnostics &a, const rammp::Diagnostics &b) {
  if (a.seq != b.seq || a.items.size() != b.items.size()) {
    return false;
  }
  for (size_t i = 0; i < a.items.size(); ++i) {
    if (a.items[i].values != b.items[i].values) {
      return false;
    }
  }
  return true;
}
inline bool eq(const rammp::UInt32 &a, const rammp::UInt32 &b) { return a.data == b.data; }
inline bool eq(const rammp::SelfTestReport &a, const rammp::SelfTestReport &b) {
  return a.run_id == b.run_id && a.kind == b.kind && a.index == b.index && a.count == b.count &&
         a.result == b.result && a.value == b.value && a.lo == b.lo && a.hi == b.hi &&
         a.name == b.name && a.unit == b.unit && a.detail == b.detail;
}
inline bool eq(const rammp::MotorCommand &a, const rammp::MotorCommand &b) {
  return a.seq == b.seq && a.requested_state == b.requested_state && a.mode == b.mode &&
         a.clear_fault_req_id == b.clear_fault_req_id && same(a.position, b.position) &&
         same(a.velocity, b.velocity) && same(a.torque, b.torque) &&
         same(a.torque_limit, b.torque_limit) && same(a.vel_limit, b.vel_limit) &&
         same(a.accel_limit, b.accel_limit);
}
inline bool eq(const rammp::MotorState &a, const rammp::MotorState &b) {
  bool temps = true;
  for (size_t i = 0; i < a.temps.size(); ++i) {
    temps = temps && same(a.temps[i], b.temps[i]);
  }
  return temps && a.api_version == b.api_version && a.axis_id == b.axis_id && a.seq == b.seq &&
         a.last_cmd_seq == b.last_cmd_seq && a.state == b.state && a.mode == b.mode &&
         a.fault_code == b.fault_code && a.clear_fault_ack == b.clear_fault_ack &&
         a.vbus_measured == b.vbus_measured && a.flags == b.flags && a.cmd_age_ms == b.cmd_age_ms &&
         a.drv_status == b.drv_status && a.uptime_ms == b.uptime_ms &&
         a.fw_version == b.fw_version && same(a.position, b.position) &&
         same(a.velocity, b.velocity) && same(a.torque_est, b.torque_est) && same(a.iq, b.iq) &&
         same(a.id, b.id) && same(a.vbus, b.vbus) && same(a.ibus_est, b.ibus_est) &&
         same(a.torque_limit_eff, b.torque_limit_eff) && same(a.vel_limit_eff, b.vel_limit_eff) &&
         same(a.accel_limit_eff, b.accel_limit_eff);
}

/// True if a decoded text holds a byte the Python codec cannot carry unchanged: a NUL
/// (it cuts the string there) or a non-ASCII byte (it drops or replaces it).
inline bool odd_text(const std::string &s) {
  return std::any_of(s.begin(), s.end(), [](const char c) {
    return c == '\0' || static_cast<unsigned char>(c) >= 0x80U;
  });
}
template <class T> bool odd_text(const T &) { return false; }
inline bool odd_text(const MIB::MibStatus &m) {
  return odd_text(m.error_message) || odd_text(m.status_text) || odd_text(m.error_footer);
}
inline bool odd_text(const rammp::SelfTestReport &r) {
  return odd_text(r.name) || odd_text(r.unit) || odd_text(r.detail);
}

/// splitmix64: the seeded random source for hostile bytes (TS-DET-02).
inline uint64_t next_random(uint64_t &state) {
  state += 0x9E3779B97F4A7C15ULL;
  uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

} // namespace rtps_test
