# espp_adc: vendored copy of espp's `adc` (CS-LAY-05)

espp's ADC component (`espp::OneshotAdc`, `espp::ContinuousAdc`), vendored for hazard fix C2
([hazard-c2-spec.md](../../docs/plans/hazard-c2-spec.md), REQ-CTL-16; owner decision D6 of
[hazard-decisions.md](../../docs/plans/hazard-decisions.md): vendor now, offer the change
upstream later). `README.upstream.md` is upstream's text, unchanged. This file records where the
copy came from and every change from it.

It replaces the registry dependency `espp/adc` (removed from `main/idf_component.yml`, so the
component manager no longer downloads it); its users require `espp_adc`.

## Upstream

| | |
| --- | --- |
| Repository | https://github.com/esp-cpp/espp |
| Path | `components/adc` |
| Revision | `038eea4a63d3bc4c287b6c1420924a646abb5ee2` |
| Version | espp/adc 1.3.2 (component registry), as pinned by `main/idf_component.yml` before |
| License | MIT (espp) |

How this was checked (2026-10-08): every copied file matches the sha256 in the registry
archive's `CHECKSUMS.json` (`managed_components/espp__adc`, `.component_hash` `ff4f840b...`),
and the copied `idf_component.yml` gives `version: 1.3.2` and that commit.

Not copied: `example/` and `CHECKSUMS.json`. Local only: `.component_hash` (kept from
`managed_components/`), `upstream/continuous_adc.hpp` (the pristine upstream file, kept for the
ratchet to measure our hook lines against; not compiled), `upstream.diff` (upstream 1.3.2 to
this copy), this file.

## Changes vs upstream (owner decision 2026-10-08: our code stays under every check)

Our code is not in this component: the window bookkeeping is the first-party component
`components/adc_window` (`hmi::adc::AdcWindowBoard`), and the lock-free read is
`components/control/include/control/windowed_adc.hpp` (`WindowedContinuousAdc`, which reaches
the protected `windows_` and `get_index`). Upstream code here gets only additions, each marked
`pace-hmi-fw`. The full diff is `upstream.diff` (`git apply -p1` from this folder on upstream
1.3.2). `idf_component.yml`, `adc_types.hpp` and `oneshot_adc.hpp` are byte-identical to
upstream.

| File | Added | Why |
| --- | --- | --- |
| `CMakeLists.txt` | `adc_window` in `REQUIRES` | the hook lines include it |
| `include/continuous_adc.hpp` | the include of `adc_window/adc_window.hpp`; in `update_task`, `windows_.add(index, data)` beside espp's sum and, after espp's locked block, `windows_.close_and_publish(...)`; the protected helper `to_mv(index, raw)` (the conversion the update task uses for `values_`); the member `hmi::adc::AdcWindowBoard windows_` | REQ-CTL-16: each channel's window max and sequence, published lock-free |

Upstream's own code is untouched: `values_`, `actual_rates_`, `get_mv()` and `get_rate()` work
exactly as before. Cost: one more `adc_cali_raw_to_voltage` per channel per window (the max),
on espp's `ContinuousAdc` task, not on the stick path. Nothing reads the windows yet (C2 commit
4 makes the island read both X/Y windows through `WindowedContinuousAdc::get_windows`).

## Checks

| Check | Upstream files (`adc_types.hpp`, `oneshot_adc.hpp`, `upstream/`) | `continuous_adc.hpp` (upstream + our hook lines) | Our code (`components/adc_window`, `control/windowed_adc.hpp`) |
| --- | --- | --- | --- |
| L0 ratchet | out of scope, by name (`VENDORED_FILES`) | no metric above the pristine copy's count (`VENDORED_MODIFIED`); `lines` as usual | as any first-party file |
| clang-format | excluded, by name | formatted | formatted |
| cppcheck | scanned (no exclusion) | scanned | scanned |
| fw-standards warnings | first-party (the flag is per component; the upstream headers build clean under it) | first-party | first-party |
