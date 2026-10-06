// The confirms-its-boot marker in a downloaded image. Moved from main/github_ota.cpp
// (dev_refactor 54347bb, install() :369-371 and :423-427) with the same answers.

#include <algorithm>

#include "ota_parse/ota_parse.hpp"

namespace hmi::ota {

void MarkerSearch::feed(std::span<const std::uint8_t> data) {
  if (found_) {
    return;
  }
  carry_.append(reinterpret_cast<const char *>(data.data()), data.size());
  found_ = carry_.find(marker_) != std::string::npos;
  carry_.erase(0, carry_.size() - std::min(carry_.size(), marker_.size() - 1));
}

} // namespace hmi::ota
