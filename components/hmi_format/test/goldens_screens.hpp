#pragma once
// GOLDEN tables: what the About, Firmware update and Internet screens' text helpers gave before
// they moved out of main/ (dev_refactor 54347bb), recorded from the verbatim copies in
// legacy_screens.cpp (fmt 12.1 from espp's format.hpp, the host C library's sscanf). Frozen: a
// failing row is a behaviour change in the code under test, never a reason to edit this file
// (CORE never-list).
//
// Domain: day()'s numbers fit an int (sscanf's overflow is the C library's own, glibc and newlib
// differ), and done * 100 fits 32 bits (the target's size_t), so every row holds on the target.

#include <array>
#include <cstddef>
#include <cstdint>

namespace fmt_test {

struct TagGolden {
  const char *version;
  bool names_a_tag;
};

struct ShaGolden {
  const char *hex;
  std::size_t from;
  const char *text;
};

struct MacGolden {
  std::array<uint8_t, 6> mac;
  const char *text;
};

struct DayGolden {
  const char *iso;
  const char *text;
};

struct SizeTextGolden {
  std::size_t value;
  const char *text;
};

struct PctGolden {
  std::size_t done;
  std::size_t total;
  int32_t pct;
};

struct ProgressGolden {
  std::size_t done;
  std::size_t total;
  int32_t pct;
  const char *text;
};

struct SignalGolden {
  bool has_rssi;
  int rssi_dbm;
  const char *text;
};

struct RowSignalGolden {
  bool secured;
  int rssi_dbm;
  const char *text;
};

// names_a_tag
inline constexpr TagGolden TAG_GOLDEN[] = {
    {"", false},
    {"v", true},
    {"V4.0.0", false},
    {"4.0.0", false},
    {"unknown", false},
    {"a4c1d47", false},
    {"v4.0.0-alpha", true},
    {"v4.0.0", true},
    {"v4.0.0-alpha-3-ga4c1d47", false},
    {"v4.0.0-alpha-dirty", false},
    {"v4.0.0-alpha-3-ga4c1d47-dirty", false},
    {"v4.0.0-dirty-3-ga4c1d47", false},
    {"v4.0.0-g", true},
    {"v4.0.0-gXYZ", true},
    {"v4.0.0-gA4C1D47", true},
    {"v1-g1-g2", false},
    {"v1-gabc-x", true},
    {"vdirty", true},
    {"v-dirt", true},
    {"v-dirtyx", false},
    {"v-g0", false},
    {"v4.0.0-rc-g", true},
    {"v4.0.0-3-g0123456789abcdef", false},
    {"v4.0.0-3-ga4c1d4g", true},
    {"v-g", true},
    {"v-gg", true},
    {"va-gb", false},
    {"v\x01"
     "-g1",
     false},
};

// show_sha's line text: the hex digest and the offset of its line (0 or 32)
inline constexpr ShaGolden SHA_GOLDEN[] = {
    {"0123456789abcdef1032547698badcfe02468ace13579bdffedcba9876543210", 0,
     "01234567 89abcdef 10325476 98badcfe"},
    {"0123456789abcdef1032547698badcfe02468ace13579bdffedcba9876543210", 32,
     "02468ace 13579bdf fedcba98 76543210"},
    {"0123456789abcdef1032547698badcfe02468ace13579bdffedcba9876543210ff", 0,
     "01234567 89abcdef 10325476 98badcfe"},
    {"0123456789abcdef1032547698badcfe02468ace13579bdffedcba9876543210ff", 32,
     "02468ace 13579bdf fedcba98 76543210"},
    {"0123456789abcdef1032547698badcfe02468ace13579bdffedcba98", 32, "02468ace 13579bdf fedcba98 "},
    {"0123456789abcdef1032547698badcfe02468ace13579bdffedcba987", 32,
     "02468ace 13579bdf fedcba98 7"},
    {"0123456789abcdef1032547698badcfe02468ace13579bdffedcba987654", 32,
     "02468ace 13579bdf fedcba98 7654"},
    {"0123456789abcdef10325476", 0, "01234567 89abcdef 10325476 "},
    {"0123456789abcdef1032547698badc", 0, "01234567 89abcdef 10325476 98badc"},
    {"0123456789abcdef1032547698badcfe0", 0, "01234567 89abcdef 10325476 98badcfe"},
    {"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 0,
     "ffffffff ffffffff ffffffff ffffffff"},
    {"0000000000000000000000000000000000000000000000000000000000000000", 32,
     "00000000 00000000 00000000 00000000"},
};

// fill_static's MAC text
inline constexpr MacGolden MAC_GOLDEN[] = {
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, "00:00:00:00:00:00"},
    {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, "FF:FF:FF:FF:FF:FF"},
    {{0x30, 0xed, 0xa0, 0x1b, 0x2c, 0x3d}, "30:ED:A0:1B:2C:3D"},
    {{0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}, "0A:0B:0C:0D:0E:0F"},
    {{0x10, 0x09, 0x90, 0xa5, 0x5a, 0x01}, "10:09:90:A5:5A:01"},
};

