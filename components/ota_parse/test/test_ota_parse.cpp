// L1 host app for the OTA and firmware-info parsers (cases OTA-001..OTA-104).
//
// Characterisation (TS-UNIT-07, plan §1/§3.2): every case pins what the code does TODAY, bad
// or not, as a golden in goldens.hpp, recorded from the verbatim pre-move copy
// (legacy_ota_parse.cpp). A case named "today ..." pins a hazard that is parked, not fixed:
// changing it is a behaviour change with its own commit and review. Every golden runs against
// each subject in subject_*.cpp, so after the move the component must give the same answers.
//
// Hostile inputs (TS-UNIT-07): empty, truncated at every length, wrong types, missing fields,
// sizes at/above/far above any limit, non-UTF-8, deep nesting, and seeded random bytes. Under
// -fsanitize=address,undefined a hang, overrun or crash fails the run.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "describe.hpp"
#include "fixtures.hpp"
#include "goldens.hpp"
#include "ota_parse/ota_parse.hpp"
#include "subject.hpp"
#include "test_case.hpp"

namespace ota_test {

const std::vector<Subject> &subjects() {
  static const std::vector<Subject> all = {legacy_subject(), component_subject()};
  return all;
}

namespace {

constexpr std::string_view kMarker = "RAMMP-HMI:confirms-its-boot:v1"; // github_ota.cpp:33

// Compares `actual` (already digested) with the golden `key`. A missing golden prints the line
// to add; it never passes.
void expect_golden(const Subject &s, std::string_view key, const std::string &actual) {
  const std::string_view *want = find_golden(key);
  if (want == nullptr) {
    std::printf("MISSING-GOLDEN    {\"%.*s\", R\"g(%s)g\"},\n", static_cast<int>(key.size()),
                key.data(), actual.c_str());
    TEST_FAIL_MESSAGE("no golden for this key");
    return;
  }
  if (*want != actual) {
    std::printf("[%s] %.*s\n  want: %.*s\n  got:  %s\n", s.name, static_cast<int>(key.size()),
                key.data(), static_cast<int>(want->size()), want->data(), actual.c_str());
  }
  TEST_ASSERT_EQUAL_STRING_MESSAGE(std::string(*want).c_str(), actual.c_str(), s.name);
}

// readable_notes' answer as a golden holds it: printable, one line (describe.hpp).
std::string notes_of(const Subject &s, std::string_view body) { return escaped(s.notes(body)); }

std::string json_str(std::string_view s) { return "\"" + std::string(s) + "\""; }

// A seeded generator whose stream is fixed by the standard (mt19937_64), not by a library's
// distributions: the goldens over random inputs stay valid on any host.
struct Rng {
  explicit Rng(std::uint64_t seed)
      : gen(seed) {}
  std::uint64_t next(std::uint64_t bound) { return bound == 0 ? 0 : gen() % bound; }
  std::uint8_t byte() { return static_cast<std::uint8_t>(gen() & 0xFF); }
  std::mt19937_64 gen;
};

std::string random_bytes(Rng &rng, std::size_t n) {
  std::string out(n, '\0');
  std::generate(out.begin(), out.end(), [&rng] { return static_cast<char>(rng.byte()); });
  return out;
}

} // namespace
} // namespace ota_test

// TEST_CASE expands to file-scope definitions: the cases sit outside the namespaces, as in the
// other host apps, so a parser that does not expand the macro (cppcheck, with no include path
// to tests/host) still reads the file. Helpers between cases get their own anonymous namespace.
using namespace ota_test;

// ---- the release list ------------------------------------------------------------------------

TEST_CASE("OTA-001 a realistic release list keeps tagged non-draft releases and finds "
          "rammp-hmi-p4.bin with its size and digest",
          "[ota][releases]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-001", digest(s.releases(std::string(kReleasesRealistic))));
  }
}

TEST_CASE("OTA-002 an empty list parses to no releases and no error", "[ota][releases]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-002", digest(s.releases("[]")));
  }
}

TEST_CASE("OTA-003 today a duplicated image asset gives the last one's url and size with the "
          "first one's digest",
          "[ota][releases][hazard]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-003", digest(s.releases(std::string(kReleasesDuplicateAsset))));
  }
}

