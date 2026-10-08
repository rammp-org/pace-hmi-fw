# ota

The firmware image: what is running (its SHA-256 and whether it is a published release), the
GitHub release list, downloading and installing a release into the other app slot, and
confirming a boot so the bootloader does not roll it back.

The parsing it needs (the release list JSON, the image's first block, fwinfo.txt, the
confirms-its-boot marker search) is the pure, host-tested `components/ota_parse`. This
component is the I/O around it: HTTPS, flash, PSA SHA-256, the worker threads. The code moved
here from `main/fw_info.*` and `main/github_ota.*` (app-main-shrink V15, CS-LAY-01); the only
changes on the way were the splits and casts of the step-(a) commits.

## Requirements

| ID | Requirement | Verified by |
| --- | --- | --- |
| REQ-FWI-01 | `fw_info_start` hashes the running image once, on a low-priority thread, and `fw_info` reports it: not done yet, the SHA-256 (64 lowercase hex), or failed; and the release whose fwinfo.txt line names that digest, if any. | bench B4 (About) |
| REQ-FWI-02 | `fw_info_record_release` appends a line for an image the board verified against GitHub, unless one for that digest is already there. | not on the bench (needs an install) |
| REQ-FWI-03 | `github_releases_fetch` asks GitHub for the releases (blocking) and returns them newest first, drafts left out, or why not. | bench B4 (Update list) |
| REQ-FWI-04 | `github_ota_start` refuses while an install is running or done; otherwise it installs on its own thread: the image is streamed into the next OTA slot a 64 KB block at a time, the first block is checked (project, chip) before anything is written, ESP-IDF checks the image, and the SHA-256 must equal GitHub's digest when GitHub gives one. Only then is the next boot set. Any failure leaves the running image booting. `github_ota_status` reports the stage, progress, message and log from any task. | not on the bench (needs a published release) |
| REQ-FWI-05 | A new image that carries the marker `kHmiConfirmsItsBoot` ("RAMMP-HMI:confirms-its-boot:v1", external linkage so it stays whole in the image) is left to confirm its own boot; one without it (an older release) is marked valid at install, so it does not roll back by itself. | ota_parse L1 (marker search); install not on the bench |
| REQ-FWI-06 | `github_ota_boot_report` logs the slot running and a rollback, once at boot; `github_ota_boot_confirm` keeps the running image (no rollback after it). | bench B2 (boot log) |

## Interface

| Header | What |
| --- | --- |
| `fw_info.hpp` | `FwRelease`, `FwInfo`, `fw_info_start`, `fw_info`, `fw_info_record_release` |
| `github_ota.hpp` | `GithubRelease`, `GithubReleases`, `github_releases_fetch`, `OtaStage`, `OtaStatus`, `github_ota_start`, `github_ota_status`, `github_ota_boot_report`, `github_ota_boot_confirm` |

The headers keep their names, so main's callers (`main.cpp`, `about_ui.cpp`, `update_ui.cpp`)
only gained the component dependency.

## Tasks and dependencies

- Tasks: the image hash and the install each run on a thread of their own (espp/pthread,
  detached); the release list request runs on the caller's thread (main's Update worker).
  State is shared under two mutexes (legacy, CS-OWN-08; moved with `ratchet.py transfer`).
- Dependencies (private): `ota_parse`, `storage`, espp `logger` and `format`, ESP-IDF
  `app_update`, `bootloader_support`, `esp_app_format`, `esp_http_client`, `esp_partition`,
  `esp_timer`, `mbedtls` (PSA, certificate bundle), `pthread`, `spi_flash`, and
  `espressif__cjson`.
- Not safety-relevant: it never commands motion. A restart after an install is main's decision
  (`main/update_ui.cpp`, never while the chair is driving).
