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
from `managed_components/`), `upstream/` (the pristine upstream files we changed, kept for the
ratchet to measure against and for the diff; not compiled), `upstream.diff` (upstream 1.3.2 to
this copy, `git apply -p1` from this folder), this file.

## Changes vs upstream (offered upstream to esp-cpp/espp)

Upstream code gets only additions, each marked `pace-hmi-fw`; our code is in `pace/`. The full
diff is `upstream.diff`. Every other file is byte-identical to upstream.

| File | Change | Why |
| --- | --- | --- |
| `include/rtps_participant.hpp` | the declaration `std::uint32_t matched_readers(std::string_view topic) const` | owner decision T1: whether the DriveCommand writer has a matched reader (the facade's two matched callbacks name no topic, and the engine's writers are private) |
| `include/rtps_participant.hpp` | the comments of `matched_callback_t`, `on_publisher_matched` and `on_subscriber_matched` | they had the two the wrong way round: `SEDPAgent.cpp` calls "publisher matched" when a remote writer matches one of our readers, and "subscriber matched" when a remote reader matches one of our writers |
| `pace/pace_matched_readers.cpp` (new) | the definition: the engine writer's `getProxiesCount()` (which takes the writer's own lock) for the topic; a linear search, no allocation. It takes no facade lock: our code stays lock-free (the L0 ratchet's `locks`, CS-OWN-08), so the caller keeps `add_writer()`, `remove_writer()` and `stop()` from running meanwhile (`main/rtps_comms.cpp` asks under its endpoints lock). Upstream would take `mutex_` | as above |
| `CMakeLists.txt` | the new source in `SRCS`; the first-party warning set on it, per file | our code under the full warning set (CS-LNG-02) |

## Checks

| Check | Upstream files (by name) | `include/rtps_participant.hpp` (upstream + our marked lines) | Our files (`pace/`) |
| --- | --- | --- | --- |
| L0 ratchet | out of scope, by name (`VENDORED_FILES`, `VENDORED_PRISTINE`) | no metric above the pristine copy's (`VENDORED_MODIFIED`); `lines` as usual | as any first-party file |
| clang-format | excluded (`^components/espp_rtps/(include\|src\|upstream)/`) | excluded: upstream's format kept, so the diff stays our lines | formatted |
| cppcheck | excluded by path (`.github/workflows/static_analysis.yml`) | excluded with the upstream headers (`include/rtps_` prefix): our lines there are a declaration and comments | scanned |
| fw-standards warnings | not first-party (`HMI_NOT_FIRST_PARTY`): the engine does not build under the full warning set | as upstream's | the full warning set, per file |
| G4 init order, G2 exports | not first-party objects (`tools/guards/guardlib.py` `VENDORED_UPSTREAM_DIRS`), as under `managed_components/` before | (header) | first-party (`VENDORED_OURS`: `pace/`) |
| D3 diagram | a vendored target (`gen_diagrams` `VENDORED_COMPONENTS`) | | |
