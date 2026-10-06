// The "legacy" subject: the verbatim pre-move parsers (legacy_ota_parse.cpp).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

#include "describe.hpp"
#include "legacy_ota_parse.hpp"
#include "subject.hpp"

namespace ota_test {
namespace {

std::string legacy_releases(const std::string &json) {
  std::string error;
  const std::vector<GithubRelease> rs = legacy::parse_releases(json, error);
  std::vector<ReleaseView> views;
  views.reserve(rs.size());
  for (const GithubRelease &r : rs) {
    views.push_back({r.tag, r.prerelease, r.published, r.notes, r.url, r.size, r.sha256});
  }
  return describe_releases(views, error);
}

std::string legacy_notes(std::string_view body) { return legacy::readable_notes(body); }

std::string legacy_header(const std::vector<std::uint8_t> &block) {
  return legacy::first_block_check(block.data(), block.size());
}

// find_release reads storage_path("fwinfo.txt"): a folder of this process's own under /tmp.
const std::string &storage_dir() {
  static const std::string dir = [] {
    std::string d = "/tmp/ota_parse_test_" + std::to_string(::getpid());
    std::string cmd = "mkdir -p " + d;
    if (std::system(cmd.c_str()) != 0) {
      std::abort();
    }
    legacy::set_storage_dir(d);
    return d;
  }();
  return dir;
}

std::string legacy_fwinfo(const std::optional<std::string> &file, const std::string &sha256) {
  const std::string path = storage_dir() + "/fwinfo.txt";
  std::remove(path.c_str());
  if (file) {
    std::ofstream out(path, std::ios::binary);
    out << *file;
  }
  const std::optional<FwRelease> found = legacy::find_release(sha256);
  if (!found) {
    return "none";
  }
  return describe_fw_record(found->tag, found->prerelease, found->checked);
}

bool legacy_marker(const std::vector<std::string> &chunks) {
  return legacy::has_marker_after(chunks);
}

} // namespace

const std::vector<Subject> &subjects() {
  static const std::vector<Subject> all = {
      {"legacy", legacy_releases, legacy_notes, legacy_header, legacy_fwinfo, legacy_marker},
  };
  return all;
}

} // namespace ota_test
