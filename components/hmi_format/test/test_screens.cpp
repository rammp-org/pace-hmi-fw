// L1 golden tests for the About, Firmware update and Internet screens' text helpers
// (components/hmi_format, REQ-FMT-1x in its README). FMT-101..110 walk a frozen golden table
// (goldens_screens.hpp, recorded from the pre-move code) through the code under test
// (subject_screens.hpp); FMT-111..116 compare the code under test with the pre-move code itself
// over wide sweeps. One behaviour per case.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "goldens_screens.hpp"
#include "legacy_screens.hpp"
#include "subject_screens.hpp"
#include "test_case.hpp"

namespace {

// Says which golden row failed, so a failure points at its input.
struct Input {
  std::array<char, 96> text{};
  Input(const char *what, unsigned long long a, long long b) {
    std::snprintf(text.data(), text.size(), "%s(%llu, %lld)", what, a, b);
  }
  [[nodiscard]] const char *c_str() const { return text.data(); }
};

} // namespace

TEST_CASE("FMT-101 a version names a release tag exactly as the golden table says",
          "[hmi_format][about]") {
  for (const auto &g : fmt_test::TAG_GOLDEN) {
    TEST_ASSERT_EQUAL_MESSAGE(g.names_a_tag, sut::names_a_tag(g.version), g.version);
  }
}

TEST_CASE("FMT-102 a SHA-256 line reads four groups of eight as the golden table says",
          "[hmi_format][about]") {
  for (const auto &g : fmt_test::SHA_GOLDEN) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, sut::sha_line(g.hex, g.from).c_str(), g.hex);
  }
}

TEST_CASE("FMT-103 a MAC reads six upper-case hex pairs joined by colons", "[hmi_format][about]") {
  for (const auto &g : fmt_test::MAC_GOLDEN) {
    TEST_ASSERT_EQUAL_STRING(g.text, sut::mac_text(g.mac).c_str());
  }
}

TEST_CASE("FMT-104 an ISO date reads day, month name, year, or itself when it does not parse",
          "[hmi_format][update]") {
  for (const auto &g : fmt_test::DAY_GOLDEN) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, sut::day(g.iso).c_str(), g.iso);
  }
}

TEST_CASE("FMT-105 a byte count reads as megabytes to one decimal, ties to even",
          "[hmi_format][update]") {
  for (const auto &g : fmt_test::MEGABYTES_GOLDEN) {
    const Input in("megabytes", g.value, 0);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, sut::megabytes(g.value).c_str(), in.c_str());
  }
}

TEST_CASE("FMT-106 the release list status reads the count as the golden table says",
          "[hmi_format][update]") {
  for (const auto &g : fmt_test::COUNT_GOLDEN) {
    const Input in("release_count_text", g.value, 0);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, sut::release_count_text(g.value).c_str(), in.c_str());
  }
}

TEST_CASE("FMT-107 the install percentage is done * 100 / total, 0 while total is 0",
          "[hmi_format][update]") {
  for (const auto &g : fmt_test::PCT_GOLDEN) {
    const Input in("progress_pct", g.done, static_cast<long long>(g.total));
    TEST_ASSERT_EQUAL_INT32_MESSAGE(g.pct, sut::progress_pct(g.done, g.total), in.c_str());
  }
}

TEST_CASE("FMT-108 the install progress reads done of total and the percentage",
          "[hmi_format][update]") {
  for (const auto &g : fmt_test::PROGRESS_GOLDEN) {
    const Input in("progress_text", g.done, g.pct);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, sut::progress_text(g.done, g.total, g.pct).c_str(),
                                     in.c_str());
  }
}

TEST_CASE("FMT-109 the WiFi signal reads N dBm, or -- without an RSSI", "[hmi_format][net]") {
  for (const auto &g : fmt_test::SIGNAL_GOLDEN) {
    const std::optional<int> rssi =
        g.has_rssi ? std::optional<int>(g.rssi_dbm) : std::optional<int>();
    const Input in("signal_text", g.has_rssi ? 1U : 0U, g.rssi_dbm);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g.text, sut::signal_text(rssi).c_str(), in.c_str());
  }
}

