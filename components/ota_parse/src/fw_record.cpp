// fwinfo.txt records. Moved from main/fw_info.cpp (dev_refactor 54347bb, find_release
// :94-112) with the same answers; the caller opens the file. See test/goldens.hpp.

#include <sstream>

#include "ota_parse/ota_parse.hpp"

namespace hmi::ota {

std::optional<FwRecord> find_fw_record(std::istream &in, std::string_view sha256) {
  // spec-deviation(CS-FLW-02): one pass per line of the file, bounded by the file's length
  // only (no line or line-count limit). Pinned as-is by OTA-087; parked (README "Hazards").
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    std::string hash;
    std::string tag;
    std::string kind;
    if (fields >> hash >> tag >> kind && hash == sha256) {
      std::string checked;
      fields >> checked;
      return FwRecord{tag, kind == "prerelease", checked};
    }
  }
  return std::nullopt;
}

} // namespace hmi::ota
