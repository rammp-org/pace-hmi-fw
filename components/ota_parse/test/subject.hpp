#pragma once
// The code under test, behind one interface: every golden case runs against each subject in
// SUBJECTS, and each answer is turned into one canonical text (describe.hpp) before it is
// compared. "legacy" is the verbatim pre-move copy (legacy_ota_parse.cpp).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ota_test {

struct Subject {
  const char *name;
  /// parse_releases, described: "error=<e> n=<count>" then one line per release.
  std::string (*releases)(const std::string &json);
  /// readable_notes.
  std::string (*notes)(std::string_view body);
  /// The first block's check before anything is written: "" = accepted, else why not.
  std::string (*header)(const std::vector<std::uint8_t> &first_block);
  /// find_release over fwinfo.txt's contents (nullopt: no file), described.
  std::string (*fwinfo)(const std::optional<std::string> &file, const std::string &sha256);
  /// Whether the confirms-its-boot marker was seen in the chunks read (an empty chunk ends
  /// the read, as esp_http_client_read() == 0 does).
  bool (*marker)(const std::vector<std::string> &chunks);
};

const std::vector<Subject> &subjects();

} // namespace ota_test
