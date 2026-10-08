#pragma once
// What the screens show about the firmware running: its identity and whether it is a release.

#include <optional>
#include <string>

namespace hmi::ui {

/// The GitHub release this image is (main's FwRelease, without when it was checked).
struct FirmwareRelease {
  std::string tag;         ///< e.g. "v4.0.0-alpha"
  bool prerelease = false; ///< GitHub marks it a pre-release
};

/// What main knows of the running image so far (main's FwInfo).
struct FirmwareInfo {
  bool done = false;                      ///< the hash has been attempted
  std::optional<std::string> sha256;      ///< 64 lowercase hex digits; nullopt if it failed
  std::optional<FirmwareRelease> release; ///< the release whose binary this image is, if known
};

/// The image's own description (esp_app_desc_t) and the commit it was built from.
struct FirmwareIdentity {
  const char *version; ///< git describe at build time
  const char *date;    ///< build date
  const char *time;    ///< build time
  const char *commit;  ///< short commit hash, or "unknown"
};

} // namespace hmi::ui
