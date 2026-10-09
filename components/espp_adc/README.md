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
`managed_components/`), `upstream.diff` (upstream 1.3.2 to this copy), this file.

## Changes vs upstream

The full diff is `upstream.diff` (upstream 1.3.2 to this copy; `git apply -p1` from this
folder on upstream). `CMakeLists.txt`, `idf_component.yml`, `adc_types.hpp` and
`oneshot_adc.hpp` are byte-identical to upstream.

| File | Change | Why |
| --- | --- | --- |
| `include/adc_window.hpp` (new) | `AdcWindowAccumulator` (one channel's sum, count and, new, largest raw conversion in a window), `AdcWindowReading` (mean, max, sequence), `close_window()`: espp's mean arithmetic unchanged (`float(sum) / float(count)`, then the calibration), the max through the same calibration, the sequence + 1 for a window holding a conversion of that channel. Plain C++, so the host app L1-CTL tests it (CTL-024) | REQ-CTL-16 |
| `include/continuous_adc.hpp` | `update_task` keeps its per-channel sums in `AdcWindowAccumulator`s (was `sums_`, `num_samples_`) and closes each window with `close_window()` into `readings_`; `values_` and `actual_rates_` get exactly what they got before, so `get_mv()` and `get_rate()` return the same values. New `get_windows(configs)`: each channel's newest `AdcWindowReading`, all under one take of `data_mutex_`. Includes `adc_window.hpp` | REQ-CTL-16 |

Cost: one more `adc_cali_raw_to_voltage` per channel per window (the max), on espp's
`ContinuousAdc` task, not on the stick path. Nothing calls `get_windows()` yet (C2 commit 4
makes the island read both X/Y windows with it).

## Tools

Formatted by its upstream: excluded from clang-format (`.pre-commit-config.yaml`) and from the L0
ratchet (`tools/l0/ratchet.py`, like `components/m5stack-tab5`).
