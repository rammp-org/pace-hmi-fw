# settings

The user settings that survive a reboot: one typed spec table (`settings_spec.hpp`), the values
in RAM, and `/storage/settings.txt`.

A value lives in RAM from the table's default; `settings_load` reads the file once at boot, and
a setter saves the whole file when a value changes. The code moved here from `main/` unchanged
(app-main-shrink V15, CS-LAY-01). `AppliedSettings` holds what other tasks read of the settings
as atomics (the stick's mapping and scale, the sounds); what the other settings do when they
change (the theme, the flip) stays with their owners in main (`setting_store_observer`).

## Requirements

The behaviour is pinned by the host app `tests/host/settings` (manifest entry L1-SET, cases
SET-001..SET-064), written against this code before the move.

| ID | Requirement | Verified by |
| --- | --- | --- |
| REQ-SET-01 | Every parameter starts at its table default; `settings_get` of a parameter outside the table is 0. | L1-SET |
| REQ-SET-02 | `settings_load` reads "key value" pairs; a known key's value is clamped to its range; an unknown key followed by a number is skipped; the first token that is not a number stops the load. A missing file keeps the defaults. | L1-SET |
| REQ-SET-03 | `settings_set` clamps to the range and writes the whole file (through `storage_write`) only when the value changed; a parameter outside the table is ignored. | L1-SET |
| REQ-SET-04 | The spec table's invariants (ranges, defaults within them, pages, unique keys) hold at compile time and in `test_settings_spec.cpp`. | L1-SET |

## Interface

| Header | What |
| --- | --- |
| `settings.hpp` | `settings_load`, `settings_get`, `settings_set`, `settings_theme` / `settings_set_theme`, `settings_brightness` / `settings_set_brightness`, `kBrightnessMinPercent`, `kBrightnessMaxPercent` |
| `settings_applied.hpp` | `hmi::settings::AppliedSettings`: the stick sensitivity, drive speed, stick inversion and swap, and sounds, as atomics the Read ADC, touch and LVGL tasks read without the LVGL lock; `apply(param, value)` from the rows' observer (UI task). From `main/frag_state.inc` |
| `settings_spec.hpp` | `SETTINGS_PARAMS`, `SETTINGS_PAGES`, `SETTINGS_PARAM_*`, `SETTINGS_PAGE_*` and the ranges: the spec table (CS-TYP-03) |

The headers keep their names, so main's callers only gained the component dependency.

## Tasks and dependencies

- Tasks: none. Any task: a mutex guards the values and the save (legacy, CS-OWN-08; moved with
  `ratchet.py transfer`, not changed).
- Dependencies (private): `storage`, espp `logger`.
- Not safety-relevant in itself; two of its values (stick sensitivity, drive speed) feed the
  stick through `AppliedSettings` (main's instance, read by the Read ADC task).
