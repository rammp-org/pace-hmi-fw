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

None yet: `CMakeLists.txt`, `idf_component.yml` and the three headers are byte-identical to
upstream 1.3.2, and `upstream.diff` is empty.

## Tools

Formatted by its upstream: excluded from clang-format (`.pre-commit-config.yaml`) and from the L0
ratchet (`tools/l0/ratchet.py`, like `components/m5stack-tab5`).
