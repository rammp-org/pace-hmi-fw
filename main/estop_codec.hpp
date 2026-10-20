#pragma once

/**
 * @file estop_codec.hpp
 * @brief Decodes a received rammp::EStop without building a std::string, so the
 *        receive path never allocates. Header-only and free of ESP-IDF, so the host
 *        test (test/estop_codec_test.cpp) checks it against espp/cdr's encoder.
 *
 * Accepts XCDR1 little-endian (encapsulation 0x00 0x01), which every RAMMP node
 * sends, and the 4-field ROS layout as well as the full one: the appended fields
 * read as pub_seq 0, no clear_target, so such a message is a heartbeat or an engage,
 * never a clear attempt. Anything else is refused.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "messages/estop_message.hpp"

namespace estop_codec {

/// Copies `in` into `out`, truncated and always NUL-terminated.
template <std::size_t N> constexpr void copy_text(char (&out)[N], std::string_view in) {
  const std::size_t n = in.size() < N - 1 ? in.size() : N - 1;
  for (std::size_t i = 0; i < n; i++) {
    out[i] = in[i];
  }
  out[n] = '\0';
}

/// What the HMI needs from one EStop: plain values, so it fits a queue.
struct Decoded {
  uint32_t pub_seq = 0;
  uint32_t clear_pub_seq = 0;
  bool engaged = false;        ///< state is anything but NOT_ENGAGED
  bool from_self = false;      ///< source is this HMI: our own sample, ignored
  bool from_clearer = false;   ///< source is rammp::kEStopClearer
  bool clear_for_self = false; ///< clear_target is this HMI: a clear attempt for us
  char source[16] = {};        ///< truncated, for the banner and the log
  char reason[48] = {};
};

/// XCDR1 little-endian reader over the body (after the 4-byte encapsulation header,
/// which is the alignment origin). Every read is bounds-checked.
class Reader {
public:
  explicit constexpr Reader(std::span<const uint8_t> body)
      : body_(body) {}

  /// Whether `size` more bytes, aligned to `size`, are left: an appended field that
  /// is absent (a shorter layout, or only the trailing padding) reads as "no more".
  constexpr bool has(std::size_t size) const { return aligned(size) + size <= body_.size(); }

  constexpr bool u8(uint8_t &out) {
    if (pos_ + 1 > body_.size()) {
      return false;
    }
    out = body_[pos_++];
    return true;
  }

  constexpr bool u32(uint32_t &out) {
    const std::size_t at = aligned(4);
    if (at + 4 > body_.size()) {
      return false;
    }
    out = static_cast<uint32_t>(body_[at]) | (static_cast<uint32_t>(body_[at + 1]) << 8) |
          (static_cast<uint32_t>(body_[at + 2]) << 16) |
          (static_cast<uint32_t>(body_[at + 3]) << 24);
    pos_ = at + 4;
    return true;
  }

  /// A CDR string: length including the NUL, the bytes, the NUL. A length of 0
  /// (some writers send it for "") reads as empty. Not constexpr: it views bytes as chars.
  bool string(std::string_view &out) {
    uint32_t len = 0;
    if (!u32(len)) {
      return false;
    }
    if (len == 0) {
      out = {};
      return true;
    }
    if (len > body_.size() - pos_ || body_[pos_ + len - 1] != 0) {
      return false;
    }
    out = std::string_view(reinterpret_cast<const char *>(body_.data() + pos_), len - 1);
    pos_ += len;
    return true;
  }

private:
  constexpr std::size_t aligned(std::size_t size) const { return (pos_ + size - 1) / size * size; }

  std::span<const uint8_t> body_;
  std::size_t pos_ = 0;
};

/// Decodes `payload` (encapsulation header + body). False = not an EStop this
/// firmware can read; `out` is then unspecified.
inline bool decode(std::span<const uint8_t> payload, std::string_view self,
                   std::string_view clearer, Decoded &out) {
  out = Decoded{};
  if (payload.size() < 4 || payload[0] != 0x00 || payload[1] != 0x01) {
    return false; // not XCDR1 little-endian
  }
  Reader r(payload.subspan(4));
  uint32_t sec = 0;
  uint32_t nanosec = 0;
  std::string_view frame_id;
  uint8_t state = 0; // rammp::EStopState
  std::string_view source;
  std::string_view reason;
  // header.stamp and header.frame_id are read past: the stamp is for the log only.
  if (!r.u32(sec) || !r.u32(nanosec) || !r.string(frame_id) || !r.u8(state) || !r.string(source) ||
      !r.string(reason)) {
    return false;
  }
  // Anything but NOT_ENGAGED (0) engages: the fail-safe reading of an unknown value.
  out.engaged = state != static_cast<uint8_t>(rammp::EStopState::NOT_ENGAGED);
  out.from_self = source == self;
  out.from_clearer = source == clearer;
  copy_text(out.source, source);
  copy_text(out.reason, reason);
  // The appended fields. Absent = the 4-field ROS layout: pub_seq 0, no clear_target.
  if (!r.has(4)) {
    return true;
  }
  if (!r.u32(out.pub_seq)) {
    return false;
  }
  if (!r.has(4)) {
    return true;
  }
  std::string_view clear_target;
  if (!r.string(clear_target) || !r.u32(out.clear_pub_seq)) {
    return false;
  }
  out.clear_for_self = !clear_target.empty() && clear_target == self;
  return true;
}

} // namespace estop_codec
