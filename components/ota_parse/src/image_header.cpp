// The first block of a downloaded image. Moved from main/github_ota.cpp (dev_refactor 54347bb,
// check_header :305-323 and install()'s length check :389) with the same answers; see
// test/goldens.hpp. Reads the fields from the bytes, little-endian as the chip lays them out,
// instead of copying them into ESP-IDF's structs.

#include <algorithm>

#include "ota_parse/ota_parse.hpp"

namespace hmi::ota {
namespace {

std::uint32_t read_le(std::span<const std::uint8_t> bytes, std::size_t at, std::size_t n) {
  std::uint32_t v = 0;
  for (std::size_t i = 0; i < n; i++) {
    v |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
  }
  return v;
}

// A text field of the app description, up to its NUL or the end of the field: what strncmp
// and fmt's "{:.Ns}" saw of it before the move.
std::string_view desc_text(std::span<const std::uint8_t> block, DescField field) {
  const auto *begin = reinterpret_cast<const char *>(block.data() + kAppDescOffset + field.offset);
  const auto *end = std::find(begin, begin + field.size, '\0');
  return {begin, static_cast<std::size_t>(end - begin)};
}

} // namespace

std::string check_first_block(std::span<const std::uint8_t> block, std::uint16_t chip_id,
                              std::string_view running_project, AppDesc &desc) {
  if (block.size() < kDescEnd) {
    return "The file is too short to be firmware";
  }
  if (block[0] != kImageMagic || read_le(block, kAppDescOffset, 4) != kAppDescMagic) {
    return "The file is not firmware";
  }
  if (read_le(block, kChipIdOffset, 2) != chip_id) {
    return "The file is firmware for another chip";
  }
  const std::string_view project = desc_text(block, kDescProjectName);
  if (project != running_project) {
    return "The file is '" + std::string(project) + "', not this HMI's firmware";
  }
  desc.project_name = project;
  desc.version = desc_text(block, kDescVersion);
  desc.date = desc_text(block, kDescDate);
  desc.time = desc_text(block, kDescTime);
  return "";
}

} // namespace hmi::ota
