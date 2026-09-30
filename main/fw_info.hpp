#pragma once

/**
 * @file fw_info.hpp
 * @brief What firmware this is: the SHA-256 of the image as it sits in flash,
 *        and whether that is a published GitHub release.
 *
 * The SHA-256 covers the running app image byte for byte as flashed, so it
 * equals `sha256sum rammp-hmi-p4.bin` of the file that was flashed -- and so
 * the digest GitHub shows for that file on a release page.
 *
 * Whether it is a release is not something the board can find out alone. The
 * PC that flashes it can: scripts/fw_verify.py asks GitHub for the release the
 * .bin claims to be and, when the digests match, adds a line to
 * /storage/fwinfo.txt:
 *
 *     <sha256>  <tag>  <release|prerelease>  <checked, ISO 8601>
 *
 * The board looks up its OWN hash there. A line can only ever match the exact
 * image it was written for, so lines left by other firmware (an older release,
 * the other OTA slot, a rollback) are harmless, and the file is never wrong
 * about the firmware that is running.
 *
 * Hashing reads ~4 MB of flash (~0.5 s), so it runs once, on a low-priority
 * thread started at boot.
 */

#include <optional>
#include <string>

/// A GitHub release whose rammp-hmi-p4.bin is this very image.
struct FwRelease {
  std::string tag;     ///< e.g. "v4.0.0-alpha"
  bool prerelease;     ///< GitHub marks it a pre-release
  std::string checked; ///< when the PC compared the digests, as it wrote it
};

struct FwInfo {
  bool done = false;                 ///< the hash has been attempted
  std::optional<std::string> sha256; ///< 64 lowercase hex digits; nullopt if it failed
  std::optional<FwRelease> release;  ///< this image's line in fwinfo.txt, if any
};

/// Starts hashing the running image in the background. Once, after boot and
/// after /storage is mounted.
void fw_info_start();

/// What is known so far. Any task.
FwInfo fw_info();
