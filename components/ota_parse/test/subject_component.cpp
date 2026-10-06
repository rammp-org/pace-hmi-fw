// The "component" subject: components/ota_parse, called as main/github_ota.cpp and
// main/fw_info.cpp call it.

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

#include "describe.hpp"
#include "legacy_ota_parse.hpp" // kRunningProject, kChipIdP4: the same stand-ins as legacy's
#include "ota_parse/ota_parse.hpp"
#include "subject.hpp"

namespace ota_test {
namespace {

constexpr char kAssetName[] = "rammp-hmi-p4.bin"; // main/github_ota.cpp

std::string component_releases(const std::string &json) {
  std::string error;
  const std::vector<hmi::ota::Release> rs = hmi::ota::parse_releases(json, kAssetName, error);
  std::vector<ReleaseView> views;
  views.reserve(rs.size());
  std::transform(rs.begin(), rs.end(), std::back_inserter(views), [](const hmi::ota::Release &r) {
    return ReleaseView{r.tag, r.prerelease, r.published, r.notes, r.url, r.size, r.sha256};
  });
  return describe_releases(views, error);
}

std::string component_notes(std::string_view body) { return hmi::ota::readable_notes(body); }

std::string component_header(const std::vector<std::uint8_t> &block) {
  hmi::ota::AppDesc desc;
  return hmi::ota::check_first_block(block, legacy::kChipIdP4, legacy::kRunningProject, desc);
}

std::string component_fwinfo(const std::optional<std::string> &file, const std::string &sha256) {
  std::optional<hmi::ota::FwRecord> found;
  if (file) {
    std::istringstream in(*file);
    found = hmi::ota::find_fw_record(in, sha256);
  } else {
    std::ifstream in("/nonexistent/ota_parse/fwinfo.txt"); // no file: the stream is failed
    found = hmi::ota::find_fw_record(in, sha256);
  }
  if (!found) {
    return "none";
  }
  return describe_fw_record(found->tag, found->prerelease, found->checked);
}

bool component_marker(const std::vector<std::string> &chunks) {
  hmi::ota::MarkerSearch search("RAMMP-HMI:confirms-its-boot:v1");
  for (const std::string &chunk : chunks) {
    if (chunk.empty()) {
      break; // esp_http_client_read() == 0 ends the read
    }
    search.feed({reinterpret_cast<const std::uint8_t *>(chunk.data()), chunk.size()});
  }
  return search.found();
}

} // namespace

Subject component_subject() {
  return {"component",      component_releases, component_notes,
          component_header, component_fwinfo,   component_marker};
}

} // namespace ota_test
