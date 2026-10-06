// L1 golden tests for the About, Firmware update and Internet screens' text helpers
// (components/hmi_format, REQ-FMT-1x in its README). FMT-101..110 walk a frozen golden table
// (goldens_screens.hpp, recorded from the pre-move code) through the code under test
// (subject_screens.hpp). One behaviour per case.

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "goldens_screens.hpp"
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
