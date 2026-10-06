# joystick (vendored, modified espp/joystick)

A modified copy of espp's `joystick` component: the upstream X/Y joystick plus an
independent third (Z, twist) axis. Kept in `components/` under CS-LAY-05 because no
upstream release has the twist axis.

## Upstream

| | |
| --- | --- |
| Repository | https://github.com/esp-cpp/espp |
| Path | `components/joystick` |
| Revision | `615b8dfde3e14e5a40ac4d1e2c605182b9d31bfd` (espp 1.2.0) |
| Vendored | 2026-08-26, commit `a0af13e` ("Implemented joystick using joystick component; Added twist axis to joystick component") |
| License | MIT (espp) |

How the revision was found: the commit that vendored this copy did not record a revision, so
it was established from the files (2026-10-06). The project had just moved to espp 1.2.0
(`d04ce5e`, 2026-08-20). The base of `include/joystick.hpp`, `include/joystick_formatters.hpp`,
`src/joystick.cpp` and `CMakeLists.txt` is byte-identical (same git blob ids) to espp's files at
1.2.0 (`615b8df`). Those files have not changed upstream since `6d0d0f68` (2025-04-09) and are
still the same at espp 1.3.2 (`038eea4`). `upstream.diff` applies cleanly in both directions.
So the base content is certain. That the copy was taken at 1.2.0, rather than at another
revision with the same content, is inferred from the date.

Not copied from upstream: `README.md`, `idf_component.yml`, `example/`.
Local only (not upstream): `test/` (the host L1 app, see below) and this README.

## Changes vs upstream

The full diff is `upstream.diff` (upstream 1.2.0 to this copy, `test/` excluded). Every change
was made in `a0af13e`, and `src/` and `include/` have not changed since.

| File | What changed | Why |
| --- | --- | --- |
| `include/joystick.hpp` | `#include <optional>`; class doc explains the Z axis | Z axis |
| `include/joystick.hpp` | New `get_values_3d_fn` typedef; `Config::z_calibration` (`std::optional<FloatRangeMapper::Config>`, default `nullopt`) and `Config::get_values_3d` | Add a twist axis that is configured and read like X/Y |
| `include/joystick.hpp` | New public `set_z_calibration()`, `has_z()`, `update(x, y, z)`, `z()`, `raw_z()`; doc notes on `set_type()`, `update()`, `update(x, y)` and `position()` saying that Z is separate | Z axis API |
| `include/joystick.hpp` | `recalculate(x, y)` becomes `recalculate(x, y, std::optional<float> z)`; new members `z_mapper_`, `get_values_3d_`, `has_z_`, `raw_z_`, `z_` | Z axis state |
| `include/joystick.hpp` | New free function `espp::joystick_selftest()` | Boot-time self check of the mapping math (called from `main/main.cpp`) |
| `include/joystick.hpp` | Two doc comments re-wrapped and the `get_values` comment re-aligned, with no change in meaning | Reformatted by this repo's clang-format pre-commit hook (`components/joystick` is not excluded from it) |
| `include/joystick_formatters.hpp` | The `v`, `r` and `b` presentations also print Z (and raw Z) when `has_z_`; unchanged output for a 2-axis joystick | Show the twist in logs |
| `src/joystick.cpp` | `#include <cassert>`, `<cmath>` | Used by `joystick_selftest()` |
| `src/joystick.cpp` | Constructor stores `get_values_3d` and configures `z_mapper_` when `z_calibration` is set | Z axis |
| `src/joystick.cpp` | `set_type()`: comment only. Z keeps its own deadbands; the circular deadzone is X/Y only | Z axis |
| `src/joystick.cpp` | New `set_z_calibration()`, `has_z()`, `update(x, y, z)`, `z()`, `raw_z()`. `update()` prefers `get_values_3d_` when set. `update(x, y)` passes `nullopt`, so Z keeps its last value | Z axis |
| `src/joystick.cpp` | `recalculate()` maps Z with `z_mapper_` after the X/Y circular clamp, separately from it | A twist pot is a separate potentiometer, not a third dimension of the tilt, so a spherical deadzone over X/Y/Z would be wrong (see the class doc) |
| `src/joystick.cpp` | New `joystick_selftest()`: 17 `assert()` checks covering rectangular/circular mapping, deadzones, clamping and Z independence | Boot-time self check; ported one case per assert to the host L1 app |

## Local test

`test/` (added in `482b54c`) is the host L1 app `L1-JOY` (`tests/manifest.d/joystick.yaml`):
JOY-001..JOY-017 port the 17 asserts of `joystick_selftest()` and JOY-018 runs it. It tests
`src/` and `include/` as they are.

## Static analysis status

- clang-tidy: excluded. `.clang-tidy` `ExcludeHeaderFilterRegex` matches `components/joystick/`
  (CS-LAY-05).
- L0 ratchet (`tools/l0/ratchet.py`): in scope as legacy. Its debt is held in
  `tools/l0/baseline.json` and may shrink but never grow: `assert` 17 and `fn_over_60` 1
  (`src/joystick.cpp`, both from `joystick_selftest()`), and `typedef` 2
  (`include/joystick.hpp`, `get_values_fn` from upstream and `get_values_3d_fn` added here).
- clang-format: formatted by this repo's pre-commit hook. Unlike `m5stack-tab5`, it is not
  excluded.
- This feeds the `stick` pipeline, which is safety-relevant in `docs/plans/refactor.md`.
  `joystick_selftest()` uses `assert()`. That is legacy debt against CS-ERR-04 and is tracked by
  the ratchet.

## Dropping the fork

1. Upstream the Z axis to esp-cpp/espp. This is CS-LAY-07's "extend it upstream". The change
   is self-contained (the table above: 211 lines added and 17 removed, about 125 of the added lines without the
   self test) and keeps the
   2-axis behaviour and format output unchanged. `joystick_selftest()` should stay here, or
   become an upstream example or test, rather than library code.
2. Once a release has it, add `espp/joystick: '==<that version>'` to `main/idf_component.yml`
   with every other `espp/*` at that version (CS-LAY-06: one pinned version; the rest of espp
   is 1.3.2 now). Then delete `components/joystick/` and move `test/` to `tests/host/joystick/`.
3. No version alignment is needed before that: upstream joystick is identical at 1.2.0 and
   1.3.2, so this copy matches the pinned 1.3.2 `math` and `base_component` it builds against.
