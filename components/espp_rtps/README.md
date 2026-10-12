# espp_rtps: vendored copy of espp's `rtps` (CS-LAY-05)

espp's RTPS / DDS component (`espp::RtpsParticipant`, `espp::Publisher`, `espp::Subscriber` and
the embeddedRTPS engine under `include/rtps/`, `src/`), vendored for the owner's decision T1
([hazard-decisions.md](../../docs/plans/hazard-decisions.md)): the boot DISABLE (hazard fix C3,
row 50) is sent only once the DriveCommand writer has matched a reader, and the pinned espp
reports a match for no topic in particular. Done the way `components/espp_adc` was (owner
decision D6: vendor now, offer the change upstream). `README.upstream.md` is upstream's text,
unchanged. This file records where the copy came from and every change from it.

It replaces the registry dependency `espp/rtps` (removed from `main/idf_component.yml`, so the
component manager no longer downloads it); `main` requires `espp_rtps`. Its own dependencies
(`espp/base_component`, `cdr`, `socket`, `task`, `thread_pool`, `timer`) still come from the
registry through the copied `idf_component.yml`.

## Upstream

| | |
| --- | --- |
| Repository | https://github.com/esp-cpp/espp |
| Path | `components/rtps` |
| Revision | `038eea4a63d3bc4c287b6c1420924a646abb5ee2` |
| Version | espp/rtps 1.3.2 (component registry), as pinned by `main/idf_component.yml` before |
| License | MIT (espp) |

How this was checked (2026-10-11): all 76 copied files match the sha256 in the registry
archive's `CHECKSUMS.json` (`managed_components/espp__rtps`, `.component_hash` `9712e447...`),
and the copied `idf_component.yml` gives `version: 1.3.2` and that commit.

Not copied: `example/`, `interop/` and `CHECKSUMS.json`. Local only: `.component_hash` (kept
from `managed_components/`), this file.

## Changes vs upstream

None in this commit: the copy is byte-identical. The changes come with their own commits and
are listed here when they land.

## Checks

| Check | Upstream files (by name) | Our files (`pace_*`) |
| --- | --- | --- |
| L0 ratchet | out of scope, by name (`VENDORED_FILES`) | as any first-party file |
| clang-format | excluded (`^components/espp_rtps/(include\|src)/(?!pace_)`) | formatted |
| cppcheck | excluded by path (`.github/workflows/static_analysis.yml`) | scanned |
| fw-standards warnings | not first-party (`HMI_NOT_FIRST_PARTY`): the engine does not build under the full warning set | the full warning set, per file |
| G4 init order, G2 exports | not first-party objects (`tools/guards/guardlib.py` `VENDORED_UPSTREAM_DIRS`), as under `managed_components/` before | first-party (`VENDORED_OURS`: `pace/`) |
| D3 diagram | a vendored target (`gen_diagrams` `VENDORED_COMPONENTS`) | |