TEST_CASE("FMT-110 a network row reads its signal, with open in front when it has no password",
          "[hmi_format][net]") {
  for (const auto &g : fmt_test::ROW_SIGNAL_GOLDEN) {
    const Input in("net_row_signal_text", g.secured ? 1U : 0U, g.rssi_dbm);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        g.text, sut::net_row_signal_text(g.secured, g.rssi_dbm).c_str(), in.c_str());
  }
}

// ---------------------------------------------------------------------------------------------
// FMT-111..116: the component against the pre-move code itself (legacy_screens.cpp), byte for
// byte, over wide sweeps. Inputs come from a fixed-seed generator, so every run tries the same.

namespace {

// xorshift64*: the same sequence on every run and every host.
class Inputs {
public:
  explicit Inputs(uint64_t seed)
      : state_(seed) {}
  uint64_t next() {
    state_ ^= state_ >> 12U;
    state_ ^= state_ << 25U;
    state_ ^= state_ >> 27U;
    return state_ * 0x2545F4914F6CDD1DULL;
  }
  std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
  // A string of up to max_len characters drawn from `alphabet`.
  std::string text(std::string_view alphabet, std::size_t max_len) {
    std::string s(below(max_len + 1), ' ');
    std::generate(s.begin(), s.end(), [&] { return alphabet[below(alphabet.size())]; });
    return s;
  }

private:
  uint64_t state_;
};

// The longest run of digits in s: day() is compared only where every number fits an int.
std::size_t longest_digit_run(const std::string &s) {
  std::size_t longest = 0;
  std::size_t run = 0;
  for (const char c : s) {
    run = (c >= '0' && c <= '9') ? run + 1 : 0;
    longest = run > longest ? run : longest;
  }
  return longest;
}

constexpr std::size_t MAX_SAFE_DIGITS = 9; // 999999999 fits an int32

} // namespace

TEST_CASE("FMT-111 a byte count reads the same megabytes as before the move over a wide sweep",
          "[hmi_format][update]") {
  for (std::size_t b = 0; b <= 300000; ++b) { // every count up to 0.3 MB: the ties included
    TEST_ASSERT_EQUAL_STRING(legacy::megabytes(b).c_str(), sut::megabytes(b).c_str());
  }
  for (std::size_t b = 50000; b <= 200000000; b += 100000) { // every .x5 point to 200 MB, +-1
    for (std::size_t near : {b - 1, b, b + 1}) {
      TEST_ASSERT_EQUAL_STRING(legacy::megabytes(near).c_str(), sut::megabytes(near).c_str());
    }
  }
  Inputs in(111);
  for (int i = 0; i < 100000; ++i) {
    const std::size_t b = static_cast<std::size_t>(in.next() >> in.below(64));
    TEST_ASSERT_EQUAL_STRING(legacy::megabytes(b).c_str(), sut::megabytes(b).c_str());
    const std::size_t t = static_cast<std::size_t>(in.next() >> in.below(64));
    const auto pct = static_cast<int32_t>(in.next());
    TEST_ASSERT_EQUAL_STRING(legacy::progress_text(b, t, pct).c_str(),
                             sut::progress_text(b, t, pct).c_str());
    TEST_ASSERT_EQUAL_INT32(legacy::progress_pct(b, t), sut::progress_pct(b, t));
  }
}