// day: an ISO date's text, or the input itself when it does not parse
inline constexpr DayGolden DAY_GOLDEN[] = {
    {"2026-09-30", "30 Sep 2026"},
    {"2026-01-01", "1 Jan 2026"},
    {"2026-12-31", "31 Dec 2026"},
    {"2026-13-01", "2026-13-01"},
    {"2026-00-10", "2026-00-10"},
    {"2026-9-3", "3 Sep 2026"},
    {"2026-09-3x", "3 Sep 2026"},
    {"2026-09", "2026-09"},
    {"2026-09-", "2026-09-"},
    {"", ""},
    {"garbage", "garbage"},
    {"-", "-"},
    {"2026", "2026"},
    {" 2026-09-30", "30 Sep 2026"},
    {"2026- 09-30", "30 Sep 2026"},
    {"2026 -09-30", "2026 -09-30"},
    {"+2026-+09-+30", "30 Sep 2026"},
    {"-5-03-02", "2 Mar -5"},
    {"2026-09-30T12:34:56Z", "30 Sep 2026"},
    {"2026/09/30", "2026/09/30"},
    {"2026-09-99", "99 Sep 2026"},
    {"0-1-0", "0 Jan 0"},
    {"2026--09-30", "2026--09-30"},
    {"2026-06--1", "-1 Jun 2026"},
    {"2026-02-29", "29 Feb 2026"},
    {"99999-06-15", "15 Jun 99999"},
    {"2026-06-15 ", "15 Jun 2026"},
    {"\x09"
     "2026-\x0a"
     "06-\x0b"
     "15",
     "15 Jun 2026"},
    {"\x0c"
     "2026-\x0d"
     "06- 15",
     "15 Jun 2026"},
    {"2026-0x6-15", "2026-0x6-15"},
    {"2026-+-6-15", "2026-+-6-15"},
    {"2026-- 6-15", "2026-- 6-15"},
    {"000002026-000006-000015", "15 Jun 2026"},
    {"2147483647-12-2147483647", "2147483647 Dec 2147483647"},
    {"-2147483648-01--2147483648", "-2147483648 Jan -2147483648"},
    {"2026-06-15", "15 Jun 2026"},
    {"2026-1-1", "1 Jan 2026"},
    {"2026-11-1", "1 Nov 2026"},
    {"1999-12-31abc", "31 Dec 1999"},
};

// megabytes
inline constexpr SizeTextGolden MEGABYTES_GOLDEN[] = {
    {0U, "0.0 MB"},
    {1U, "0.0 MB"},
    {49999U, "0.0 MB"},
    {50000U, "0.1 MB"},
    {50001U, "0.1 MB"},
    {99999U, "0.1 MB"},
    {100000U, "0.1 MB"},
    {149999U, "0.1 MB"},
    {150000U, "0.1 MB"},
    {150001U, "0.2 MB"},
    {250000U, "0.2 MB"},
    {350000U, "0.3 MB"},
    {450000U, "0.5 MB"},
    {750000U, "0.8 MB"},
    {1250000U, "1.2 MB"},
    {1750000U, "1.8 MB"},
    {2250000U, "2.2 MB"},
    {999999U, "1.0 MB"},
    {1000000U, "1.0 MB"},
    {1048576U, "1.0 MB"},
    {1234567U, "1.2 MB"},
    {1500000U, "1.5 MB"},
    {2097152U, "2.1 MB"},
    {4194304U, "4.2 MB"},
    {8388608U, "8.4 MB"},
    {9949999U, "9.9 MB"},
    {9950000U, "9.9 MB"},
    {9999999U, "10.0 MB"},
    {16777216U, "16.8 MB"},
    {42949672U, "42.9 MB"},
    {99950000U, "100.0 MB"},
    {123456789U, "123.5 MB"},
    {999950000U, "1000.0 MB"},
    {4294967295U, "4295.0 MB"},
};

