// Host test: main/estop_codec.hpp's decoder against espp/cdr's encoder, which is
// what the HMI sends with and what every espp node (the MIB) uses.
//
// From the repo root, after an idf.py build has fetched managed_components:
//
//   g++ -std=c++23 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
//     -Wall -Wextra -Werror -Imain
//     -Iexternal/rammp-rtps/components/rammp_rtps_messages/include
//     -Imanaged_components/espp__cdr/detail/cdr/include
//     -Imanaged_components/espp__reflect_cpp/detail/reflect-cpp/include
//     test/estop_codec_test.cpp -o /tmp/estop_codec_test && /tmp/estop_codec_test

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include <cdr/cdr.hpp>

#include "estop_codec.hpp"
#include "messages/estop_message.hpp"

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
  if (!ok) {
    std::printf("FAIL line %d: %s\n", line, what);
    failures++;
  }
}
#define CHECK(x) check((x), #x, __LINE__)

// The ROS EStop layout, without the appended fields: what a node that knows only
// the ROS message sends.
struct RosEStop {
  rammp::Header header;
  bool engaged;
  std::string source;
  std::string reason;
};

template <class T> std::vector<uint8_t> encode(const T &msg) {
  auto bytes = cdr::serialize<cdr::xcdr1>(msg);
  if (!bytes) {
    std::printf("encode failed\n");
    std::exit(2);
  }
  std::vector<uint8_t> out(bytes->size());
  for (size_t i = 0; i < out.size(); i++) {
    out[i] = static_cast<uint8_t>((*bytes)[i]);
  }
  return out;
}

bool decode(const std::vector<uint8_t> &bytes, estop_codec::Decoded &out) {
  return estop_codec::decode(bytes, "hmi", rammp::kEStopClearer, out);
}