TEST_CASE("OTA-010 empty input is not a release list", "[ota][releases][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-010", digest(s.releases("")));
  }
}

TEST_CASE("OTA-011 every truncation of the realistic list is rejected without a crash",
          "[ota][releases][hostile]") {
  const std::string full(kReleasesRealistic);
  for (const Subject &s : subjects()) {
    std::string all;
    std::size_t accepted = 0;
    for (std::size_t n = 0; n < full.size(); n++) {
      const std::string d = s.releases(full.substr(0, n));
      accepted += d.starts_with("error= ") ? std::size_t{1} : std::size_t{0};
      all += d;
      all += '\x1e';
    }
    expect_golden(s, "OTA-011", "accepted=" + std::to_string(accepted) + " " + digest(all));
  }
}

TEST_CASE("OTA-012 a root that is not an array is not a release list", "[ota][releases][hostile]") {
  constexpr std::array<std::string_view, 8> kRoots = {
      "{}", R"({"tag_name":"v1"})", "\"[]\"", "42", "null", "true", "[", "]"};
  for (const Subject &s : subjects()) {
    std::string all;
    for (std::string_view root : kRoots) {
      all += s.releases(std::string(root)) + "|";
    }
    expect_golden(s, "OTA-012", digest(all));
  }
}

TEST_CASE("OTA-013 fields of the wrong type read as empty, false or 0",
          "[ota][releases][hostile]") {
  constexpr std::array<std::string_view, 8> kDocs = {
      R"([{"tag_name":7}])",
      R"([{"tag_name":"v1","prerelease":"true","draft":"true"}])",
      R"([{"tag_name":"v1","prerelease":1,"draft":1}])",
      R"([{"tag_name":"v1","published_at":20260930,"body":["x"]}])",
      R"([{"tag_name":"v1","assets":"rammp-hmi-p4.bin"}])",
      R"([{"tag_name":"v1","assets":[{"name":["rammp-hmi-p4.bin"]}]}])",
      R"([{"tag_name":"v1","assets":[{"name":"rammp-hmi-p4.bin","size":"3891200","browser_download_url":5,"digest":7}]}])",
      R"([{"tag_name":"v1","draft":null,"prerelease":null,"assets":null}])",
  };
  for (const Subject &s : subjects()) {
    std::string all;
    for (std::string_view doc : kDocs) {
      all += s.releases(std::string(doc)) + "|";
    }
    expect_golden(s, "OTA-013", digest(all));
  }
}

TEST_CASE("OTA-014 releases without a tag, and elements that are not objects, are left out",
          "[ota][releases][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-014",
                  digest(s.releases(R"([{}, 1, "v9", null, [], {"tag_name":"only-tag"}])")));
  }
}

TEST_CASE("OTA-015 today assets given as an object are walked as if they were an array",
          "[ota][releases][hazard]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-015",
                  digest(s.releases(R"([{"tag_name":"v1","assets":{"a":{"name":"rammp-hmi-p4.bin",)"
                                    R"("size":5,"browser_download_url":"u"}}}])")));
  }
}

TEST_CASE("OTA-016 today a 1 MiB tag, url and body are taken whole; only the notes are cut",
          "[ota][releases][hazard]") {
  const std::string big(1u << 20, 'A');
  const std::string doc =
      "[{\"tag_name\":" + json_str(big) + ",\"body\":" + json_str(big) +
      ",\"assets\":[{\"name\":\"rammp-hmi-p4.bin\",\"browser_download_url\":" + json_str(big) +
      "}]}]";
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-016", digest(s.releases(doc)));
  }
}

TEST_CASE("OTA-017 today a 10,000-release list is taken whole (no count limit)",
          "[ota][releases][hazard]") {
  std::string doc = "[";
  for (int i = 0; i < 10000; i++) {
    doc += (i == 0 ? "" : ",") + std::string("{\"tag_name\":\"v") + std::to_string(i) + "\"}";
  }
  doc += "]";
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-017", digest(s.releases(doc)));
  }
}

