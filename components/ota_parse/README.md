# ota_parse

Pure parsers for what a firmware update reads from outside: GitHub's release list (JSON over
HTTPS), the first block of a downloaded image, `fwinfo.txt` from `/storage`, and the
confirms-its-boot marker in the image bytes.

No ESP-IDF, no HTTP, no logging. `main/github_ota.cpp` and `main/fw_info.cpp` do the I/O and
call these. The code moved here from `main/` (dev_refactor 54347bb) and gives the same answers,
as the tests below show: every golden runs against the moved code and a verbatim copy of the
old (`test/legacy_ota_parse.cpp`).

## Requirements

| ID | Requirement | Tests |
| --- | --- | --- |
| REQ-OTA-01 | A release list is a JSON array. Its releases come back in order, without drafts and without those with an empty `tag_name`. The asset named `rammp-hmi-p4.bin` gives the url, the size and, from a `digest` of `sha256:` plus 64 characters, the SHA-256. Anything but an array is the error "GitHub's answer was not a release list". | OTA-001..OTA-003, OTA-010..OTA-030 |
| REQ-OTA-02 | Release notes read as ASCII: the CI size report and HTML comments cut the rest, `**`, backticks and CRs are dropped, blank lines collapse to one, eight punctuation marks get ASCII stand-ins, other UTF-8 is dropped, and the text is cut at 1800 characters and then gets `...`. | OTA-040..OTA-049 |
| REQ-OTA-03 | The first block of a download is accepted only from 288 bytes, with both magics, this chip's ID and this project's name. Otherwise the answer says why. | OTA-060..OTA-068 |
| REQ-OTA-04 | `fwinfo.txt` gives the first line whose hash is this image's: `<sha256> <tag> <kind> [<checked>]`, `prerelease` only when the kind is exactly that. Comment lines (`#` first), blank lines and lines with fewer than three fields are skipped. | OTA-080..OTA-088 |
| REQ-OTA-05 | The confirms-its-boot marker is found in the downloaded bytes wherever the reads split it. | OTA-100..OTA-104 |
| REQ-OTA-06 | Each answer is what the code in `main/` gave before the move (goldens recorded from the verbatim copy). | OTA-001..OTA-104 |
| REQ-OTA-07 | Hostile input (empty, truncated, wrong types, oversized, non-UTF-8, deeply nested, random bytes) never hangs, overruns or crashes under ASan and UBSan (TS-UNIT-07). | OTA-010..OTA-030, OTA-045, OTA-046, OTA-049, OTA-061, OTA-066, OTA-068, OTA-087, OTA-088, OTA-103, OTA-104 |

## Interface

All in `ota_parse/ota_parse.hpp`, namespace `hmi::ota`:

| What | For |
| --- | --- |
| `parse_releases`, `Release`, `readable_notes`, `kNotesMaxChars` | the release list |
| `check_first_block`, `AppDesc`, the `k*` layout constants | the image's first block; `main/github_ota.cpp` asserts the layout against ESP-IDF's structs |
| `find_fw_record`, `FwRecord` | `fwinfo.txt`, from any `std::istream` |
| `MarkerSearch` | the marker, fed chunk by chunk |

`parse_releases` uses cJSON (`espressif__cjson`) with whatever allocation hooks the caller
installed: `main/` puts the nodes in PSRAM.

## Hazards (parked)

The tests pin these as they are ("today ..." cases). Each fix changes behaviour, so it is a
separate, reviewed change.

| ID | Hazard | Pinned by |
| --- | --- | --- |
| OTA-H1 | Before the move, `check_header` formatted the 32-byte `project_name` with fmt's `{:.32s}`, which takes `strlen` first: a description with no NUL from the name to its end was read past. The move reads each field within its size and prints the same text. | OTA-064, OTA-068 |
| OTA-H2 | cJSON parses recursively, up to `CONFIG_CJSON_NESTING_LIMIT` (1000, the Kconfig default). The release list is parsed on a 12 KiB thread (`main/update_ui.cpp:41`); about 100 B of stack per level (x86-64 `-Os` estimate) puts 1000 levels near 100 KB. | OTA-022 |
| OTA-H3 | No count limits: any number of releases or assets is walked (CS-FLW-02). | OTA-017, OTA-018 |
| OTA-H4 | No length limits on the tag, url or digest strings, or on `fwinfo.txt` lines. | OTA-016, OTA-087 |
| OTA-H5 | An asset size that is negative, fractional or too big for `size_t` is cast from `double`: undefined for negative or too-big values. | OTA-023, OTA-024 |
| OTA-H6 | A duplicated image asset gives the last one's url and size with the first one's digest. | OTA-003 |
| OTA-H7 | `assets` given as an object is walked like an array; the digest is not checked to be hex; bytes after the array are ignored; `\uZZZZ` reads as NUL. | OTA-015, OTA-021, OTA-025, OTA-030 |
| OTA-H8 | In `fwinfo.txt` any kind but exactly `prerelease` reads as a release. | OTA-084 |

Outside this component, also parked: the release-list body is read without a size limit
(`main/github_ota.cpp`, the read loop in `github_releases_fetch`, and `json.reserve` takes the
server's Content-Length unchecked), and `psa_hash_setup` / `psa_hash_update` results are not
checked in `install()`.