TEST_CASE("FMT-112 a date string reads the same as before the move for every input tried",
          "[hmi_format][update]") {
  for (int y : {-1, 0, 1, 2026, 99999}) {
    for (int m = -1; m <= 14; ++m) {
      for (int d = -1; d <= 40; ++d) {
        std::array<char, 48> iso{};
        std::snprintf(iso.data(), iso.size(), "%d-%02d-%02d", y, m, d);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(legacy::day(iso.data()).c_str(),
                                         sut::day(iso.data()).c_str(), iso.data());
      }
    }
  }
  Inputs in(112);
  int compared = 0;
  while (compared < 200000) {
    const std::string iso = in.text("0123456789--+ \t\nTx:Z", 16);
    if (longest_digit_run(iso) > MAX_SAFE_DIGITS) {
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(legacy::day(iso).c_str(), sut::day(iso).c_str(), iso.c_str());
    ++compared;
  }
  // A '\0' inside ends the date, as it ended the C string sscanf read.
  const std::string with_nul("2026-06-15\0junk", 15);
  TEST_ASSERT_EQUAL_STRING(legacy::day(with_nul).c_str(), sut::day(with_nul).c_str());
  const std::string nul_early("2026-\0"
                              "06-15",
                              11);
  TEST_ASSERT_EQUAL_STRING(legacy::day(nul_early).c_str(), sut::day(nul_early).c_str());
}

TEST_CASE("FMT-113 names_a_tag agrees with the pre-move code for every version tried",
          "[hmi_format][about]") {
  constexpr std::string_view alphabet = "v-dirtyg0a9fX.";
  Inputs in(113);
  for (int i = 0; i < 200000; ++i) {
    const std::string version = in.text(alphabet, 14);
    TEST_ASSERT_EQUAL_MESSAGE(legacy::names_a_tag(version), sut::names_a_tag(version.c_str()),
                              version.c_str());
  }
}

TEST_CASE("FMT-114 a SHA-256 line reads as before the move for every digest length it allows",
          "[hmi_format][about]") {
  Inputs in(114);
  for (std::size_t size = 24; size <= 80; ++size) {
    for (int trial = 0; trial < 50; ++trial) {
      std::string hex = in.text("0123456789abcdef", size);
      hex.resize(size, '0');
      for (std::size_t from : {std::size_t{0}, std::size_t{32}}) {
        if (from + 24 > hex.size()) {
          continue; // outside the precondition: the pre-move code threw
        }
        TEST_ASSERT_EQUAL_STRING_MESSAGE(legacy::sha_line(hex, from).c_str(),
                                         sut::sha_line(hex, from).c_str(), hex.c_str());
      }
    }
  }
}

TEST_CASE("FMT-115 the MAC, count and signal texts read as before the move over wide sweeps",
          "[hmi_format][about][update][net]") {
  for (unsigned v = 0; v <= 0xFF; ++v) {
    for (std::size_t at = 0; at < 6; ++at) {
      std::array<uint8_t, 6> mac{0x30, 0xED, 0xA0, 0x1B, 0x2C, 0x3D};
      mac.at(at) = static_cast<uint8_t>(v);
      TEST_ASSERT_EQUAL_STRING(legacy::mac_text(mac).c_str(), sut::mac_text(mac).c_str());
    }
  }
  for (int rssi = -300; rssi <= 300; ++rssi) {
    TEST_ASSERT_EQUAL_STRING(legacy::signal_text(rssi).c_str(), sut::signal_text(rssi).c_str());
    for (bool secured : {false, true}) {
      TEST_ASSERT_EQUAL_STRING(legacy::net_row_signal_text(secured, rssi).c_str(),
                               sut::net_row_signal_text(secured, rssi).c_str());
    }
  }
  TEST_ASSERT_EQUAL_STRING(legacy::signal_text(std::nullopt).c_str(),
                           sut::signal_text(std::nullopt).c_str());
  Inputs in(115);
  for (int i = 0; i < 100000; ++i) {
    const auto rssi = static_cast<int>(static_cast<int32_t>(in.next()));
    TEST_ASSERT_EQUAL_STRING(legacy::signal_text(rssi).c_str(), sut::signal_text(rssi).c_str());
    TEST_ASSERT_EQUAL_STRING(legacy::net_row_signal_text(false, rssi).c_str(),
                             sut::net_row_signal_text(false, rssi).c_str());
    const std::size_t count = static_cast<std::size_t>(in.next() >> in.below(64));
    TEST_ASSERT_EQUAL_STRING(legacy::release_count_text(count).c_str(),
                             sut::release_count_text(count).c_str());
  }
}

namespace {

// One text written at every buffer size from 0 to `size`: it must be the full text cut to
// size - 1 characters, terminated, with nothing written past the buffer.
template <class Write>
void check_cut(const std::string &full, std::size_t size, Write write, const char *what) {
  TEST_ASSERT_LESS_THAN_size_t_MESSAGE(size, full.size(), what); // the size holds the text
  for (std::size_t n = 0; n <= size; ++n) {
    std::array<char, 80> out{};
    out.fill('#');
    write(std::span<char>(out.data(), n));
    if (n == 0) {
      TEST_ASSERT_EQUAL_CHAR_MESSAGE('#', out.at(0), what);
      continue;
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(full.substr(0, n - 1).c_str(), out.data(), what);
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('#', out.at(n), what);
  }
}

} // namespace

TEST_CASE("FMT-116 each buffer size holds its longest text, and a shorter buffer cuts it",
          "[hmi_format][about][update][net]") {
  namespace f = hmi::format;
  constexpr std::size_t SIZE_MAX_BYTES = std::numeric_limits<std::size_t>::max();
  constexpr int32_t I_MIN = std::numeric_limits<int32_t>::min();
  const std::string hex(64, 'f');
  check_cut(
      legacy::sha_line(hex, 32), f::SHA_LINE_TEXT_SIZE,
      [&](std::span<char> o) { f::sha_line_text(hex, 32, o); }, "sha_line_text");
  const std::array<uint8_t, 6> mac{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  check_cut(
      legacy::mac_text(mac), f::MAC_TEXT_SIZE, [&](std::span<char> o) { f::mac_text(mac, o); },
      "mac_text");
  const std::string iso = "-2147483648-01--2147483648";
  check_cut(
      legacy::day(iso), f::DAY_TEXT_SIZE, [&](std::span<char> o) { (void)f::day_text(iso, o); },
      "day_text");
  check_cut(
      legacy::megabytes(SIZE_MAX_BYTES), f::MEGABYTES_TEXT_SIZE,
      [&](std::span<char> o) { f::megabytes_text(SIZE_MAX_BYTES, o); }, "megabytes_text");
  check_cut(
      legacy::release_count_text(SIZE_MAX_BYTES), f::RELEASE_COUNT_TEXT_SIZE,
      [&](std::span<char> o) { f::release_count_text(SIZE_MAX_BYTES, o); }, "release_count_text");
  check_cut(
      legacy::progress_text(SIZE_MAX_BYTES, SIZE_MAX_BYTES, I_MIN), f::PROGRESS_TEXT_SIZE,
      [&](std::span<char> o) { f::progress_text(SIZE_MAX_BYTES, SIZE_MAX_BYTES, I_MIN, o); },
      "progress_text");
  check_cut(
      legacy::signal_text(I_MIN), f::SIGNAL_TEXT_SIZE,
      [&](std::span<char> o) { f::signal_text(I_MIN, o); }, "signal_text");
  check_cut(
      legacy::net_row_signal_text(false, I_MIN), f::ROW_SIGNAL_TEXT_SIZE,
      [&](std::span<char> o) { f::net_row_signal_text(false, I_MIN, o); }, "net_row_signal_text");
  // A date that does not parse leaves the buffer empty: the caller shows the input instead.
  std::array<char, f::DAY_TEXT_SIZE> out{};
  out.fill('#');
  TEST_ASSERT_FALSE(f::day_text("2026-13-01", out));
  TEST_ASSERT_EQUAL_STRING("", out.data());
}