TEST_CASE("OTA-018 today a release with 10,000 assets is walked whole, the last image wins",
          "[ota][releases][hazard]") {
  std::string doc = "[{\"tag_name\":\"v1\",\"assets\":[";
  for (int i = 0; i < 10000; i++) {
    doc += (i == 0 ? "" : ",") + std::string("{\"name\":\"rammp-hmi-p4.bin\",\"size\":") +
           std::to_string(i) + ",\"browser_download_url\":\"u" + std::to_string(i) + "\"}";
  }
  doc += "]}]";
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-018", digest(s.releases(doc)));
  }
}

TEST_CASE("OTA-019 non-UTF-8 bytes: the tag keeps them raw, the notes drop them",
          "[ota][releases][hostile]") {
  const std::string doc = "[{\"tag_name\":\"v1\xFF\xFE\x80\",\"published_at\":\"\xC3\xA9t\xE9\","
                          "\"body\":\"a\xFF"
                          "b\xC0\x80"
                          "c\xE2\x80\"}]";
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-019", digest(s.releases(doc)));
  }
}

TEST_CASE("OTA-020 a \\u0000 escape cuts the field at the NUL", "[ota][releases][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-020",
                  digest(s.releases(R"([{"tag_name":"v1\u0000evil","body":"a\u0000b"}])")));
  }
}

TEST_CASE("OTA-021 a bad escape rejects the whole list, but today \\uZZZZ reads as a NUL and "
          "empties the field",
          "[ota][releases][hostile]") {
  constexpr std::array<std::string_view, 4> kDocs = {
      R"([{"tag_name":"v1"},{"tag_name":"\uD800"}])", R"([{"tag_name":"\uZZZZ"}])",
      R"([{"tag_name":"\q"}])", R"([{"tag_name":"v1)"};
  for (const Subject &s : subjects()) {
    std::string all;
    for (std::string_view doc : kDocs) {
      all += s.releases(std::string(doc)) + "|";
    }
    expect_golden(s, "OTA-021", digest(all));
  }
}

namespace {
std::string nested(std::size_t depth) {
  return R"([{"tag_name":"v1","x":)" + std::string(depth, '[') + std::string(depth, ']') + "}]";
}
} // namespace

TEST_CASE("OTA-022 nesting up to cJSON's limit parses; deeper, even 100,000 deep, is rejected "
          "without a crash",
          "[ota][releases][hostile]") {
  // The list itself and the release object take two levels: 998 more reach 1000.
  constexpr std::array<std::size_t, 5> kDepths = {997, 998, 999, 1000, 100000};
  for (const Subject &s : subjects()) {
    std::string all;
    for (std::size_t depth : kDepths) {
      all += std::to_string(depth) + ":" + s.releases(nested(depth)) + "|";
    }
    expect_golden(s, "OTA-022", digest(all));
  }
}

TEST_CASE("OTA-023 asset size: a fraction is cut, a string is 0, 2^32 is kept on a 64-bit host",
          "[ota][releases][hostile]") {
  constexpr std::array<std::string_view, 4> kSizes = {"3.9", "\"12\"", "4294967296", "0"};
  for (const Subject &s : subjects()) {
    std::string all;
    for (std::string_view size : kSizes) {
      all += s.releases(R"([{"tag_name":"v1","assets":[{"name":"rammp-hmi-p4.bin","size":)" +
                        std::string(size) + "}]}]") +
             "|";
    }
    expect_golden(s, "OTA-023", digest(all));
  }
}

TEST_CASE("OTA-024 today a negative or huge asset size is an out-of-range float-to-size_t cast "
          "(undefined; only 'no crash' is pinned)",
          "[ota][releases][hazard]") {
  constexpr std::array<std::string_view, 3> kSizes = {"-1", "1e300", "-1e300"};
  for (const Subject &s : subjects()) {
    for (std::string_view size : kSizes) {
      const std::string d =
          s.releases(R"([{"tag_name":"v1","assets":[{"name":"rammp-hmi-p4.bin","size":)" +
                     std::string(size) + "}]}]");
      TEST_ASSERT_TRUE(d.starts_with("error= n=1\n[tag=v1 "));
    }
  }
}

