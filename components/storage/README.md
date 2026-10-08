# storage

Files on the `storage` partition: LittleFS through espp's FileSystem, mounted at `/storage`.

It gives a file's full path, replaces a file atomically, and once, on the first boot of the
two-slot (OTA) layout, copies the files the old `storage` partition held into the new one. The
code moved here from `main/storage.*` unchanged (app-main-shrink V15, CS-LAY-01).

## Requirements

| ID | Requirement | Verified by |
| --- | --- | --- |
| REQ-STO-01 | `storage_path(name)` is `name` under the storage root; the first call mounts the partition, formatting a blank one. | bench B2 (settings and calibration load at boot) |
| REQ-STO-02 | `storage_write(name, contents)` writes `name.tmp` and renames it over `name`, so a reset mid-write leaves the previous file, never half of one. It logs and returns false when the write or the rename fails. | bench B2 |
| REQ-STO-03 | `storage_migrate_legacy()` copies every regular file at the old storage offset (0x810000, 4 MiB, inside ota_1) into storage, read-only, only when storage is empty and ota_1 has never held an app (no 0xE9 magic at its start). Once a file exists, or ota_1 holds an image, it does nothing. | not on the bench (a board's first boot of the two-slot layout) |

## Interface

| Header | What |
| --- | --- |
| `storage.hpp` | `storage_path`, `storage_write`, `storage_migrate_legacy` |

The header keeps its name and the functions their names, so the callers (`main.cpp`,
`settings`, `fw_info`, `joystick_cal`, `rtps_comms`) only gained the component dependency.

## Tasks and dependencies

- Tasks: none. Any task may call it; `storage_migrate_legacy` once from `app_main`, before
  anything reads storage.
- Dependencies (private): espp `file_system` and `logger`, `joltwallet__littlefs`,
  `esp_partition`.
- Not safety-relevant. Board-facing (flash): no host L1 app; the joystick_cal host test fakes
  `storage.hpp` (`components/joystick_cal/test/fakes/fake_storage.cpp`).
