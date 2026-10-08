#pragma once

/**
 * @file github_ota.hpp
 * @brief Firmware updates from the project's GitHub releases, fetched by the
 *        board itself over HTTPS.
 *
 * The releases are read from the public REST API
 * (api.github.com/repos/rammp-org/pace-hmi-fw/releases, no token: 60 requests
 * an hour per address). A release is installable when it carries the app
 * image, `rammp-hmi-p4.bin`; the same file `idf.py flash` writes at 0x10000.
 *
 * An install streams that file into the OTA slot not running
 * (partitions.csv), checking as it goes that it is this project's image for
 * this chip, and at the end that its SHA-256 is the digest GitHub publishes
 * for the asset. Only then does the next boot point at it.
 *
 * Rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): an image built from this
 * code confirms itself once it has run for a while (github_ota_boot_confirm),
 * and the bootloader goes back to the previous one if it resets before that.
 * Releases older than this feature never confirm themselves, and would be
 * rolled back at their first reset; they are recognised by a marker string
 * missing from the image, and installed already confirmed.
 *
 * Every call that talks to GitHub blocks for seconds: run it off the LVGL task.
 */

#include <cstddef>
#include <string>
#include <vector>

struct GithubRelease {
  std::string tag;       ///< "v4.0.0-alpha"
  bool prerelease;       ///< GitHub marks it a pre-release
  std::string published; ///< "2026-09-30"
  std::string notes;     ///< the release text, trimmed to what reads on the panel
  std::string url;       ///< where rammp-hmi-p4.bin downloads from; empty if it has none
  size_t size = 0;       ///< rammp-hmi-p4.bin's length in bytes
  std::string sha256;    ///< GitHub's digest of it, 64 hex digits; empty if not given
};

struct GithubReleases {
  bool ok = false;
  std::string error;                   ///< why not, when !ok, for the panel
  std::vector<GithubRelease> releases; ///< newest first, drafts left out
};

/// Asks GitHub for the releases. Blocks: a TLS handshake and ~120 KB of JSON.
GithubReleases github_releases_fetch();

enum class OtaStage {
  IDLE,        ///< nothing started since boot
  CONNECTING,  ///< resolving, TLS, following GitHub's redirect to the file
  DOWNLOADING, ///< writing it to the other slot
  VERIFYING,   ///< checking the image and its digest
  DONE,        ///< the next boot runs it
  FAILED,      ///< nothing changed: the running image still boots
};

struct OtaStatus {
  OtaStage stage = OtaStage::IDLE;
  std::string tag;     ///< the release being installed
  size_t done = 0;     ///< bytes written
  size_t total = 0;    ///< bytes expected
  std::string message; ///< one line: what it is doing, or why it failed
  std::string log;     ///< every step so far, one per line
};

/// Starts installing `release` on a thread of its own. False if one is
/// already running, or finished and waiting for the restart.
bool github_ota_start(const GithubRelease &release);

/// Where the install is. Any task.
OtaStatus github_ota_status();

/// Logs the slot running and whether it still has to confirm itself, and an
/// update rolled back, if there was one. Once, at boot.
void github_ota_boot_report();

/// Keeps the running image: after this the bootloader no longer rolls back
/// to the previous one. Call once the firmware has shown it runs.
void github_ota_boot_confirm();