TEST_CASE("OTA-025 the digest is taken only as 'sha256:' and 64 characters, which are not "
          "checked to be hex",
          "[ota][releases][hostile]") {
  const std::string hex64(64, 'a');
  const std::array<std::string, 5> kDigests = {"sha256:" + hex64.substr(1), "sha256:" + hex64 + "a",
                                               "SHA256:" + hex64, "sha256:" + std::string(64, 'Z'),
                                               "sha512:" + hex64};
  for (const Subject &s : subjects()) {
    std::string all;
    for (const std::string &dg : kDigests) {
      all += s.releases(R"([{"tag_name":"v1","assets":[{"name":"rammp-hmi-p4.bin","digest":")" +
                        dg + "\"}]}]") +
             "|";
    }
    expect_golden(s, "OTA-025", digest(all));
  }
}

TEST_CASE("OTA-026 published_at keeps its first 10 characters, or all of a shorter one",
          "[ota][releases][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-026",
                  digest(s.releases(R"([{"tag_name":"a","published_at":"2026"},)"
                                    R"({"tag_name":"b","published_at":""}])")));
  }
}

TEST_CASE("OTA-027 seeded random bytes are rejected without a crash", "[ota][releases][hostile]") {
  for (const Subject &s : subjects()) {
    Rng rng(27);
    std::string all;
    for (int i = 0; i < 3000; i++) {
      all += s.releases(random_bytes(rng, rng.next(400))) + "\x1e";
    }
    expect_golden(s, "OTA-027", digest(all));
  }
}

TEST_CASE("OTA-028 seeded random JSON-alphabet text never crashes", "[ota][releases][hostile]") {
  constexpr std::string_view kAlphabet = "[]{}\":, 01-9.eE+truefalsnul\\u\"tag_name\"assets";
  for (const Subject &s : subjects()) {
    Rng rng(28);
    std::string all;
    for (int i = 0; i < 3000; i++) {
      std::string doc = "[";
      const std::size_t n = rng.next(200);
      for (std::size_t k = 0; k < n; k++) {
        doc += kAlphabet[rng.next(kAlphabet.size())];
      }
      all += s.releases(doc) + "\x1e";
    }
    expect_golden(s, "OTA-028", digest(all));
  }
}

TEST_CASE("OTA-029 seeded byte mutations of the realistic list never crash",
          "[ota][releases][hostile]") {
  const std::string full(kReleasesRealistic);
  for (const Subject &s : subjects()) {
    Rng rng(29);
    std::string all;
    for (int i = 0; i < 1500; i++) {
      std::string doc = full;
      const std::size_t flips = 1 + rng.next(4);
      for (std::size_t k = 0; k < flips; k++) {
        doc[rng.next(doc.size())] = static_cast<char>(rng.byte());
      }
      all += s.releases(doc) + "\x1e";
    }
    expect_golden(s, "OTA-029", digest(all));
  }
}

TEST_CASE("OTA-030 today bytes after the list, and after a NUL, are ignored",
          "[ota][releases][hazard]") {
  const std::string with_nul = std::string(R"([{"tag_name":"v1"}])") + '\0' + "garbage";
  for (const Subject &s : subjects()) {
    expect_golden(
        s, "OTA-030",
        digest(s.releases(R"([{"tag_name":"v1"}] trailing {"x":)") + "|" + s.releases(with_nul)));
  }
}

// ---- the release notes -----------------------------------------------------------------------

TEST_CASE("OTA-040 a realistic release text reads as plain ASCII", "[ota][notes]") {
  const std::string body = "## What's new\r\n\r\n* **Firmware update** from GitHub \xE2\x80\x94 "
                           "with rollback.\r\n* `About` shows the SHA.\r\n\r\n\r\n\r\nNext.\r\n";
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-040", digest(notes_of(s, body)));
  }
}

TEST_CASE("OTA-041 the CI size report and an HTML comment cut the rest", "[ota][notes]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-041",
                  digest(notes_of(s, "a\n### ESP-IDF Size Report\n|x|") + "|" +
                         notes_of(s, "b<!-- c -->d") + "|" + notes_of(s, "<!--") + "|" +
                         notes_of(s, "x<!--y### ESP-IDF Size Report")));
  }
}

TEST_CASE("OTA-042 bold, code marks and CR are dropped; a single star is kept", "[ota][notes]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-042",
                  digest(notes_of(s, "**b** `c` *i* a\r\nz***") + "|" + notes_of(s, "*") + "|" +
                         notes_of(s, "**")));
  }
}

