#pragma once
// Parsers for what a firmware update reads from outside: GitHub's release list (JSON over
// HTTPS), the first block of a downloaded image, fwinfo.txt from /storage, and the
// confirms-its-boot marker in the image bytes. Pure: no ESP-IDF, no HTTP, no logging. Moved
// from main/github_ota.cpp and main/fw_info.cpp with the same answers (test/goldens.hpp).

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::ota {

// ---- the release list --------------------------------------------------------------------------

/// Release notes longer than this are cut: the panel is for a glance.
inline constexpr std::size_t kNotesMaxChars = 1800;

/// One release from GitHub's list (main/github_ota.hpp GithubRelease, field for field).
struct Release {
  std::string tag;         ///< "v4.0.0-alpha"
  bool prerelease = false; ///< GitHub marks it a pre-release
  std::string published;   ///< "2026-09-30": the first 10 characters of published_at
  std::string notes;       ///< the release text, trimmed to what reads on the panel
  std::string url;         ///< where the image asset downloads from; empty if it has none
  std::size_t size = 0;    ///< the image asset's length in bytes
  std::string sha256;      ///< GitHub's digest of it, 64 characters; empty if not given
};

/// The release text as the panel can draw it: ASCII, without the CI size report or HTML
/// comments, bold and code marks, CRs or runs of blank lines; at most kNotesMaxChars
/// (plus a stand-in that crosses it) and then "...".
[[nodiscard]] std::string readable_notes(std::string_view body);

/// The releases in GitHub's answer to GET /repos/{owner}/{repo}/releases, in its order, drafts
/// and untagged ones left out. `asset_name` is the image's file name. On anything but a JSON
/// array, sets `error` and returns none. Uses cJSON with whatever hooks the caller installed.
[[nodiscard]] std::vector<Release> parse_releases(std::string_view json,
                                                  std::string_view asset_name, std::string &error);

// ---- the image's first block ------------------------------------------------------------------

// The image header, first segment header and app description, in the order esptool lays them
// out (ESP-IDF v6.0 esp_app_format.h, esp_app_desc.h; main/github_ota.cpp asserts these
// against the real structs).
inline constexpr std::size_t kImageHeaderBytes = 24;  ///< sizeof(esp_image_header_t)
inline constexpr std::size_t kSegmentHeaderBytes = 8; ///< sizeof(esp_image_segment_header_t)
inline constexpr std::size_t kAppDescBytes = 256;     ///< sizeof(esp_app_desc_t)
inline constexpr std::size_t kAppDescOffset = kImageHeaderBytes + kSegmentHeaderBytes;
inline constexpr std::size_t kDescEnd = kAppDescOffset + kAppDescBytes;
inline constexpr std::size_t kChipIdOffset = 12;           ///< in esp_image_header_t, uint16 LE
inline constexpr std::uint8_t kImageMagic = 0xE9;          ///< ESP_IMAGE_HEADER_MAGIC
inline constexpr std::uint32_t kAppDescMagic = 0xABCD5432; ///< ESP_APP_DESC_MAGIC_WORD

/// A field of esp_app_desc_t: its offset in the description and its size.
struct DescField {
  std::size_t offset;
  std::size_t size;
};
inline constexpr DescField kDescVersion{16, 32};
inline constexpr DescField kDescProjectName{48, 32};
inline constexpr DescField kDescTime{80, 16};
inline constexpr DescField kDescDate{96, 16};

/// The image's description, each text up to its NUL or the end of its field.
struct AppDesc {
  std::string project_name;
  std::string version;
  std::string date;
  std::string time;
};

/// Whether the first block of a download is this project's image for this chip, before any of
/// it is written. "" when it is, with `desc` filled; else why not, for the panel.
[[nodiscard]] std::string check_first_block(std::span<const std::uint8_t> block,
                                            std::uint16_t chip_id, std::string_view running_project,
                                            AppDesc &desc);

// ---- fwinfo.txt -------------------------------------------------------------------------------

/// A line of fwinfo.txt: `<sha256>  <tag>  <release|prerelease>  <checked>`.
struct FwRecord {
  std::string tag;
  bool prerelease = false; ///< the kind is exactly "prerelease"
  std::string checked;     ///< empty when the line has no fourth field
};

/// The first line of `in` whose hash is `sha256`. Blank lines and lines starting with '#' are
/// skipped, and so are lines with fewer than three fields.
[[nodiscard]] std::optional<FwRecord> find_fw_record(std::istream &in, std::string_view sha256);

// ---- the confirms-its-boot marker -------------------------------------------------------------

/// Looks for `marker` in a stream of chunks, across chunk boundaries too. Keeps at most the
/// marker's length less one byte between chunks, plus the chunk being searched.
class MarkerSearch {
public:
  /// `marker` must outlive the search.
  explicit MarkerSearch(std::string_view marker)
      : marker_(marker) {}

  /// The next bytes read. Once the marker is found, does nothing.
  void feed(std::span<const std::uint8_t> data);

  [[nodiscard]] bool found() const { return found_; }

private:
  std::string_view marker_;
  std::string carry_; ///< the tail of the last chunk, for a marker across two
  bool found_ = false;
};

} // namespace hmi::ota