// list_status's count text
inline constexpr SizeTextGolden COUNT_GOLDEN[] = {
    {0U, "0 releases. Pick one to install it."},
    {1U, "1 releases. Pick one to install it."},
    {2U, "2 releases. Pick one to install it."},
    {9U, "9 releases. Pick one to install it."},
    {10U, "10 releases. Pick one to install it."},
    {30U, "30 releases. Pick one to install it."},
    {100U, "100 releases. Pick one to install it."},
    {65535U, "65535 releases. Pick one to install it."},
    {4294967295U, "4294967295 releases. Pick one to install it."},
};

// run_refresh's percentage; done * 100 stays inside 32 bits, as on the target
inline constexpr PctGolden PCT_GOLDEN[] = {
    {0U, 0U, 0},
    {5U, 0U, 0},
    {0U, 100U, 0},
    {50U, 100U, 50},
    {99U, 100U, 99},
    {100U, 100U, 100},
    {200U, 100U, 200},
    {1U, 3U, 33},
    {2U, 3U, 66},
    {1234567U, 2097152U, 58},
    {2097151U, 2097152U, 99},
    {42949672U, 42949672U, 100},
    {1U, 42949672U, 0},
    {21474836U, 42949672U, 50},
    {7U, 1U, 700},
    {0U, 1U, 0},
};

// run_refresh's progress text while total > 0
inline constexpr ProgressGolden PROGRESS_GOLDEN[] = {
    {0U, 2097152U, 0, "0.0 MB of 2.1 MB  (0%)"},
    {1048576U, 2097152U, 50, "1.0 MB of 2.1 MB  (50%)"},
    {2097152U, 2097152U, 100, "2.1 MB of 2.1 MB  (100%)"},
    {1234567U, 8388608U, 14, "1.2 MB of 8.4 MB  (14%)"},
    {0U, 1U, 0, "0.0 MB of 0.0 MB  (0%)"},
    {250000U, 750000U, 33, "0.2 MB of 0.8 MB  (33%)"},
    {4294967295U, 4294967295U, 100, "4295.0 MB of 4295.0 MB  (100%)"},
    {5U, 3U, -7, "0.0 MB of 0.0 MB  (-7%)"},
    {0U, 1U, INT32_MIN, "0.0 MB of 0.0 MB  (-2147483648%)"},
    {0U, 1U, 2147483647, "0.0 MB of 0.0 MB  (2147483647%)"},
};

// main_refresh's signal text; has_value false is no RSSI
inline constexpr SignalGolden SIGNAL_GOLDEN[] = {
    {false, 0, "--"},
    {true, 0, "0 dBm"},
    {true, -1, "-1 dBm"},
    {true, -45, "-45 dBm"},
    {true, -67, "-67 dBm"},
    {true, -100, "-100 dBm"},
    {true, 5, "5 dBm"},
    {true, 127, "127 dBm"},
    {true, -128, "-128 dBm"},
    {true, INT32_MIN, "-2147483648 dBm"},
    {true, 2147483647, "2147483647 dBm"},
};

// networks_fill's row signal text
inline constexpr RowSignalGolden ROW_SIGNAL_GOLDEN[] = {
    {true, 0, "0 dBm"},
    {true, -1, "-1 dBm"},
    {true, -45, "-45 dBm"},
    {true, -67, "-67 dBm"},
    {true, -100, "-100 dBm"},
    {true, 5, "5 dBm"},
    {true, 127, "127 dBm"},
    {true, -128, "-128 dBm"},
    {true, INT32_MIN, "-2147483648 dBm"},
    {true, 2147483647, "2147483647 dBm"},
    {false, 0, "open  0 dBm"},
    {false, -1, "open  -1 dBm"},
    {false, -45, "open  -45 dBm"},
    {false, -67, "open  -67 dBm"},
    {false, -100, "open  -100 dBm"},
    {false, 5, "open  5 dBm"},
    {false, 127, "open  127 dBm"},
    {false, -128, "open  -128 dBm"},
    {false, INT32_MIN, "open  -2147483648 dBm"},
    {false, 2147483647, "open  2147483647 dBm"},
};

} // namespace fmt_test