TEST_CASE("OTA-043 more than one blank line becomes one", "[ota][notes]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-043", digest(notes_of(s, "a\n\n\n\n\nb\n\nc\nd")));
  }
}

TEST_CASE("OTA-044 eight punctuation marks get ASCII stand-ins; other UTF-8 is dropped",
          "[ota][notes]") {
  const std::string body = "\xE2\x80\x94\xE2\x80\x93\xE2\x80\x98\xE2\x80\x99\xE2\x80\x9C\xE2\x80"
                           "\x9D\xE2\x86\x92\xE2\x80\xA6|\xC3\xA9|\xF0\x9F\x9A\x80|\xE2\x84\xA2";
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-044", digest(notes_of(s, body)));
  }
}

TEST_CASE("OTA-045 broken UTF-8 is dropped without reading past the end", "[ota][notes][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-045",
                  digest(notes_of(s, "a\xFF") + "|" + notes_of(s, "a\xF0") + "|" +
                         notes_of(s, "a\xE2\x80") + "|" + notes_of(s, "\x80\x80z") + "|" +
                         notes_of(s, "\xC0") + "|" + notes_of(s, std::string(5, '\xFF'))));
  }
}

TEST_CASE("OTA-046 notes at 1799, 1800, 1801 and 1 MiB characters: cut at 1800 plus '...'",
          "[ota][notes][hostile]") {
  constexpr std::array<std::size_t, 5> kLengths = {1799, 1800, 1801, 4000, 1u << 20};
  for (const Subject &s : subjects()) {
    std::string all;
    for (std::size_t n : kLengths) {
      const std::string out = notes_of(s, std::string(n, 'x'));
      all += std::to_string(n) + ":" + std::to_string(out.size()) + ":" + out.substr(1790) + "|";
    }
    expect_golden(s, "OTA-046", digest(all));
  }
}

TEST_CASE("OTA-047 today a stand-in at the limit runs past 1800 before '...' is added",
          "[ota][notes][hazard]") {
  const std::string body = std::string(1799, 'x') + "\xE2\x80\xA6" + "tail";
  for (const Subject &s : subjects()) {
    const std::string out = notes_of(s, body);
    expect_golden(s, "OTA-047", std::to_string(out.size()) + ":" + out.substr(1795));
  }
}

TEST_CASE("OTA-048 trailing spaces and newlines go; text that is all dropped is empty",
          "[ota][notes]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-048",
                  digest(notes_of(s, "a \n \n") + "|" + notes_of(s, "\r\r``") + "|" +
                         notes_of(s, "") + "|" + notes_of(s, " \n\n ")));
  }
}

TEST_CASE("OTA-049 seeded random bytes never crash the notes", "[ota][notes][hostile]") {
  for (const Subject &s : subjects()) {
    Rng rng(49);
    std::string all;
    for (int i = 0; i < 3000; i++) {
      all += notes_of(s, random_bytes(rng, rng.next(3000))) + "\x1e";
    }
    expect_golden(s, "OTA-049", digest(all));
  }
}

// ---- the image header (first block of the download) -----------------------------------------

namespace {
constexpr std::size_t kDescEnd = 24 + 8 + 256;
constexpr std::size_t kDesc = 24 + 8;

std::vector<std::uint8_t> image_block(std::size_t size, std::string_view project) {
  std::vector<std::uint8_t> b(size, 0);
  if (size >= kDescEnd) {
    b[0] = 0xE9;  // ESP_IMAGE_HEADER_MAGIC
    b[12] = 0x12; // chip_id ESP32-P4, little-endian
    b[13] = 0x00;
    const std::uint32_t magic = 0xABCD5432; // ESP_APP_DESC_MAGIC_WORD
    for (std::size_t i = 0; i < 4; i++) {
      b[kDesc + i] = static_cast<std::uint8_t>(magic >> (8 * i));
    }
    const std::string_view version = "v4.1.0", time = "18:02:11", date = "Sep 30 2026";
    for (std::size_t i = 0; i < version.size(); i++) {
      b[kDesc + 16 + i] = static_cast<std::uint8_t>(version[i]);
    }
    for (std::size_t i = 0; i < project.size() && i < 32; i++) {
      b[kDesc + 48 + i] = static_cast<std::uint8_t>(project[i]);
    }
    for (std::size_t i = 0; i < time.size(); i++) {
      b[kDesc + 80 + i] = static_cast<std::uint8_t>(time[i]);
    }
    for (std::size_t i = 0; i < date.size(); i++) {
      b[kDesc + 96 + i] = static_cast<std::uint8_t>(date[i]);
    }
  }
  return b;
}
} // namespace

