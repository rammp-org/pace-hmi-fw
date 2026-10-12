# seat: the seat gate and the seat reading (docs/plans/hazard-seat-spec.md §7.1). Pure C++:
# no allocation, no LVGL, no IDF, no lock (REQ-SEAT-06). Safety-relevant (CS-SAF-01): it decides
# whether a seat press may become a SeatCommand. README.md has the requirements. Host tests:
# tests/host/seat (tests/manifest.d/seat.yaml).
#
# Header-only (constexpr), so an INTERFACE library: it takes no PRIVATE options, and
# fw_component_options() does not apply. Its includers build it with their own flags (hmi_ui,
# and the host app with the full fw-standards warning set and -Wswitch-enum).
idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES hmi_format
                                hmi_rtps_spec
                                stick)