rammp::EStop full(bool engaged, std::string source, std::string reason, uint32_t seq,
                  std::string ack_source, uint32_t ack_seq) {
  return rammp::EStop{.header = {.stamp = {1'760'000'000, 123}, .frame_id = ""},
                      .engaged = engaged,
                      .source = std::move(source),
                      .reason = std::move(reason),
                      .seq = seq,
                      .ack_source = std::move(ack_source),
                      .ack_seq = ack_seq};
}

void test_round_trip() {
  // String lengths 0..9 move every later field through each alignment.
  for (size_t src_len = 0; src_len < 10; src_len++) {
    for (size_t why_len = 0; why_len < 10; why_len++) {
      const std::string src(src_len, 's');
      const std::string why(why_len, 'r');
      estop_codec::Decoded d;
      CHECK(decode(encode(full(true, src, why, 0xDEADBEEF, "", 0)), d));
      CHECK(d.engaged && d.seq == 0xDEADBEEF && !d.ack_for_self && d.ack_seq == 0);
      CHECK(std::string(d.source) == src && std::string(d.reason) == why);
    }
  }
}

void test_clear() {
  estop_codec::Decoded d;
  CHECK(decode(encode(full(false, "mib", "operator clear", 7, "hmi", 42)), d));
  CHECK(!d.engaged && d.from_clearer && !d.from_self && d.ack_for_self);
  CHECK(d.seq == 7 && d.ack_seq == 42);

  // A clear for someone else is not ours.
  CHECK(decode(encode(full(false, "mib", "", 8, "other", 42)), d));
  CHECK(d.from_clearer && !d.ack_for_self);

  // A clear from anyone but the MIB is decoded, and flagged as not the clearer.
  CHECK(decode(encode(full(false, "rogue", "", 9, "hmi", 42)), d));
  CHECK(!d.from_clearer && d.ack_for_self);

  // Our own sample.
  CHECK(decode(encode(full(true, "hmi", "switch gpio2", 3, "", 0)), d));
  CHECK(d.from_self);
}

void test_ros_layout() {
  // The 4-field layout reads as seq 0 with no ack: a heartbeat or an engage, never a
  // clear. Lengths vary so the message ends with and without trailing padding.
  for (size_t why_len = 0; why_len < 8; why_len++) {
    const RosEStop ros{.header = {.stamp = {0, 0}, .frame_id = "base"},
                       .engaged = why_len % 2 == 0,
                       .source = "mib",
                       .reason = std::string(why_len, 'x')};
    auto bytes = encode(ros);
    estop_codec::Decoded d;
    CHECK(decode(bytes, d));
    CHECK(d.engaged == ros.engaged && d.seq == 0 && !d.ack_for_self && d.from_clearer);
    // And with up to 3 padding bytes after it, as an RTPS writer may add.
    for (int pad = 1; pad <= 3; pad++) {
      bytes.push_back(0);
      CHECK(decode(bytes, d));
      CHECK(d.seq == 0 && !d.ack_for_self);
    }
  }
}

void test_rejects() {
  auto good = encode(full(true, "mib", "x", 1, "", 0));
  estop_codec::Decoded d;

  auto be = good;
  be[1] = 0x00; // CDR big-endian
  CHECK(!decode(be, d));
  auto xcdr2 = good;
  xcdr2[1] = 0x07; // XCDR2 little-endian
  CHECK(!decode(xcdr2, d));
  CHECK(!decode({}, d));

  // Every truncation: refused, or (cut inside the appended fields) read as the
  // shorter layout -- never a clear, never out of bounds (ASan watches).
  auto clear = encode(full(false, "mib", "why", 5, "hmi", 4));
  for (size_t n = 0; n < clear.size(); n++) {
    const std::vector<uint8_t> cut(clear.begin(), clear.begin() + static_cast<long>(n));
    if (decode(cut, d)) {
      CHECK(!d.ack_for_self);
    }
  }

  // A string whose last byte is not the NUL.
  auto no_nul = encode(full(true, "mib", "abc", 1, "", 0));
  // Body offsets: sec 0, nanosec 4, frame_id length 8 and its NUL 12, engaged 13,
  // source length 16, then "mib\0" at 20..23. Overwrite that NUL (header 4 + 23).
  CHECK(no_nul[4 + 23] == 0);
  no_nul[4 + 23] = 'X';
  CHECK(!decode(no_nul, d));
}

void test_long_text_truncates() {
  estop_codec::Decoded d;
  const std::string long_reason(200, 'L');
  CHECK(decode(encode(full(true, "a-very-long-node-name", long_reason, 1, "", 0)), d));
  CHECK(std::string(d.source).size() == sizeof(d.source) - 1);
  CHECK(std::string(d.reason).size() == sizeof(d.reason) - 1);
  CHECK(!d.from_clearer);
}

void test_hmi_message_fits() {
  // What estop.cpp sends at its longest, into its 128-byte buffer.
  const auto msg = full(true, "hmi", "switch gpio47", 0xFFFFFFFF, "", 0);
  std::array<std::byte, 128> buf{};
  const auto size = cdr::serialize_into<cdr::xcdr1>(msg, std::span(buf));
  CHECK(size.has_value() && *size <= buf.size());
}

void test_fuzz() {
  std::mt19937 rng(1234);
  const auto seed = encode(full(false, "mib", "fuzz", 77, "hmi", 76));
  for (int i = 0; i < 200'000; i++) {
    std::vector<uint8_t> bytes = seed;
    const int flips = 1 + static_cast<int>(rng() % 4);
    for (int f = 0; f < flips; f++) {
      bytes[rng() % bytes.size()] = static_cast<uint8_t>(rng());
    }
    if (rng() % 4 == 0) {
      bytes.resize(rng() % (bytes.size() + 8));
    }
    estop_codec::Decoded d;
    (void)decode(bytes, d); // only that it never reads out of bounds
  }
}

} // namespace

int main() {
  test_round_trip();
  test_clear();
  test_ros_layout();
  test_rejects();
  test_long_text_truncates();
  test_hmi_message_fits();
  test_fuzz();
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