TEST_CASE("OTA-060 this project's image for this chip is accepted, from 288 bytes up",
          "[ota][header]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-060",
                  "[" + s.header(image_block(kDescEnd, "rammp-hmi-p4")) + "][" +
                      s.header(image_block(64 * 1024, "rammp-hmi-p4")) + "]");
  }
}

TEST_CASE("OTA-061 a first block under 288 bytes is too short", "[ota][header][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-061",
                  s.header({}) + "|" + s.header(std::vector<std::uint8_t>(1, 0xE9)) + "|" +
                      s.header(std::vector<std::uint8_t>(kDescEnd - 1, 0xE9)));
  }
}

TEST_CASE("OTA-062 a wrong image or app-description magic is not firmware",
          "[ota][header][hostile]") {
  for (const Subject &s : subjects()) {
    std::vector<std::uint8_t> a = image_block(kDescEnd, "rammp-hmi-p4");
    a[0] = 0xEA;
    std::vector<std::uint8_t> b = image_block(kDescEnd, "rammp-hmi-p4");
    b[kDesc + 3] = 0x00;
    expect_golden(s, "OTA-062", s.header(a) + "|" + s.header(b));
  }
}

TEST_CASE("OTA-063 an image for another chip is refused", "[ota][header][hostile]") {
  for (const Subject &s : subjects()) {
    std::vector<std::uint8_t> a = image_block(kDescEnd, "rammp-hmi-p4");
    a[12] = 0x00; // ESP32
    std::vector<std::uint8_t> b = image_block(kDescEnd, "rammp-hmi-p4");
    b[13] = 0x01; // 0x0112: the high byte counts too
    expect_golden(s, "OTA-063", s.header(a) + "|" + s.header(b));
  }
}

TEST_CASE("OTA-064 another project's image is refused by name, at most 32 characters of it",
          "[ota][header][hostile]") {
  for (const Subject &s : subjects()) {
    // 32 characters with no NUL: the time field after it supplies one.
    expect_golden(s, "OTA-064",
                  s.header(image_block(kDescEnd, "other-project")) + "|" +
                      s.header(image_block(kDescEnd, std::string(32, 'Q'))) + "|" +
                      s.header(image_block(kDescEnd, "")));
  }
}

TEST_CASE("OTA-065 the name is compared up to its NUL: a longer name is refused, bytes after "
          "the NUL are ignored",
          "[ota][header][hostile]") {
  for (const Subject &s : subjects()) {
    std::vector<std::uint8_t> after_nul = image_block(kDescEnd, "rammp-hmi-p4");
    after_nul[kDesc + 48 + 20] = 'Z';
    expect_golden(s, "OTA-065",
                  "[" + s.header(image_block(kDescEnd, "rammp-hmi-p4-extra")) + "][" +
                      s.header(image_block(kDescEnd, "rammp-hmi-p")) + "][" + s.header(after_nul) +
                      "]");
  }
}

TEST_CASE("OTA-066 seeded random first blocks are refused without a crash",
          "[ota][header][hostile]") {
  for (const Subject &s : subjects()) {
    Rng rng(66);
    std::string all;
    for (int i = 0; i < 3000; i++) {
      std::vector<std::uint8_t> b(rng.next(kDescEnd + 64));
      for (std::uint8_t &v : b) {
        v = rng.byte();
      }
      // Every fourth block gets both magics and the chip, so the name check is reached.
      if (i % 4 == 0 && b.size() >= kDescEnd) {
        const std::vector<std::uint8_t> good = image_block(kDescEnd, "");
        for (std::size_t k : {std::size_t{0}, std::size_t{12}, std::size_t{13}, kDesc, kDesc + 1,
                              kDesc + 2, kDesc + 3}) {
          b[k] = good[k];
        }
        b[kDesc + 80] = 0; // a NUL after the name: see OTA-H1 in the README
      }
      all += s.header(b) + "\x1e";
    }
    expect_golden(s, "OTA-066", digest(all));
  }
}

