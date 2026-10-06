#pragma once
// GOLDEN answers: what the parsers did before they moved out of main/ (dev_refactor 54347bb),
// recorded from the verbatim copies in legacy_ota_parse.cpp on x86-64 Linux (g++ 13, cJSON
// from managed_components/espressif__cjson with CJSON_NESTING_LIMIT 1000, the Kconfig
// default). Long answers are held as their length and FNV-1a (describe.hpp). Frozen: a
// failing row is a behaviour change in the code under test, never a reason to edit this file
// (CORE never-list).

#include <algorithm>
#include <iterator>
#include <string_view>

namespace ota_test {

struct Golden {
  std::string_view key;
  std::string_view value;
};

inline constexpr Golden GOLDENS[] = {
    {"OTA-001", R"g(error= n=3
[tag=v4.1.0 pre=0 pub=2026-09-30 url=https://github.com/rammp-org/pace-hmi-fw/releases/download/v4.1.0/rammp-hmi-p4.bin size=3891200 sha=3f9a0c6e1b2d4f5a6b7c8d9e0f1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c notes=## What's new\n\n* Firmware update from GitHub releases - with rollback.\n* About shows the image's SHA256.\n\nSee docs -> OTA. It's "safe"...]
[tag=v4.1.0-rc1 pre=1 pub=2026-09-28 url=https://github.com/rammp-org/pace-hmi-fw/releases/download/v4.1.0-rc1/rammp-hmi-p4.bin size=3890000 sha= notes=Release candidate: test on the bench first.]
[tag=v3.9.0 pre=0 pub=2026-06-01 url= size=0 sha= notes=])g"},
    {"OTA-002", R"g(error= n=0)g"},
    {"OTA-003", R"g(error= n=1
[tag=v5.0.0 pre=0 pub=2026-10-01 url=https://example.com/second.bin size=200 sha=3f9a0c6e1b2d4f5a6b7c8d9e0f1a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c notes=two])g"},
    {"OTA-010", R"g(error=GitHub's answer was not a release list n=0)g"},
    {"OTA-011", R"g(accepted=0 len=191492 fnv1a=cbf23d935fb71791)g"},
    {"OTA-012",
     R"g(error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|)g"},
    {"OTA-013", R"g(error= n=0|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|)g"},
    {"OTA-014", R"g(error= n=1
[tag=only-tag pre=0 pub= url= size=0 sha= notes=])g"},
    {"OTA-015", R"g(error= n=1
[tag=v1 pre=0 pub= url=u size=5 sha= notes=])g"},
    {"OTA-016", R"g(len=2099007 fnv1a=329e477e90824fcc)g"},
    {"OTA-017", R"g(len=468904 fnv1a=1cfbcf04ed70f9c2)g"},
    {"OTA-018", R"g(error= n=1
[tag=v1 pre=0 pub= url=u9999 size=9999 sha= notes=])g"},
    {"OTA-019", R"g(error= n=1
[tag=v1\xFF\xFE\x80 pre=0 pub=\xC3\xA9t\xE9 url= size=0 sha= notes=ac])g"},
    {"OTA-020", R"g(error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=a])g"},
    {"OTA-021",
     R"g(error=GitHub's answer was not a release list n=0|error= n=0|error=GitHub's answer was not a release list n=0|error=GitHub's answer was not a release list n=0|)g"},
    {"OTA-022", R"g(997:error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|998:error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|999:error=GitHub's answer was not a release list n=0|1000:error=GitHub's answer was not a release list n=0|100000:error=GitHub's answer was not a release list n=0|)g"},
    {"OTA-023", R"g(error= n=1
[tag=v1 pre=0 pub= url= size=3 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=4294967296 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|)g"},
    {"OTA-025", R"g(error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha=ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|)g"},
    {"OTA-026", R"g(error= n=2
[tag=a pre=0 pub=2026 url= size=0 sha= notes=]
[tag=b pre=0 pub= url= size=0 sha= notes=])g"},
    {"OTA-027", R"g(len=147000 fnv1a=89aed5f33b76781d)g"},
    {"OTA-028", R"g(len=143656 fnv1a=1d4b20b37d553c6d)g"},
    {"OTA-029", R"g(len=423493 fnv1a=7c3d0b0eb86c6da3)g"},
    {"OTA-030", R"g(error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=]|error= n=1
[tag=v1 pre=0 pub= url= size=0 sha= notes=])g"},
    {"OTA-040",
     R"g(## What's new\n\n* Firmware update from GitHub - with rollback.\n* About shows the SHA.\n\nNext.)g"},
    {"OTA-041", R"g(a|b||x)g"},
    {"OTA-042", R"g(b c *i* a\nz*|*|)g"},
    {"OTA-043", R"g(a\n\nb\n\nc\nd)g"},
    {"OTA-044", R"g(--''""->...|||)g"},
    {"OTA-045", R"g(a|a|a|z||)g"},
    {"OTA-046",
     R"g(1799:1799:xxxxxxxxx|1800:1803:xxxxxxxxxx...|1801:1803:xxxxxxxxxx...|4000:1803:xxxxxxxxxx...|1048576:1803:xxxxxxxxxx...|)g"},
    {"OTA-047", R"g(1805:xxxx......)g"},
    {"OTA-048", R"g(a|||)g"},
    {"OTA-049", R"g(len=2672016 fnv1a=3a3dc4beb1e29417)g"},
    {"OTA-060", R"g([][])g"},
    {"OTA-061",
     R"g(The file is too short to be firmware|The file is too short to be firmware|The file is too short to be firmware)g"},
    {"OTA-062", R"g(The file is not firmware|The file is not firmware)g"},
    {"OTA-063", R"g(The file is firmware for another chip|The file is firmware for another chip)g"},
    {"OTA-064",
     R"g(The file is 'other-project', not this HMI's firmware|The file is 'QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ', not this HMI's firmware|The file is '', not this HMI's firmware)g"},
    {"OTA-065",
     R"g([The file is 'rammp-hmi-p4-extra', not this HMI's firmware][The file is 'rammp-hmi-p', not this HMI's firmware][])g"},
    {"OTA-066", R"g(len=110105 fnv1a=bd255b299f9cbdf4)g"},
    {"OTA-080",
     R"g(tag=v4.0.0 pre=0 checked=2026-09-01T10:00:00Z|tag=v4.1.0-rc1 pre=1 checked=unknown|tag=v3.0.0 pre=0 checked=)g"},
    {"OTA-081", R"g(none|none|none)g"},
    {"OTA-082", R"g(none|tag=t pre=0 checked=|tag=t pre=0 checked=c)g"},
    {"OTA-083", R"g(tag=t2 pre=1 checked=x|none)g"},
    {"OTA-084", R"g(tag=t pre=0 checked=c|tag=t pre=0 checked=c)g"},
    {"OTA-085", R"g(tag=t1 pre=0 checked=c1)g"},
    {"OTA-086", R"g(none)g"},
    {"OTA-087", R"g(len=1048669 fnv1a=bed3e91f966bc5f5)g"},
    {"OTA-088", R"g(len=5400 fnv1a=0c3931c0b07e644d)g"},
    {"OTA-101", R"g(11111111111111111111111111111)g"},
    {"OTA-104",
     R"g(101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010)g"},
};

inline const std::string_view *find_golden(std::string_view key) {
  const auto it = std::find_if(std::begin(GOLDENS), std::end(GOLDENS),
                               [key](const Golden &g) { return g.key == key; });
  return it == std::end(GOLDENS) ? nullptr : &it->value;
}

} // namespace ota_test
