// GitHub's release list and the release notes. Moved from main/github_ota.cpp (dev_refactor
// 54347bb, :158-247) with the same answers; see test/goldens.hpp.

#include <algorithm>
#include <memory>

#include "cJSON.h"
#include "ota_parse/ota_parse.hpp"

namespace hmi::ota {
namespace {

std::string json_string(const cJSON *obj, const char *key) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

} // namespace

std::string readable_notes(std::string_view body) {
  for (std::string_view cut : {"### ESP-IDF Size Report", "<!--"}) {
    if (const size_t at = body.find(cut); at != std::string_view::npos) {
      body = body.substr(0, at);
    }
  }
  std::string out;
  out.reserve(std::min(body.size(), kNotesMaxChars) + 8);
  // Bounded by the body's length: every pass moves i on by at least one.
  for (size_t i = 0; i < body.size() && out.size() < kNotesMaxChars; i++) {
    const auto c = static_cast<unsigned char>(body[i]);
    if (c == '\r' || c == '`' || (c == '*' && i + 1 < body.size() && body[i + 1] == '*')) {
      i += c == '*' ? 1 : 0; // "**" is bold: drop both
      continue;
    }
    if (c < 0x80) {
      if (c == '\n' && out.size() >= 2 && out.back() == '\n' && out[out.size() - 2] == '\n') {
        continue; // at most one blank line
      }
      out += static_cast<char>(c);
      continue;
    }
    // UTF-8: a few punctuation marks have ASCII stand-ins, the rest is dropped.
    const std::string_view rest = body.substr(i);
    struct Swap {
      std::string_view utf8, ascii;
    };
    static constexpr Swap kSwaps[] = {{"—", "-"},  {"–", "-"},  {"‘", "'"},  {"’", "'"},
                                      {"“", "\""}, {"”", "\""}, {"→", "->"}, {"…", "..."}};
    const auto swap = std::find_if(std::begin(kSwaps), std::end(kSwaps),
                                   [&](const Swap &s) { return rest.starts_with(s.utf8); });
    if (swap != std::end(kSwaps)) {
      out += swap->ascii;
    }
    size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    i += len - 1;
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
    out.pop_back();
  }
  if (out.size() >= kNotesMaxChars) {
    out += "...";
  }
  return out;
}

std::vector<Release> parse_releases(std::string_view json, std::string_view asset_name,
                                    std::string &error) {
  // spec-deviation(CS-FLW-02): the release and asset counts are not checked against a maximum
  // before the loops below; they are bounded by the JSON's length only. Pinned as-is by
  // OTA-017/OTA-018; the cap is a parked behaviour change (README "Hazards").
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithLength(json.data(), json.size()), cJSON_Delete);
  if (!cJSON_IsArray(root.get())) {
    error = "GitHub's answer was not a release list";
    return {};
  }
  std::vector<Release> releases;
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, root.get()) {
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "draft"))) {
      continue;
    }
    Release r;
    r.tag = json_string(item, "tag_name");
    r.prerelease = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "prerelease"));
    r.published = json_string(item, "published_at").substr(0, 10);
    r.notes = readable_notes(json_string(item, "body"));
    const cJSON *asset = nullptr;
    cJSON_ArrayForEach(asset, cJSON_GetObjectItemCaseSensitive(item, "assets")) {
      if (json_string(asset, "name") != asset_name) {
        continue;
      }
      r.url = json_string(asset, "browser_download_url");
      const cJSON *size = cJSON_GetObjectItemCaseSensitive(asset, "size");
      r.size = cJSON_IsNumber(size) ? static_cast<size_t>(size->valuedouble) : 0;
      const std::string digest = json_string(asset, "digest"); // "sha256:<hex>"
      if (digest.starts_with("sha256:") && digest.size() == 7 + 64) {
        r.sha256 = digest.substr(7);
      }
    }
    if (!r.tag.empty()) {
      releases.push_back(std::move(r));
    }
  }
  return releases;
}

} // namespace hmi::ota