TEST_CASE("OTA-067 an accepted first block reports the image's project, version, date and time",
          "[ota][header]") {
  std::vector<std::uint8_t> b = image_block(kDescEnd, "rammp-hmi-p4");
  hmi::ota::AppDesc desc;
  TEST_ASSERT_EQUAL_STRING("",
                           hmi::ota::check_first_block(b, 0x0012, "rammp-hmi-p4", desc).c_str());
  TEST_ASSERT_EQUAL_STRING("rammp-hmi-p4", desc.project_name.c_str());
  TEST_ASSERT_EQUAL_STRING("v4.1.0", desc.version.c_str());
  TEST_ASSERT_EQUAL_STRING("Sep 30 2026", desc.date.c_str());
  TEST_ASSERT_EQUAL_STRING("18:02:11", desc.time.c_str());
}

TEST_CASE("OTA-068 a description with no NUL from the name to its end is read only within its "
          "fields",
          "[ota][header][hostile]") {
  // The legacy copy is not run here: fmt's "{:.32s}" on a char array takes strlen first and
  // reads past the description (README hazard OTA-H1). The component bounds every field.
  std::vector<std::uint8_t> b = image_block(kDescEnd, "");
  for (std::size_t i = kDesc + 16; i < kDescEnd; i++) {
    b[i] = 'N';
  }
  hmi::ota::AppDesc desc;
  const std::string err = hmi::ota::check_first_block(b, 0x0012, "rammp-hmi-p4", desc);
  TEST_ASSERT_EQUAL_STRING(
      ("The file is '" + std::string(32, 'N') + "', not this HMI's firmware").c_str(), err.c_str());
  desc = {};
  const std::string ok = hmi::ota::check_first_block(b, 0x0012, std::string(32, 'N'), desc);
  TEST_ASSERT_EQUAL_STRING("", ok.c_str());
  TEST_ASSERT_EQUAL(32, desc.version.size());
  TEST_ASSERT_EQUAL(16, desc.date.size());
  TEST_ASSERT_EQUAL(16, desc.time.size());
}

// ---- fwinfo.txt ------------------------------------------------------------------------------

TEST_CASE("OTA-080 each recorded image's hash finds its line", "[ota][fwinfo]") {
  const std::string file(kFwInfoRealistic);
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-080",
                  s.fwinfo(file, std::string(kSha2)) + "|" + s.fwinfo(file, std::string(kSha1)) +
                      "|" + s.fwinfo(file, std::string(64, '1')));
  }
}

TEST_CASE("OTA-081 no file, an empty file or an unknown hash finds nothing", "[ota][fwinfo]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-081",
                  s.fwinfo(std::nullopt, std::string(kSha1)) + "|" +
                      s.fwinfo("", std::string(kSha1)) + "|" +
                      s.fwinfo(std::string(kFwInfoRealistic), std::string(64, 'f')));
  }
}

TEST_CASE("OTA-082 a line starting with '#' is a comment; one starting with a space is not",
          "[ota][fwinfo][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-082",
                  s.fwinfo("#h t release\n", "#h") + "|" + s.fwinfo("  #h t release\n", "#h") +
                      "|" + s.fwinfo("  h t release c\n", "h"));
  }
}

TEST_CASE("OTA-083 a line with fewer than three fields is skipped", "[ota][fwinfo][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-083",
                  s.fwinfo("h t\nh t2 prerelease x y\n", "h") + "|" + s.fwinfo("h\n", "h"));
  }
}

TEST_CASE("OTA-084 today any kind but exactly 'prerelease' reads as a release",
          "[ota][fwinfo][hazard]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-084",
                  s.fwinfo("h t PRERELEASE c\n", "h") + "|" + s.fwinfo("h t garbage c\n", "h"));
  }
}

TEST_CASE("OTA-085 with two lines for one hash the first wins", "[ota][fwinfo]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-085", s.fwinfo("h t1 release c1\nh t2 prerelease c2\n", "h"));
  }
}

TEST_CASE("OTA-086 an empty hash matches no line", "[ota][fwinfo][hostile]") {
  for (const Subject &s : subjects()) {
    expect_golden(s, "OTA-086", s.fwinfo(" \n\t\nh t release\n", ""));
  }
}

TEST_CASE("OTA-087 today a 1 MiB line and NUL bytes are read whole (no line limit)",
          "[ota][fwinfo][hazard]") {
  const std::string big(1u << 20, 'x');
  for (const Subject &s : subjects()) {
    const std::string with_nul = std::string("h t") + '\0' + "x release c\nh2 t2 release c2\n";
    expect_golden(s, "OTA-087",
                  digest(s.fwinfo(big + " t release c\n", big) + "|" +
                         s.fwinfo("h " + big + " release c\n", "h") + "|" +
                         s.fwinfo(with_nul, "h") + "|" + s.fwinfo(with_nul, "h2")));
  }
}

TEST_CASE("OTA-088 seeded random files never crash the lookup", "[ota][fwinfo][hostile]") {
  for (const Subject &s : subjects()) {
    Rng rng(88);
    std::string all;
    for (int i = 0; i < 400; i++) {
      std::string file = random_bytes(rng, rng.next(600));
      // Every other file carries a findable line among the noise.
      if (i % 2 == 0) {
        file.insert(rng.next(file.size() + 1), "\nh t prerelease c\n");
      }
      all += s.fwinfo(file, "h") + "\x1e";
    }
    expect_golden(s, "OTA-088", digest(all));
  }
}

// ---- the confirms-its-boot marker in the download --------------------------------------------

TEST_CASE("OTA-100 the marker inside one chunk is found", "[ota][marker]") {
  for (const Subject &s : subjects()) {
    TEST_ASSERT_TRUE(s.marker({"abc" + std::string(kMarker) + "def"}));
    TEST_ASSERT_TRUE(s.marker({std::string(kMarker)}));
  }
}

TEST_CASE("OTA-101 the marker split across two chunks at any point is found", "[ota][marker]") {
  const std::string m(kMarker);
  for (const Subject &s : subjects()) {
    std::string seen;
    for (std::size_t cut = 1; cut < m.size(); cut++) {
      seen += s.marker({"xx" + m.substr(0, cut), m.substr(cut) + "yy"}) ? '1' : '0';
    }
    expect_golden(s, "OTA-101", seen);
  }
}

TEST_CASE("OTA-102 the marker one byte per chunk is found", "[ota][marker]") {
  std::vector<std::string> chunks;
  for (char c : kMarker) {
    chunks.emplace_back(1, c);
  }
  for (const Subject &s : subjects()) {
    TEST_ASSERT_TRUE(s.marker(chunks));
  }
}

TEST_CASE("OTA-103 no marker, a marker short of its last byte, or one after the read ended is "
          "not found",
          "[ota][marker][hostile]") {
  const std::string m(kMarker);
  for (const Subject &s : subjects()) {
    TEST_ASSERT_FALSE(s.marker({}));
    TEST_ASSERT_FALSE(s.marker({"no marker here"}));
    TEST_ASSERT_FALSE(s.marker({m.substr(0, m.size() - 1)}));
    TEST_ASSERT_FALSE(s.marker({m.substr(0, 10), m.substr(11)}));
    TEST_ASSERT_FALSE(s.marker({"a", "", m}));
  }
}

TEST_CASE("OTA-104 seeded random images and chunkings find the marker exactly when it is "
          "there",
          "[ota][marker][hostile]") {
  for (const Subject &s : subjects()) {
    Rng rng(104);
    std::string seen;
    for (int i = 0; i < 300; i++) {
      std::string image = random_bytes(rng, 1 + rng.next(4000));
      if (i % 2 == 0) {
        image.insert(rng.next(image.size() + 1), kMarker);
      }
      std::vector<std::string> chunks;
      for (std::size_t at = 0; at < image.size();) {
        const std::size_t n = 1 + rng.next(700);
        chunks.push_back(image.substr(at, n));
        at += n;
      }
      seen += s.marker(chunks) ? '1' : '0';
    }
    expect_golden(s, "OTA-104", digest(seen));
  }
}
